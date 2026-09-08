// HIGH_LEVEL: #drafting — every content-changing save boots a fresh review.
// HIGH_LEVEL: #modes — PASS auto-promotes, FAIL returns findings, user "go" promotes.
// SPEC: B1.3 (supersede, thresholds, approval), B2 (go-override).
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer, sendWake } from "../app/interpreter";
import { DEFAULT_CONFIG, readQuestConfig, type QuestConfig } from "../config";
import type { ApprovedBy, QuestState } from "../domain/quest";
import { childDeviated, noteDraftFindings, promote, researchRecorded } from "../domain/quest";
import { draftPath } from "../domain/paths";
import type { Pi, PiCtx, ToolResultEvent } from "../hooks/events";
import { onSessionStart, onToolResult, onTurnEnd, onUserMessage } from "../hooks/events";
import { buildReviewPrompt } from "../review/prompts";
import { runIsolatedReview } from "../review/flow";
import { cancelReview, isCurrentReview, supersedeReviewThenBootFresh } from "../review/tracker";
import { noteDraftUpdated } from "../durability/status";
import { handleDraftEdit } from "./edits";
import {
  bumpReviewCount,
  draftProfileText,
  hashContent,
  meetsReviewThresholds,
  parseDraftSections,
  reviewMaterial,
} from "./plan-text";
export const GO_PATTERN = /^\s*(go|approve(?:d)?|lgtm|ship it)\s*[.!]*\s*$/i;

export const MAX_REVIEW_RETRIES = 3;
export const REVIEW_RETRY_BASE_MS = 4000;

// HIGH_LEVEL: #drafting — retries are capped and back off; the agent is only
// woken at the cap. The only paths out of drafting stay a verdict or a real
// live-user "go" — never the agent's own judgment.
const retryAttempts = new Map<string, number>();

export function draftReviewRetryCount(qid: string): number {
  return retryAttempts.get(qid) ?? 0;
}

export function clearDraftReviewRetry(qid: string): void {
  retryAttempts.delete(qid);
}

type RetryDispatcher = (delayMs: number, fn: () => void) => void;
let dispatchRetry: RetryDispatcher = (delayMs, fn) => {
  setTimeout(fn, delayMs);
};

// Test seam: swap the real timer for a recorder to assert retry scheduling
// deterministically without leaking pending timers.
export function setReviewRetryDispatcher(next: RetryDispatcher): RetryDispatcher {
  const previous = dispatchRetry;
  dispatchRetry = next;
  return previous;
}

async function readDraftFile(ctx: PiCtx, state: QuestState): Promise<string | null> {
  if (state.qid === null) return null;
  try {
    return await readFile(join(ctx.cwd, draftPath(state.qid)), "utf8");
  } catch {
    return null;
  }
}

const userPathSteered = new Set<string>();

// A launch that never ran a reviewer schedules a marker-bump retry with
// backoff, capped; the agent is only woken at the cap. The only ways out of
// drafting stay a completed review or a live user "go".
function noteReviewLaunchFailed(
  pi: Pi,
  ctx: PiCtx,
  config: QuestConfig,
  qid: string,
  target: string,
  detail: string,
): void {
  const attempts = retryAttempts.get(qid) ?? 0;
  if (attempts < MAX_REVIEW_RETRIES) {
    const attempt = attempts + 1;
    retryAttempts.set(qid, attempt);
    const delay = REVIEW_RETRY_BASE_MS * 2 ** (attempt - 1);
    dispatchRetry(delay, () => {
      void bumpReviewCountAndReboot(pi, ctx, config, attempt, target);
    });
    return;
  }
  retryAttempts.delete(qid);
  userPathSteered.add(`${qid}:${target}`);
  sendWake(pi, `Draft review failed ${MAX_REVIEW_RETRIES} times (last error: ${detail}). Revise the plan and save to boot a fresh review — promotion requires a reviewer PASS or a live user "go" reply.`);
}

