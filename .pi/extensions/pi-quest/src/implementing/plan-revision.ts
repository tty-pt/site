// HIGH_LEVEL: #plan revision — revision boots re-review; PASS adopts, FAIL restores.
// The revision is a new reviewable target reusing the draft-review machinery
// (full plan + diff + prior verdict); the tracker marks it in flight so the
// gate tightens like drafting until the verdict lands.
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer, sendWake } from "../app/interpreter";
import { DEFAULT_CONFIG, readQuestConfig, type QuestConfig } from "../config";
import type { QuestState } from "../domain/quest";
import { approvePlanRevision, recordPlanRevision, revertPlanRevision } from "../domain/plan-revision";
import { draftPath } from "../domain/paths";
import type { Pi, PiCtx } from "../hooks/events";
import { onSessionStart, onTurnEnd, onUserMessage } from "../hooks/events";
import { buildReviewPrompt } from "../review/prompts";
import { runIsolatedReview, shouldNoticeReview } from "../review/flow";
import { hasInFlight, isCurrentReview, supersedeReviewThenBootFresh } from "../review/tracker";
import { hashContent, parseDraftSections, reviewMaterial, splicePlanSection } from "../drafting/reviews";
import { noteDraftUpdated } from "../durability/status";

export const APPROVE_PATTERN = /^\s*approve(?:\s+revision)?\s*[.!]*\s*$/i;

const REVISION_NOTE = "Mid-implementation plan revision: the remaining steps changed after reality contradicted the approved plan.";

async function readPlanFile(ctx: PiCtx, state: QuestState): Promise<string | null> {
  if (state.qid === null) return null;
  try {
    return await readFile(join(ctx.cwd, draftPath(state.qid)), "utf8");
  } catch {
    return null;
  }
}

export async function bootPlanRevisionReview(
  pi: Pi,
  ctx: PiCtx,
  target: string,
  config: QuestConfig = DEFAULT_CONFIG,
): Promise<void> {
  const state = getState();
  if (state.phase !== "implementing" || state.qid === null || state.draft === null) return;
  if (isCurrentReview(state.qid, target)) return;
  const qid = state.qid;
  const content = await readPlanFile(ctx, state);
  if (content === null) return;
  const sections = parseDraftSections(content);
  if (sections.plan.length === 0) return;
  const material = {
    ...reviewMaterial(getState(), sections),
    revisionNote: REVISION_NOTE,
  };
  const outcome = await runIsolatedReview({
    pi,
    ctx,
    qid,
    target,
    prompt: buildReviewPrompt("draft", qid, target, material, config.draftThresholds),
    runnerTool: config.bindings.reviewRunner.tool,
  });
  if (outcome.status === "no-runner") {
    if (shouldNoticeReview(qid, target)) {
      sendSteer(pi, `No reviewer available for the plan revision on ${qid}. The staged revision is in the draft file — reply APPROVE to adopt it on your judgment, or revise again.`);
    }
    return;
  }
  if (outcome.status === "failed") {
    sendWake(pi, `Plan-revision review failed to run (${outcome.detail}). The previous plan still stands; revise again to retry.`);
    return;
  }
  if (outcome.status !== "verdict" || !outcome.settled) return;
  if (outcome.review.verdict === "PASS") {
    updateState((s) => approvePlanRevision(s, target, sections.plan));
    emitNow(pi);
    const advisories = outcome.review.advisories.trim();
    sendWake(pi, `Plan revision adopted for ${qid} (reviewer PASS). Proceed against the revised plan; validation re-binds to it.${advisories === "" ? "" : ` Non-blocking advisories: ${advisories} Record adopted ones via amendment as you work.`}`);
    return;
  }
  await restoreApprovedPlan(pi, ctx);
  emitNow(pi);
  sendWake(pi, `Plan revision FAIL (target ${target.slice(0, 12)}): ${outcome.review.findings} The previous plan is restored. Revise again via planRevision, or record an amendment.`);
}

