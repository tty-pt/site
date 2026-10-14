import type { PiCtx, PiToolSpec } from "../hooks/events.ts";
import type { EnvSource, ToolEnv } from "./index.ts";
import { axisPath, memDir, exportDetail, sepalConfigured } from "./index.ts";
import { filespecFor, buildGetInvocation, buildStoreInvocation, buildListInvocation, embedEnv } from "../qmap.ts";
import { decodePayload, encodePayload } from "../payload.ts";
import { parseBareRefs, nextRef } from "../resolve.ts";
import { withLock } from "../lock.ts";
import type { MipmapLevel, MemoryEnvelope, MemoryContext, LifecycleState } from "../types.ts";

const LEVEL_RANK: Record<MipmapLevel, number> = { L0: 0, L1: 1, L2: 2, L3: 3 };

const CONSOLIDATE_PARAMS = {
  type: "object",
  properties: {
    mode: { type: "string", enum: ["manual", "auto"], description: "Consolidation mode ('auto' or 'manual'). Default manual." },
    targetLevel: { type: "string", enum: ["L1", "L2", "L3"], description: "Target mipmap level (L1, L2, L3)." },
    sourceRefs: { type: "array", items: { type: "number" }, description: "Supporting evidence refs." },
    summary: { type: "string", description: "Consolidated statement." },
    problem: { type: "string", description: "Problem or investigation focus (L1)." },
    context: {
      type: "object",
      properties: { project: { type: "string" }, subsystem: { type: "string" }, module: { type: "string" }, path: { type: "string" } },
    },
    tags: { type: "array", items: { type: "string" } },
  },
  additionalProperties: false,
};

interface StoredItem {
  ref: number;
  date: string;
  envelope: MemoryEnvelope;
}

async function checkIdempotency(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  allRefs: number[], targetLevel: MipmapLevel, sourceRefs: number[], summary: string,
): Promise<number | null> {
  const sorted = [...sourceRefs].sort((a, b) => a - b);
  for (const r of allRefs) {
    const res = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, r, axes, cwd, eEnv));
    if (res.code === 0 && res.stdout.trim()) {
      const { envelope } = decodePayload(res.stdout.trim());
      if (envelope.level === targetLevel && envelope.summary === summary) {
        const ev = [...(envelope.evidence || [])].sort((a, b) => a - b);
        if (ev.length === sorted.length && ev.every((v, i) => v === sorted[i])) return r;
      }
    }
  }
  return null;
}

async function validateSources(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  sourceRefs: number[], targetLevel: MipmapLevel, targetLifecycle?: LifecycleState,
): Promise<{ valid: boolean; error?: string; missing: number[] }> {
  const missing: number[] = [];
  const targetRank = LEVEL_RANK[targetLevel];
  for (const r of sourceRefs) {
    const res = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, r, axes, cwd, eEnv));
    if (res.code !== 0 || !res.stdout.trim()) {
      missing.push(r);
      continue;
    }
    const { envelope } = decodePayload(res.stdout.trim());
    if ((LEVEL_RANK[envelope.level] ?? 0) >= targetRank) {
      return { valid: false, error: `cannot use ${envelope.level} source (ref ${r}) to create ${targetLevel} abstraction`, missing: [] };
    }
    if ((targetLifecycle === "supported" || !targetLifecycle) && (envelope.lifecycle === "contradicted" || envelope.lifecycle === "superseded")) {
      return { valid: false, error: `cannot promote ${envelope.lifecycle} source (ref ${r}) into supported conclusion`, missing: [] };
    }
  }
  return { valid: missing.length === 0, missing };
}

async function scanAllItems(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
): Promise<StoredItem[]> {
  const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd, eEnv));
  const refs = parseBareRefs(list.stdout);
  const items: StoredItem[] = [];
  for (const r of refs) {
    const res = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, r, axes, cwd, eEnv));
    if (res.code === 0 && res.stdout.trim()) {
      const { date, envelope } = decodePayload(res.stdout.trim());
      items.push({ ref: r, date, envelope });
    }
  }
  return items;
}

