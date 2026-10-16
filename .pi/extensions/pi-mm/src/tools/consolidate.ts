import { activeConversationTurn, type PiCtx, type PiToolSpec } from "../hooks/events.ts";
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

async function autoL0ToL1(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  items: StoredItem[], refs: number[], cited: Set<number>, date: string,
): Promise<number[]> {
  const uncited = items.filter((i) => i.envelope.level === "L0" && !cited.has(i.ref));
  const created: number[] = [];

  // Separate technical episodes and session-grouped conversational turns
  const technicalL0: StoredItem[] = [];
  const bySession = new Map<string, StoredItem[]>();
  const unassigned: StoredItem[] = [];

  for (const l0 of uncited) {
    if (l0.envelope.lifecycle === "superseded" || l0.envelope.lifecycle === "contradicted") continue;
    const isToolEpisode = l0.envelope.type === "episode" || (l0.envelope.episode?.approaches && l0.envelope.episode.approaches.length > 0);
    if (isToolEpisode) {
      technicalL0.push(l0);
      continue;
    }
    const sessId = l0.envelope.episode?.sessionId;
    if (sessId) {
      const g = bySession.get(sessId) ?? [];
      g.push(l0);
      bySession.set(sessId, g);
    } else {
      unassigned.push(l0);
    }
  }

  // 1. Process technical investigation episodes
  for (const l0 of technicalL0) {
    const worked = l0.envelope.episode?.approaches?.find((a) => a.outcome === "worked");
    const hasFindings = (l0.envelope.episode?.findings?.length ?? 0) > 0;
    const isSuccess = l0.envelope.episode?.outcome === "success" || Boolean(worked) || hasFindings;
    const lifecycle: LifecycleState = isSuccess ? "supported" : "observed";

    const problem = l0.envelope.episode?.problem || l0.envelope.summary;
    const summary = worked?.approach
      ? `Investigation: ${problem} — verified: ${worked.approach}`
      : `Investigation: ${problem}`;

    if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L1", [l0.ref], summary) !== null) {
      cited.add(l0.ref);
      continue;
    }
    const envl: MemoryEnvelope = { level: "L1", type: "investigation", lifecycle, summary, context: l0.envelope.context, evidence: [l0.ref] };
    const ref = await storeAbs(env, fspec, axes, cwd, eEnv, refs, envl, date);
    if (ref !== null) {
      created.push(ref);
      cited.add(l0.ref);
      items.push({ ref, date, envelope: envl });
    }
  }

  // 2. Process session-grouped L0 conversational turns
  for (const [sessId, group] of bySession.entries()) {
    const existingL1 = items.find((i) =>
      i.envelope.level === "L1" &&
      i.envelope.episode?.sessionId === sessId &&
      i.envelope.lifecycle !== "superseded" &&
      i.envelope.lifecycle !== "contradicted"
    );

    const groupFacts = group.map((g) => g.envelope.summary).filter((s): s is string => Boolean(s && s.trim()));
    const groupEvidence = group.map((g) => g.ref);

    if (existingL1) {
      // Create successor L1 memory incorporating new L0 facts
      const priorFindings = existingL1.envelope.episode?.findings ?? [existingL1.envelope.summary];
      const mergedFindings = Array.from(new Set([...priorFindings, ...groupFacts]));
      const priorEvidence = existingL1.envelope.evidence ?? [];
      const mergedEvidence = Array.from(new Set([...priorEvidence, ...groupEvidence]));

      let updatedSummary = existingL1.envelope.summary;
      for (const newFact of groupFacts) {
        if (!updatedSummary.toLowerCase().includes(newFact.toLowerCase())) {
          updatedSummary = `${updatedSummary} — ${newFact}`;
        }
      }
      if (updatedSummary.length > 250) {
        updatedSummary = updatedSummary.slice(0, 247) + "...";
      }

      if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L1", mergedEvidence, updatedSummary) !== null) {
        for (const r of groupEvidence) cited.add(r);
        continue;
      }

      const successorRef = nextRef(refs);
      refs.push(successorRef);

      const successorEnvelope: MemoryEnvelope = {
        level: "L1",
        type: existingL1.envelope.type ?? "note",
        lifecycle: "observed",
        summary: updatedSummary,
        supersedes: existingL1.ref,
        evidence: mergedEvidence,
        context: existingL1.envelope.context,
        episode: {
          problem: existingL1.envelope.episode?.problem || group[0].envelope.episode?.problem || updatedSummary,
          findings: mergedFindings,
          outcome: "success",
          sessionId: sessId,
          sessionFile: existingL1.envelope.episode?.sessionFile || group[0].envelope.episode?.sessionFile,
          leafId: group[group.length - 1].envelope.episode?.leafId || existingL1.envelope.episode?.leafId,
        },
      };

      const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, successorRef, encodePayload(date, successorEnvelope), axes, cwd, eEnv));
      if (res.code === 0) {
        // Mark predecessor superseded
        existingL1.envelope.lifecycle = "superseded";
        existingL1.envelope.supersededBy = successorRef;
        await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, existingL1.ref, encodePayload(existingL1.date, existingL1.envelope), axes, cwd, eEnv));

        // Update active in-memory session reference
        activeConversationTurn.activeMemoryRef = successorRef;
        activeConversationTurn.activeSessionId = sessId;

        created.push(successorRef);
        for (const r of groupEvidence) cited.add(r);
        items.push({ ref: successorRef, date, envelope: successorEnvelope });
      }
    } else {
      // Create new L1 episodic memory for this session
      const summary = groupFacts.length === 1 ? groupFacts[0] : groupFacts.join(" — ").slice(0, 250);

      if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L1", groupEvidence, summary) !== null) {
        for (const r of groupEvidence) cited.add(r);
        continue;
      }

      const l1Ref = nextRef(refs);
      refs.push(l1Ref);

      const l1Envelope: MemoryEnvelope = {
        level: "L1",
        type: "note",
        lifecycle: "observed",
        summary,
        evidence: groupEvidence,
        context: group[0].envelope.context,
        episode: {
          problem: group[0].envelope.episode?.problem || summary,
          findings: groupFacts,
          outcome: "success",
          sessionId: sessId,
          sessionFile: group[0].envelope.episode?.sessionFile,
          leafId: group[group.length - 1].envelope.episode?.leafId,
        },
      };

      const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, l1Ref, encodePayload(date, l1Envelope), axes, cwd, eEnv));
      if (res.code === 0) {
        activeConversationTurn.activeMemoryRef = l1Ref;
        activeConversationTurn.activeSessionId = sessId;

        created.push(l1Ref);
        for (const r of groupEvidence) cited.add(r);
        items.push({ ref: l1Ref, date, envelope: l1Envelope });
      }
    }
  }

  // 3. Process standalone L0 turns (no sessionId)
  for (const l0 of unassigned) {
    if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L1", [l0.ref], l0.envelope.summary) !== null) {
      cited.add(l0.ref);
      continue;
    }
    const l1Ref = nextRef(refs);
    refs.push(l1Ref);
    const l1Envelope: MemoryEnvelope = {
      level: "L1",
      type: l0.envelope.type ?? "note",
      lifecycle: l0.envelope.lifecycle,
      summary: l0.envelope.summary,
      evidence: [l0.ref],
      context: l0.envelope.context,
      episode: l0.envelope.episode,
    };
    const res = await env.runner.run(buildStoreInvocation(env.cfg.qmapBin, fspec, l1Ref, encodePayload(date, l1Envelope), axes, cwd, eEnv));
    if (res.code === 0) {
      created.push(l1Ref);
      cited.add(l0.ref);
      items.push({ ref: l1Ref, date, envelope: l1Envelope });
    }
  }

  return created;
}

