// HIGH_LEVEL: #tools (main agent) — the draft-file write path for plan and
// analysis deliverables: splice → hash → state → boot review. Kept out of
// update-state.ts so that file stays under the complexity budget.
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { updateState } from "../../app/store";
import { emitNow } from "../../app/interpreter";
import type { QuestState } from "../../domain/quest";
import { recordPlanRevision } from "../../domain/plan-revision";
import { draftPath } from "../../domain/paths";
import { bootPlanRevisionReview, clearPlanRevisionRetries } from "../../implementing/plan-revision";
import { clearDraftReviewRetry, maybeBootDraftReview } from "../../drafting/reviews";
import { hashContent, parseDraftSections, seedReviewCount, spliceAnalysisSection, splicePlanSection } from "../../drafting/plan-text";
import { noteDraftUpdated } from "../../durability/status";
import type { Pi, PiCtx } from "../../hooks/events";

export async function writePlanToDraft(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  planText: string,
): Promise<QuestState> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) {
    throw new Error(`plan text needs an active draft (phase ${state.phase}); the plan is drafting-only — record post-approval deviations via amendment or refinement`);
  }
  if (state.kind !== "standard") {
    throw new Error("plan text is for standard quests — author the ## Analysis via {analysis: ...} instead");
  }
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const updated = splicePlanSection(current, seedReviewCount(planText));
  if (updated === current) throw new Error("plan text identical to the draft file");
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  const next = updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, planAuthored: true, contentHash: hash },
    snapshotPending: true,
  });
  emitNow(pi);
  noteDraftUpdated(ctx);
  clearDraftReviewRetry(state.qid);
  void maybeBootDraftReview(pi, ctx);
  return next;
}

export async function writeAnalysisToDraft(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  analysisText: string,
): Promise<QuestState> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) {
    throw new Error(`analysis text needs an active draft (phase ${state.phase}); the analysis is drafting-only`);
  }
  if (state.kind !== "analysis") {
    throw new Error("analysis text is for analysis quests — author the ## Implementation Plan via {plan: ...} instead");
  }
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const updated = spliceAnalysisSection(current, seedReviewCount(analysisText));
  if (updated === current) throw new Error("analysis text identical to the draft file");
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  const next = updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, planAuthored: true, contentHash: hash },
    snapshotPending: true,
  });
  emitNow(pi);
  noteDraftUpdated(ctx);
  clearDraftReviewRetry(state.qid);
  void maybeBootDraftReview(pi, ctx);
  return next;
}

// Mid-implementation plan revision: the objective is immutable here — a new
// goal is a scope change and belongs in a new quest. The revision stages new
// content and boots a re-review; the approved binding moves only on PASS.
export async function writePlanRevision(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  planText: string,
  note: string,
  objective: unknown,
): Promise<QuestState> {
  if (state.phase !== "implementing" || state.draft === null || state.qid === null) {
    throw new Error(`plan revision needs an implementing quest (phase ${state.phase})`);
  }
  if (typeof objective === "string" && objective.trim() !== "") {
    const current = state.pendingRootRequest ?? state.objective;
    if (objective.trim() !== current) {
      throw new Error("plan revision keeps the quest objective — a new goal is a scope change, start a new quest");
    }
  }
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const previousPlan = parseDraftSections(current).plan;
  const updated = splicePlanSection(current, seedReviewCount(planText));
  if (updated === current) throw new Error("plan revision identical to the draft file");
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  const previousHash = state.draft.contentHash;
  const next = updateState((s) => recordPlanRevision(s, previousHash, hash, note === "" ? "plan revision" : note, previousPlan));
  emitNow(pi);
  noteDraftUpdated(ctx);
  clearPlanRevisionRetries(state.qid);
  void bootPlanRevisionReview(pi, ctx, hash);
  return next;
}

export async function carryRefinementsToDraft(ctx: PiCtx, state: QuestState): Promise<QuestState> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) return state;
  if (state.refinements.length === 0) return state;
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const items = state.refinements.map((refinement) => `- ${refinement}`).join("\n");
  const body = current.endsWith("\n") ? current : `${current}\n`;
  const updated = `${body}\n## Findings (pre-draft investigation)\n\n${items}\n`;
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  return updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, contentHash: hash },
    snapshotPending: true,
  });
}