export async function bootDraftReview(
  pi: Pi,
  ctx: PiCtx,
  target: string,
  config: QuestConfig = DEFAULT_CONFIG,
): Promise<void> {
  const state = getState();
  if (state.phase !== "drafting" || state.qid === null) return;
  const qid = state.qid;
  const content = await readDraftFile(ctx, state);
  if (content === null) return;
  const sections = parseDraftSections(content);
  // Material first: it diffs against the previously reviewed plan, so the
  // base below must still hold the old text at this point.
  const material = reviewMaterial(getState(), sections);
  const belowBar = !meetsReviewThresholds(sections, config.draftThresholds);
  updateState((s) => s.draft === null ? s : { ...s, draft: { ...s.draft, lastReviewedPlan: sections.plan } });
  const outcome = await runIsolatedReview({
    pi,
    ctx,
    qid,
    target,
    prompt: buildReviewPrompt("draft", qid, target, material, config.draftThresholds, belowBar),
    runnerTool: config.bindings.reviewRunner.tool,
    maxDurationMs: config.reviewMaxDurationMs,
    inactivityLimitMs: config.reviewInactivityMs,
  });
  if (outcome.status === "no-runner") {
    if (!userPathSteered.has(`${qid}:${target}`)) {
      userPathSteered.add(`${qid}:${target}`);
      // No reviewer subagent exists: only a live human "go" (real input) may
      // promote. A quest_ask_human default is absence, not approval — it must
      // never be treated as promotion authority.
      sendSteer(pi, `No reviewer available for ${qid}. Plan at ${draftPath(qid)}. Only a live user reply "go" promotes; a quest_ask_human default does not count. Keep revising to stay in drafting.`);
    }
    return;
  }
  if (outcome.status === "failed") {
    noteReviewLaunchFailed(pi, ctx, config, qid, target, outcome.detail);
    return;
  }
  if (outcome.status !== "verdict" || !outcome.settled) return;
  if (outcome.review.verdict === "PASS") {
    clearDraftReviewRetry(qid);
    if (!researchRecorded(getState(), sections.evidence.length)) {
      sendSteer(pi, `Reviewer PASS recorded for ${qid}, but promotion needs recorded research: no evidence, refinements, or setback evidence on file. Record research via quest_update_state, or a live user may reply "go".`);
      return;
    }
    updateState((s) => promote(s, "review"));
    emitNow(pi);
    const advisories = outcome.review.advisories.trim();
    sendWake(pi, `Quest ${qid} promoted to implementing (reviewer PASS). Proceed autonomously from the draft plan.${advisories === "" ? "" : ` Non-blocking advisories: ${advisories} Record adopted ones via amendment as you work.`}`);
    return;
  }
  updateState((s) => noteDraftFindings(s));
  clearDraftReviewRetry(qid);
  emitNow(pi);
  const verbatim = outcome.review.text.trim();
  const reviewExcerpt = verbatim === ""
    ? ""
    : `\n\nReview text (verbatim, budget-bounded):\n${verbatim}`;
  const profile = belowBar
    ? `\n\nDraft profile:\n${draftProfileText(sections, config.draftThresholds)}`
    : "";
  sendWake(pi, `Draft review FAIL (target ${target.slice(0, 12)}): ${outcome.review.findings} Revise the plan and save; saving boots a fresh review.${reviewExcerpt}${profile}`);
}

// A failed run's retry: rewrite the quest doc with the incremented review
// count, then boot the fresh review through the standard pipeline. Only
// fires while the file still holds the failed target — a superseding agent
// save cancels it (that save boots its own review) instead of clobbering.
export async function bumpReviewCountAndReboot(
  pi: Pi,
  ctx: PiCtx,
  config: QuestConfig,
  attempt: number,
  retryTarget: string,
): Promise<void> {
  const state = getState();
  if (state.phase !== "drafting" || state.qid === null) return;
  const qid = state.qid;
  if (retryAttempts.get(qid) !== attempt) return;
  const file = join(ctx.cwd, draftPath(qid));
  let content: string;
  try {
    content = await readFile(file, "utf8");
  } catch {
    return;
  }
  if (hashContent(content) !== retryTarget) {
    clearDraftReviewRetry(qid);
    return;
  }
  const bumped = bumpReviewCount(content, attempt);
  if (bumped === content) return;
  try {
    await writeFile(file, bumped, "utf8");
  } catch {
    return;
  }
  const hash = hashContent(bumped);
  updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, contentHash: hash },
    snapshotPending: true,
  });
  noteDraftUpdated(ctx);
  if (isCurrentReview(qid, hash)) return;
  supersedeReviewThenBootFresh(qid, hash, () => {
    void bootDraftReview(pi, ctx, hash, config);
  });
}

export async function maybeBootDraftReview(pi: Pi, ctx: PiCtx): Promise<void> {
  const state = getState();
  if (state.phase !== "drafting" || state.qid === null || state.draft === null) return;
  const content = await readDraftFile(ctx, state);
  if (content === null) return;
  const sections = parseDraftSections(content);
  if (sections.plan.length > 0 && !state.draft.planAuthored) {
    updateState((s) => s.draft === null ? s : {
      ...s,
      draft: { ...s.draft, planAuthored: true },
      snapshotPending: true,
    });
  }
  const config = await readQuestConfig(ctx.cwd);
  const target = hashContent(content);
  if (state.draft.approvedBy !== null && state.draft.contentHash === target) return;
  if (isCurrentReview(state.qid, target)) return;
  if (state.parentQid !== null && !childDeviated(getState())) return;
  supersedeReviewThenBootFresh(state.qid, target, () => {
    void bootDraftReview(pi, ctx, target, config);
  });
}