async function storeAbs(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  refs: number[], envelope: MemoryEnvelope, date: string,
): Promise<number | null> {
  const ref = nextRef(refs);
  refs.push(ref);
  const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, ref, encodePayload(date, envelope), axes, cwd, eEnv));
  return res.code === 0 ? ref : null;
}

async function autoL0ToL1(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  items: StoredItem[], refs: number[], cited: Set<number>, date: string,
): Promise<number[]> {
  const uncited = items.filter((i) => i.envelope.level === "L0" && !cited.has(i.ref));
  const created: number[] = [];
  for (const l0 of uncited) {
    if (l0.envelope.lifecycle === "superseded" || l0.envelope.lifecycle === "contradicted") continue;
    const worked = l0.envelope.episode?.approaches?.find((a) => a.outcome === "worked");
    const hasFindings = (l0.envelope.episode?.findings?.length ?? 0) > 0;
    const isSuccess = l0.envelope.episode?.outcome === "success" || Boolean(worked) || hasFindings;
    const lifecycle: LifecycleState = isSuccess ? "supported" : "observed";

    const problem = l0.envelope.episode?.problem || l0.envelope.summary;
    const summary = worked?.approach
      ? `Investigation: ${problem} — verified: ${worked.approach}`
      : `Investigation: ${problem}`;

    if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L1", [l0.ref], summary) !== null) continue;
    const envl: MemoryEnvelope = { level: "L1", type: "investigation", lifecycle, summary, context: l0.envelope.context, evidence: [l0.ref] };
    const ref = await storeAbs(env, fspec, axes, cwd, eEnv, refs, envl, date);
    if (ref !== null) {
      created.push(ref);
      cited.add(l0.ref);
      items.push({ ref, date, envelope: envl });
    }
  }
  return created;
}

const STOP_WORDS = new Set([
  "investigation", "observed", "episode", "verified", "issue", "problem", "error", "session", "test", "fix",
]);

function hasConflictingFindings(investigations: StoredItem[]): boolean {
  let hasAlways = false;
  let hasNever = false;
  for (const inv of investigations) {
    const text = `${inv.envelope.summary} ${inv.envelope.episode?.problem || ""}`.toLowerCase();
    if (text.includes("never") || text.includes("avoid") || text.includes("refute") || text.includes("prohibit")) hasNever = true;
    if (text.includes("always") || text.includes("require") || text.includes("must") || text.includes("enforce")) hasAlways = true;
  }
  return hasAlways && hasNever;
}

function findSharedInvariantRule(investigations: StoredItem[]): string | null {
  if (investigations.length < 2) return null;
  if (hasConflictingFindings(investigations)) return null; // Conflicting conclusions cannot form an invariant!
  const tokenSets = investigations.map((inv) => {
    const text = `${inv.envelope.summary} ${inv.envelope.episode?.problem || ""}`.toLowerCase();
    const words = text
      .split(/\s+/)
      .map((w) => w.replace(/[^\w]/g, ""))
      .filter((w) => w.length >= 4 && !STOP_WORDS.has(w));
    return new Set(words);
  });
  const common = [...tokenSets[0]].filter((token) => tokenSets.every((ts) => ts.has(token)));
  if (common.length === 0) return null;
  return `Hypothesized rule for ${common.join(" ")}`;
}