function extractMemoryScope(l1: StoredItem): { scope: string; isConversational: boolean } | null {
  const ctx = l1.envelope.context;
  if (ctx?.project || ctx?.subsystem || ctx?.module) {
    return { scope: (ctx.project || ctx.subsystem || ctx.module)!, isConversational: false };
  }
  if (ctx?.subject) {
    return { scope: ctx.subject, isConversational: true };
  }
  if (l1.envelope.tags && l1.envelope.tags.length > 0) {
    return { scope: l1.envelope.tags[0], isConversational: true };
  }
  return null;
}

async function autoL1ToL2(
  env: ToolEnv, fspec: string, axes: string, cwd: string, eEnv: Record<string, string>,
  items: StoredItem[], refs: number[], cited: Set<number>, date: string,
): Promise<number[]> {
  const uncited = items.filter((i) => i.envelope.level === "L1" && !cited.has(i.ref));
  const byScope = new Map<string, { items: StoredItem[]; isConversational: boolean }>();
  for (const l1 of uncited) {
    if (l1.envelope.lifecycle === "contradicted" || l1.envelope.lifecycle === "superseded") continue;
    const scopeInfo = extractMemoryScope(l1);
    if (!scopeInfo) continue;
    const g = byScope.get(scopeInfo.scope) ?? { items: [], isConversational: scopeInfo.isConversational };
    g.items.push(l1);
    byScope.set(scopeInfo.scope, g);
  }
  const created: number[] = [];
  for (const [scope, group] of byScope.entries()) {
    const activeItems = group.items.filter((g) => g.envelope.lifecycle === "supported" || g.envelope.lifecycle === "observed");
    if (activeItems.length < 2) continue;

    const sRefs = activeItems.map((g) => g.ref);
    let summary: string;
    let type: MemoryEnvelope["type"] = "concept";
    const lifecycle: LifecycleState = activeItems.some((g) => g.envelope.lifecycle === "supported") ? "supported" : "observed";

    if (group.isConversational) {
      const summaries = activeItems.map((g) => g.envelope.summary).join("; ");
      summary = `Scoped Knowledge [${scope}]: ${summaries.slice(0, 240)}`;
    } else {
      const supportedOnly = activeItems.filter((g) => g.envelope.lifecycle === "supported");
      if (supportedOnly.length < 2) continue;
      const sharedRule = findSharedInvariantRule(supportedOnly);
      if (!sharedRule) continue; // Unrelated investigations in the same scope are NOT conjoined!
      summary = `Project Invariant [${scope}]: ${sharedRule}`;
      type = "invariant";
    }

    if (await checkIdempotency(env, fspec, axes, cwd, eEnv, refs, "L2", sRefs, summary) !== null) continue;
    const envl: MemoryEnvelope = {
      level: "L2",
      type,
      lifecycle,
      summary,
      context: group.isConversational ? { subject: scope } : { project: scope },
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

export async function runAutoConsolidation(env: ToolEnv, _ctx: PiCtx): Promise<{ content: Array<{ type: string; text?: string }>; details: unknown }> {
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
    if (targetLevel === "L2" && (!context || (!context.project && !context.module && !context.subsystem && !context.subject))) {
      return { content: [{ type: "text", text: "mm consolidate: L2 knowledge requires explicit scope (context.project/subsystem/module/subject)" }], details: { error: "scope-required" } };
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
