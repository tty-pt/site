import type { PiCtx, PiToolSpec } from "../hooks/events";
import type { EnvSource } from "./index";
import { axisPath, memDir, exportDetail, sepalConfigured } from "./index";
import { filespecFor, buildScanInvocation, scanExpr, parseResultLines, sepalLeafForText, embedEnv } from "../qmap";

function defaultLevel(l: unknown): number {
  if (typeof l === "number" && l >= 0 && l <= 2) return l;
  if (typeof l === "string" && ["0", "1", "2"].includes(l)) return Number(l);
  return 0;
}

function defaultLimit(n: unknown): number {
  if (typeof n === "number" && Number.isInteger(n) && n > 0) return n;
  return 10;
}

export function makeScanTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_scan",
    label: "Scan Memory",
    description: "Search stored memories by topic at a time-window level. level 0 = all-time (pure text), 1 = today, 2 = this month. embed=true adds semantic (sepal) recall when QMAP_SEPAL_EMBED_URL is configured. Returns {ref, score?, record}[].",
    parameters: {
      type: "object",
      properties: {
        topic: { type: "string", description: "Search topic (accent-sensitive, passed verbatim to the text index)." },
        level: { type: ["number", "string"], description: "Time resolution: 0 = all-time, 1 = today, 2 = this month. Default 0." },
        until: { type: "string", description: "ISO date (YYYY-MM-DD) upper bound. Caps the time window: level 0 searches all-time up to this date; level 1/2 caps the window end. Default: now." },
        embed: { type: "boolean", description: "Add semantic (sepal) ranking: libsepal embeds the topic server-side when QMAP_SEPAL_EMBED_URL + QMAP_SEPAL_EMBED_MODEL are configured. Unconfigured degrades to the plain text scan; a configured-but-unreachable endpoint fails the query loud (exit 1)." },
        limit: { type: "number", description: "Maximum records returned. Default 10." },
      },
      required: ["topic"],
      additionalProperties: false,
    },
    execute: async (_id, params, _signal, _onUpdate, ctx) => {
      try {
        const env = await envSource(ctx as PiCtx);
        const topic = typeof params["topic"] === "string" ? params["topic"].trim() : "";
        if (topic === "") return { content: [{ type: "text", text: "mm scan: empty topic (degradation: soft)" }], details: { records: [] } };
        if (env.cfg.qmapBin === "") return { content: [{ type: "text", text: "mm unavailable: qmap binary not found" }], details: { error: "no-qmap", records: [] } };

        const level = defaultLevel(params["level"]);
        const limit = defaultLimit(params["limit"]) || env.cfg.scanLimit;
        const untilRaw = params["until"];
        const until = typeof untilRaw === "string" && /^\d{4}-\d{2}-\d{2}/.test(untilRaw) ? untilRaw.slice(0, 10) : undefined;
        const wantEmbed = params["embed"] === true;
        const embedReady = wantEmbed && sepalConfigured(env.cfg);
        const details: Record<string, unknown> = {};
        // libsepal embeds the topic server-side at query time (Phase 6
        // `query=` leaf): no curl, no temp vector — just a text leaf.
        const sepalLeaf = embedReady ? sepalLeafForText(topic) : undefined;
        if (wantEmbed && !embedReady) details["embed"] = "unconfigured";
        const axes = axisPath(env);
        const cwd = memDir(env);
        const now = env.nowProvider();
        const expr = scanExpr(topic, level, now, until, sepalLeaf);
        const result = await env.runner.run(buildScanInvocation(env.cfg.qmapBin, filespecFor(cwd, embedReady), expr, limit, axes, cwd, embedEnv(env.cfg)));
        if (result.code !== 0) {
          return { content: [{ type: "text", text: `mm scan failed (exit ${result.code}): ${result.stderr || result.stdout}` }], details: { records: [], error: result.stderr || result.stdout, ...details } };
        }
        details["records"] = parseResultLines(result.stdout);
        return exportDetail(details);
      } catch (err) {
        const msg = err instanceof Error ? err.message : String(err);
        return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg, records: [] } };
      }
    },
  };
}

export async function scanTopRef(envSource: EnvSource, ctx: PiCtx, topic: string): Promise<number | null> {
  const env = await envSource(ctx as PiCtx);
  if (env.cfg.qmapBin === "") return null;
  const axes = axisPath(env);
  const cwd = memDir(env);
  const expr = scanExpr(topic, 0, env.nowProvider());
  const result = await env.runner.run(buildScanInvocation(env.cfg.qmapBin, filespecFor(cwd, sepalConfigured(env.cfg)), expr, 1, axes, cwd, embedEnv(env.cfg)));
  if (result.code !== 0) return null;
  const lines = parseResultLines(result.stdout);
  return lines.length > 0 ? lines[0].ref : null;
}