// FAIL restores the last approved plan text over the rejected revision and
// rewinds the content binding; history stays append-only.
async function restoreApprovedPlan(pi: Pi, ctx: PiCtx): Promise<void> {
  void pi;
  const state = getState();
  if (state.phase !== "implementing" || state.qid === null || state.draft === null) return;
  const approved = state.draft.lastReviewedPlan
    ?? [...(state.draft.planRevisions ?? [])].reverse().find((r) => r.plan !== "")?.plan
    ?? null;
  if (approved === null) return;
  const content = await readPlanFile(ctx, state);
  if (content === null) return;
  const restored = splicePlanSection(content, approved);
  if (restored !== content) {
    await writeFile(join(ctx.cwd, draftPath(state.qid)), restored, "utf8");
  }
  updateState((s) => revertPlanRevision(s));
}

// Unreviewed disk edits during implementing (the gate polices tool calls,
// not the filesystem) become revisions: the approved binding is untouched
// until a reviewer PASSes the new content.
export async function maybeBootPlanRevisionReview(pi: Pi, ctx: PiCtx, note: string): Promise<string> {
  const state = getState();
  if (state.phase !== "implementing" || state.qid === null || state.draft === null) return "idle";
  const content = await readPlanFile(ctx, state);
  if (content === null) return "idle";
  const sections = parseDraftSections(content);
  if (sections.plan.length === 0) return "idle";
  const hash = hashContent(content);
  if (hash === state.draft.contentHash) return "in-sync";
  if (isCurrentReview(state.qid, hash)) return "in-flight";
  if (hash === state.draft.approvedPlanHash) {
    updateState((s) => s.draft === null ? s : { ...s, draft: { ...s.draft, contentHash: hash } });
    return "in-sync";
  }
  const qid = state.qid;
  const previousHash = state.draft.contentHash;
  const previousPlan = state.draft.lastReviewedPlan ?? "";
  updateState((s) => recordPlanRevision(s, previousHash, hash, note, previousPlan));
  noteDraftUpdated(ctx);
  const config = await readQuestConfig(ctx.cwd);
  supersedeReviewThenBootFresh(qid, hash, () => {
    void bootPlanRevisionReview(pi, ctx, hash, config);
  });
  return "booted";
}

// No-reviewer escape hatch: the user adopts the staged revision on their
// judgment. Agents cannot self-approve — this only fires on user input.
export async function handleApproveInput(pi: Pi, ctx: PiCtx, text: string): Promise<boolean> {
  if (!APPROVE_PATTERN.test(text)) return false;
  const state = getState();
  if (state.phase !== "implementing" || state.qid === null || state.draft === null) return false;
  if (state.draft.contentHash === null || state.draft.contentHash === state.draft.approvedPlanHash) return false;
  if (hasInFlight(state.qid)) return false;
  const content = await readPlanFile(ctx, state);
  if (content === null) return false;
  const sections = parseDraftSections(content);
  if (sections.plan.length === 0) return false;
  updateState((s) => approvePlanRevision(s, s.draft?.contentHash ?? "", sections.plan));
  emitNow(pi);
  sendSteer(pi, `Plan revision adopted for ${state.qid} on user approval. Proceed against the revised plan; validation re-binds to it.`);
  return true;
}

export function watchApproveInput(pi: Pi): void {
  onUserMessage(pi, (text, eventCtx) => {
    void handleApproveInput(pi, eventCtx, text);
  });
}

export function watchPlanRevisionCatchAll(pi: Pi): void {
  onTurnEnd(pi, (_event, eventCtx) => {
    void maybeBootPlanRevisionReview(pi, eventCtx, "unreviewed draft-file edit");
  });
}

export function watchPlanRevisionResume(pi: Pi): void {
  onSessionStart(pi, (_event, eventCtx) => {
    void maybeBootPlanRevisionReview(pi, eventCtx, "staged revision resumed");
  });
}
