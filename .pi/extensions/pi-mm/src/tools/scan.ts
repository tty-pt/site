import type { PiCtx, PiToolSpec } from "../hooks/events.ts";
import type { EnvSource } from "./index.ts";
import { axisPath, memDir, exportDetail, sepalConfigured } from "./index.ts";
import { filespecFor, buildScanInvocation, scanExpr, parseResultLines, embedEnv, SEPAL_MIN_SIM, scanWindowArgs } from "../qmap.ts";
import { decodePayload } from "../payload.ts";
import type { MipmapLevel, LifecycleState, MemoryEnvelope } from "../types.ts";

const SCAN_PARAMS = {
  type: "object",
  properties: {
    topic: { type: "string", description: "Search topic (accent-sensitive, passed verbatim to text index)." },
    level: { type: ["number", "string"], description: "Resolution level ('L0'|'L1'|'L2'|'L3') or legacy time window (0 all-time, 1 today, 2 month). Default 0." },
    timeWindow: { type: ["number", "string"], description: "Time window when level is used for mipmaps: 0 = all-time, 1 = today, 2 = month." },
    lifecycle: { type: "string", enum: ["observed", "hypothesized", "tested", "supported", "contradicted", "superseded", "all"], description: "Filter by lifecycle state (default all)." },
    project: { type: "string", description: "Filter by context project name." },
    subsystem: { type: "string", description: "Filter by context subsystem." },
    bbox: {
      type: "object",
      description: "Spatial bounding box for islet axis (dim, s=min_corner, l=extent).",
      properties: {
        dim: { type: "number" },
        s: { type: "string" },
        l: { type: "string" },
      },
      required: ["s", "l"],
    },
    until: { type: "string", description: "ISO date upper bound (YYYY-MM-DD)." },
    embed: { type: "boolean", description: "Add semantic (sepal) ranking when configured." },
    limit: { type: "number", description: "Maximum records returned (default 10)." },
  },
  required: ["topic"],
  additionalProperties: false,
};

function resolveTimeLevel(lvl: unknown, timeWin: unknown): number {
  if (typeof timeWin === "number" && timeWin >= 0 && timeWin <= 2) return timeWin;
  if (typeof timeWin === "string" && ["0", "1", "2"].includes(timeWin)) return Number(timeWin);
  if (typeof lvl === "number" && lvl >= 0 && lvl <= 2) return lvl;
  if (typeof lvl === "string" && ["0", "1", "2"].includes(lvl)) return Number(lvl);
  return 0;
}

function resolveMipmapFilter(lvl: unknown): MipmapLevel | undefined {
  if (typeof lvl === "string" && ["L0", "L1", "L2", "L3"].includes(lvl)) {
    return lvl as MipmapLevel;
  }
  return undefined;
}

function defaultLimit(n: unknown): number {
  if (typeof n === "number" && Number.isInteger(n) && n > 0) return n;
  return 10;
}

export interface EnrichedRecord {
  ref: number;
  score?: string;
  record: string;
  envelope?: MemoryEnvelope;
  level?: MipmapLevel;
  lifecycle?: LifecycleState;
  summary?: string;
}

interface FilterCriteria {
  mipmap?: MipmapLevel;
  lifecycle?: string;
  project?: string;
  subsystem?: string;
  limit: number;
}

function computeRankingScore(rec: EnrichedRecord, crit: FilterCriteria): number {
  let score = rec.score ? parseFloat(rec.score) : 1.0;
  if (isNaN(score)) score = 1.0;

  const env = rec.envelope;
  if (!env) return score;

  // Lifecycle state factor
  const lifecycleWeights: Record<LifecycleState, number> = {
    supported: 1.0,
    tested: 0.95,
    observed: 0.9,
    hypothesized: 0.7,
    contradicted: 0.1,
    superseded: 0.05,
  };
  score *= (lifecycleWeights[env.lifecycle] ?? 0.8);

  // Provenance bonus
  if (env.evidence && env.evidence.length > 0) {
    score += 0.1;
  }

  // Utility feedback factor
  if (env.utility) {
    const net = (env.utility.usedCount || 0) - (env.utility.irrelevantCount || 0) - (env.utility.obsoleteCount || 0) * 2;
    score += Math.max(-0.3, Math.min(0.3, net * 0.05));
  }

  // Scope compatibility bonus
  if (crit.project && env.context?.project === crit.project) score += 0.15;
  if (crit.subsystem && env.context?.subsystem === crit.subsystem) score += 0.15;

  return score;
}

function filterAndEnrich(rawLines: ReturnType<typeof parseResultLines>, crit: FilterCriteria): EnrichedRecord[] {
  const candidates: EnrichedRecord[] = [];
  for (const line of rawLines) {
    const { envelope } = decodePayload(line.record);
    if (crit.mipmap && envelope.level !== crit.mipmap) continue;
    if (crit.lifecycle && crit.lifecycle !== "all" && envelope.lifecycle !== crit.lifecycle) continue;
    if (crit.project && envelope.context?.project !== crit.project) continue;
    if (crit.subsystem && envelope.context?.subsystem !== crit.subsystem) continue;

    candidates.push({
      ...line,
      envelope,
      level: envelope.level,
      lifecycle: envelope.lifecycle,
      summary: envelope.summary,
    });
  }

  // Multi-factor ranking
  candidates.sort((a, b) => computeRankingScore(b, crit) - computeRankingScore(a, crit));

  return candidates.slice(0, crit.limit);
}