async function autoL1ToL2(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  items: StoredItem[], refs: number[], cited: Set<number>, date: string,
): Promise<number[]> {
  const uncited = items.filter((i) => i.envelope.level === "L1" && !cited.has(i.ref));
  const byScope = new Map<string, StoredItem[]>();
  for (const l1 of uncited) {
    if (l1.envelope.lifecycle === "contradicted" || l1.envelope.lifecycle === "superseded") continue;
    const scope = l1.envelope.context?.project || l1.envelope.context?.subsystem || l1.envelope.context?.module;
    if (!scope) continue;
    const g = byScope.get(scope) ?? [];
    g.push(l1);
    byScope.set(scope, g);
  }
  const created: number[] = [];
  for (const [scope, group] of byScope.entries()) {
    const supported = group.filter((g) => g.envelope.lifecycle === "supported");
    if (supported.length < 2) continue; // Single investigation or unverified items do not form an invariant rule

    const sharedRule = findSharedInvariantRule(supported);
    if (!sharedRule) continue; // Unrelated investigations in the same scope are NOT conjoined!

    const sRefs = supported.map((g) => g.ref);
    const summary = `Project Invariant [${scope}]: ${sharedRule}`;

    if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L2", sRefs, summary) !== null) continue;
    // Without an active semantic synthesis model, automated abstractions are recorded as hypotheses
    const envl: MemoryEnvelope = {
      level: "L2",
      type: "invariant",
      lifecycle: "hypothesized",
      summary,
      context: { project: scope },
      evidence: sRefs,
    };
    const ref = await storeAbs(env, fspec, axes, cwd, eEnv, refs, envl, date);
    if (ref !== null) {
      created.push(ref);
      for (const r of sRefs) cited.add(r);
      items.push({ ref, date, envelope: envl });
    }
  }
  return created;
}

async function runAutoConsolidation(env: ToolEnv, _ctx: PiCtx): Promise<{ content: Array<{ type: string; text?: string }>; details: unknown }> {
  const embed = sepalConfigured(env.cfg);
  const cwd = memDir(env);
  const fspec = filespecFor(cwd, embed);
  const axes = axisPath(env);
  const eEnv = embedEnv(env.cfg);

  return await withLock(cwd, async () => {
    const items = await scanAllItems(env, fspec, axes, cwd, eEnv);
    const refs = items.map((i) => i.ref);
    const cited = new Set<number>();
    for (const it of items) for (const ev of it.envelope.evidence || []) cited.add(ev);
    const date = env.nowProvider().toISOString().slice(0, 10);

    const createdL1 = await autoL0ToL1(env, fspec, axes, cwd, eEnv, items, refs, cited, date);
    const createdL2 = await autoL1ToL2(env, fspec, axes, cwd, eEnv, items, refs, cited, date);

    return exportDetail({
      mode: "auto",
      createdL1,
      createdL2,
      createdL3: [],
      totalCreated: createdL1.length + createdL2.length,
      l3Status: "unavailable (synthesis model not configured)",
    });
  });
}

async function validateL3Generalization(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  sourceRefs: number[],
): Promise<{ valid: boolean; error?: string }> {
  if (sourceRefs.length === 0) {
    return { valid: false, error: "L3 general lesson requires at least 1 validated project knowledge source" };
  }
  const sourceEnvelopes: MemoryEnvelope[] = [];
  for (const r of sourceRefs) {
    const item = await env.runner.run(buildGetInvocation(env.cfg.qmapBin, fspec, r, axes, cwd, eEnv));
    if (item.code === 0 && item.stdout.trim()) {
      const { envelope } = decodePayload(item.stdout.trim());
      if (envelope.level !== "L2" || envelope.lifecycle !== "supported") {
        return { valid: false, error: `source ref ${r} is not a supported L2 project invariant` };
      }
      sourceEnvelopes.push(envelope);
    }
  }
  if (sourceRefs.length >= 2) {
    const distinctScopes = new Set(sourceEnvelopes.map((e) => e.context?.project || e.context?.subsystem).filter(Boolean));
    if (distinctScopes.size < 2) {
      return { valid: false, error: "cannot generalize L3 across a single project scope" };
    }
  }
  return { valid: true };
}