// Resumed on session start: a drafting quest whose current disk content was
// never reviewed boots a fresh review, so a quit mid-review loses nothing.
// Recorded verdicts (PASS or FAIL) are left alone; planless drafts have
// nothing to review yet. Delegates to the single save-path boot below.
export async function ensureDraftReview(pi: Pi, ctx: PiCtx): Promise<void> {
  const state = getState();
  if (state.phase !== "drafting" || state.qid === null || state.draft === null) return;
  const content = await readDraftFile(ctx, state);
  if (content === null) return;
  if (parseDraftSections(content).plan.length === 0) return;
  if (state.lastReview?.target === hashContent(content)) return;
  await maybeBootDraftReview(pi, ctx);
}

export function approveDraft(pi: Pi, qid: string, by: ApprovedBy): boolean {
  const state = getState();
  if (state.phase !== "drafting" || state.qid !== qid) return false;
  cancelReview(qid);
  clearDraftReviewRetry(qid);
  updateState((s) => promote(s, by));
  emitNow(pi);
  const how = by === "user" ? 'user "go"' : "reviewer PASS";
  sendSteer(pi, `Quest ${qid} promoted to implementing (${how}). Proceed autonomously from the draft plan.`);
  return true;
}

export function handleGoInput(pi: Pi, text: string): boolean {
  // "go" here is a LIVE user input event (wired in watchGoInput via
  // onUserMessage). It is never served from a tool result — a quest_ask_human
  // default is absence, not approval, and so can never promote a draft.
  if (!GO_PATTERN.test(text)) return false;
  const state = getState();
  if (state.phase !== "drafting" || state.qid === null) return false;
  return approveDraft(pi, state.qid, "user");
}

async function onWriteResult(pi: Pi, ctx: PiCtx, event: ToolResultEvent): Promise<void> {
  if (event.isError) return;
  if (event.toolName !== "edit" && event.toolName !== "write") return;
  const rawPath = event.input["path"];
  if (typeof rawPath !== "string") return;
  const state = getState();
  if (state.phase !== "drafting" || state.qid === null) return;
  const expected = draftPath(state.qid);
  if (!rawPath.endsWith(expected)) return;
  const diskPath = rawPath.startsWith("/") ? rawPath : join(ctx.cwd, rawPath);
  let content: string;
  try {
    content = await readFile(diskPath, "utf8");
  } catch {
    return;
  }
  if (!handleDraftEdit(expected, hashContent(content))) return;
  noteDraftUpdated(ctx);
  void maybeBootDraftReview(pi, ctx);
}

export function watchDraftEdits(pi: Pi): void {
  onToolResult(pi, (event, eventCtx) => {
    void onWriteResult(pi, eventCtx, event);
  });
}

// Catch-all for bypass edits (bash heredocs, sed, external tools): the gate
// polices tool calls, not the filesystem. One hash compare per turn-end
// while drafting; a mismatch boots a fresh review and blinks.
export async function onTurnEndCatchAll(pi: Pi, ctx: PiCtx): Promise<void> {
  try {
    const state = getState();
    if (state.phase !== "drafting" || state.qid === null || state.draft === null) return;
    const expected = draftPath(state.qid);
    let content: string;
    try {
      content = await readFile(join(ctx.cwd, expected), "utf8");
    } catch {
      return;
    }
    const hash = hashContent(content);
    if (hash === state.draft.contentHash) return;
    if (state.draft.contentHash === null) {
      // Scaffold absorption: record the baseline silently; creation blinked.
      updateState((s) => s.draft === null ? s : { ...s, draft: { ...s.draft, contentHash: hash } });
      return;
    }
    if (!handleDraftEdit(expected, hash)) return;
    noteDraftUpdated(ctx);
    void maybeBootDraftReview(pi, ctx);
  } catch {
    // Passive path: never break the agent.
  }
}

export function watchDraftFileCatchAll(pi: Pi): void {
  onTurnEnd(pi, (_event, eventCtx) => {
    void onTurnEndCatchAll(pi, eventCtx);
  });
}

export function watchResume(pi: Pi): void {
  onSessionStart(pi, (_event, eventCtx) => {
    void ensureDraftReview(pi, eventCtx);
  });
}

export function watchGoInput(pi: Pi): void {
  onUserMessage(pi, (text) => {
    handleGoInput(pi, text);
  });
}