async function executeScan(envSource: EnvSource, params: Record<string, unknown>, ctx: PiCtx) {
  try {
    const env = await envSource(ctx);
    const topic = typeof params["topic"] === "string" ? params["topic"].trim() : "";
    if (topic === "") return { content: [{ type: "text", text: "mm scan: empty topic (degradation: soft)" }], details: { records: [] } };
    if (env.cfg.qmapBin === "") return { content: [{ type: "text", text: "mm unavailable: qmap binary not found" }], details: { error: "no-qmap", records: [] } };

    const timeLevel = resolveTimeLevel(params["level"], params["timeWindow"]);
    const limit = defaultLimit(params["limit"]) || env.cfg.scanLimit;
    const untilRaw = params["until"];
    const until = typeof untilRaw === "string" && /^\d{4}-\d{2}-\d{2}/.test(untilRaw) ? untilRaw.slice(0, 10) : undefined;
    const wantEmbed = params["embed"] === true;
    const embedReady = wantEmbed && sepalConfigured(env.cfg);
    const details: Record<string, unknown> = {};
    if (wantEmbed && !embedReady) details["embed"] = "unconfigured";

    const axes = axisPath(env);
    const cwd = memDir(env);
    const now = env.nowProvider();
    const bbox = params["bbox"] as { dim?: number; s: string; l: string } | undefined;

    const extraArgs: string[] = [
      ...scanWindowArgs(timeLevel, now, until),
      "--field=text",
      "--matched=1",
      "--query=" + topic,
      ...(embedReady ? ["--min-sim=" + String(SEPAL_MIN_SIM)] : []),
    ];

    let isletRefs: Set<number> | null = null;
    if (bbox) {
      const isletFspec = `${cwd}/mem-islet.db@islet:a:s`;
      const isletRes = await env.runner.run(buildScanInvocation(
        env.cfg.qmapBin,
        isletFspec,
        "islet",
        limit * 2,
        axes,
        cwd,
        undefined,
        [`--dim=${bbox.dim ?? 2}`, `--s=${bbox.s}`, `--l=${bbox.l}`],
      ));
      if (isletRes.code === 0) {
        isletRefs = new Set(parseResultLines(isletRes.stdout).map((l) => l.ref));
      } else {
        isletRefs = new Set();
      }
    }

    const expr = scanExpr(topic, timeLevel, now, until, embedReady);
    const result = await env.runner.run(buildScanInvocation(env.cfg.qmapBin, filespecFor(cwd, embedReady), expr, limit, axes, cwd, embedEnv(env.cfg), extraArgs));
    if (result.code !== 0) {
      return { content: [{ type: "text", text: `mm scan failed (exit ${result.code}): ${result.stderr || result.stdout}` }], details: { records: [], error: result.stderr || result.stdout, ...details } };
    }

    let rawLines = parseResultLines(result.stdout);
    if (isletRefs !== null) {
      rawLines = rawLines.filter((l) => isletRefs!.has(l.ref));
    }
    details["records"] = filterAndEnrich(rawLines, {
      mipmap: resolveMipmapFilter(params["level"]),
      lifecycle: typeof params["lifecycle"] === "string" ? params["lifecycle"] : undefined,
      project: typeof params["project"] === "string" ? params["project"].trim() : undefined,
      subsystem: typeof params["subsystem"] === "string" ? params["subsystem"].trim() : undefined,
      limit,
    });
    return exportDetail(details);
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg, records: [] } };
  }
}

export function makeScanTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_scan",
    label: "Scan Memory",
    description: "Search stored memories across time, text, semantics, and mipmap resolution levels (L0-L3). Returns matching records with decoded metadata.",
    parameters: SCAN_PARAMS,
    execute: (_id, params, _sig, _upd, ctx) => executeScan(envSource, params, ctx as PiCtx),
  };
}

export async function scanTopRef(envSource: EnvSource, ctx: PiCtx, topic: string): Promise<number | null> {
  const env = await envSource(ctx as PiCtx);
  if (env.cfg.qmapBin === "") return null;
  const axes = axisPath(env);
  const cwd = memDir(env);
  const expr = scanExpr(topic, 0, env.nowProvider());
  const result = await env.runner.run(buildScanInvocation(env.cfg.qmapBin, filespecFor(cwd, sepalConfigured(env.cfg)), expr, 1, axes, cwd, embedEnv(env.cfg), ["--field=text", "--matched=1", "--query=" + topic]));
  if (result.code !== 0) return null;
  const lines = parseResultLines(result.stdout);
  return lines.length > 0 ? lines[0].ref : null;
}