async function executeConsolidate(
  envSource: EnvSource, params: Record<string, unknown>, ctx: PiCtx,
): Promise<{ content: Array<{ type: string; text?: string }>; details?: unknown }> {
  try {
    const env = await envSource(ctx);
    if (env.cfg.qmapBin === "") return { content: [{ type: "text", text: "mm unavailable: qmap binary not found" }], details: { error: "no-qmap" } };
    if (params["mode"] === "auto") return await runAutoConsolidation(env, ctx);

    const targetLevel = params["targetLevel"] as MipmapLevel;
    const sourceRefs = (params["sourceRefs"] as number[]) || [];
    const summary = typeof params["summary"] === "string" ? params["summary"].trim() : "";
    const problem = typeof params["problem"] === "string" ? params["problem"].trim() : undefined;
    const context = params["context"] as MemoryContext | undefined;
    const tags = params["tags"] as string[] | undefined;

    if (!summary || sourceRefs.length === 0) return { content: [{ type: "text", text: "mm consolidate: summary and non-empty sourceRefs required" }], details: { error: "invalid-params" } };
    if (targetLevel === "L2" && (!context || (!context.project && !context.module && !context.subsystem))) {
      return { content: [{ type: "text", text: "mm consolidate: L2 project knowledge requires explicit scope (context.project/subsystem/module)" }], details: { error: "scope-required" } };
    }

    const cwd = memDir(env);
    const fspec = filespecFor(cwd, sepalConfigured(env.cfg));
    const axes = axisPath(env);
    const eEnv = embedEnv(env.cfg);

    return await withLock(cwd, async () => {
      const { valid, error, missing } = await validateSources(env, fspec, axes, cwd, eEnv, sourceRefs, targetLevel, "supported");
      if (!valid) {
        const errType = error?.includes("cannot use") ? "invalid-level-transition" : "invalid-source";
        if (error) return { content: [{ type: "text", text: `mm consolidate: ${error}` }], details: { error: errType } };
        return { content: [{ type: "text", text: `mm consolidate: supporting evidence refs missing: ${missing.join(", ")}` }], details: { error: "missing-sources", missing } };
      }

      if (targetLevel === "L3") {
        const l3Val = await validateL3Generalization(env, fspec, axes, cwd, eEnv, sourceRefs);
        if (!l3Val.valid) {
          return { content: [{ type: "text", text: `mm consolidate: ${l3Val.error}` }], details: { error: "unsupported-generalization" } };
        }
      }

      const list = await env.runner.run(buildListInvocation(env.cfg.qmapBin, fspec, axes, cwd, eEnv));
      const allRefs = parseBareRefs(list.stdout);
      const existingRef = await checkIdempotency(env, fspec, axes, cwd, eEnv, allRefs, targetLevel, sourceRefs, summary);
      if (existingRef !== null) return exportDetail({ ref: existingRef, idempotent: true, level: targetLevel, summary, evidence: sourceRefs });

      const date = env.nowProvider().toISOString().slice(0, 10);
      const envelope: MemoryEnvelope = {
        level: targetLevel,
        type: targetLevel === "L1" ? "investigation" : targetLevel === "L2" ? "invariant" : "pattern",
        lifecycle: "supported",
        summary,
        context,
        evidence: sourceRefs,
        tags,
        episode: problem ? { problem, request: problem } : undefined,
      };
      const ref = await storeAbs(env, fspec, axes, cwd, eEnv, allRefs, envelope, date);
      if (ref === null) return { content: [{ type: "text", text: `mm consolidate failed: store error` }], details: { error: "store-error" } };
      return exportDetail({ ref, idempotent: false, level: targetLevel, summary, evidence: sourceRefs });
    });
  } catch (err) {
    const msg = err instanceof Error ? err.message : String(err);
    return { content: [{ type: "text", text: `mm unavailable: ${msg}` }], details: { error: msg } };
  }
}

export function makeConsolidateTool(envSource: EnvSource): PiToolSpec {
  return {
    name: "memory_consolidate",
    label: "Consolidate Memory Mipmap",
    description: "Consolidate lower-level memories into higher-level abstractions (L1 Investigation, L2 Project Knowledge, L3 General Lesson). Supports manual synthesis and 'auto' discovery.",
    parameters: CONSOLIDATE_PARAMS,
    execute: (_id, params, _sig, _upd, ctx) => executeConsolidate(envSource, params, ctx as PiCtx),
  };
}
