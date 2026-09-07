// HIGH_LEVEL: #drafting — every content-changing save boots a fresh review.
// HIGH_LEVEL: #modes — PASS auto-promotes, FAIL returns findings, user "go" promotes.
// SPEC: B1.3 (supersede, thresholds, approval), B2 (go-override).
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { join } from "node:path";
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer, sendWake } from "../app/interpreter";
import { DEFAULT_CONFIG, readQuestConfig, type QuestConfig } from "../config";
import type { ApprovedBy, QuestState } from "../domain/quest";
import { childDeviated, noteDraftFindings, promote, researchRecorded } from "../domain/quest";
import { draftPath } from "../domain/paths";
import type { Pi, PiCtx, ToolResultEvent } from "../hooks/events";
import { onSessionStart, onToolResult, onTurnEnd, onUserMessage } from "../hooks/events";
import { buildReviewPrompt, type ReviewMaterial } from "../review/prompts";
import { runIsolatedReview } from "../review/flow";
import { cancelReview, isCurrentReview, supersedeReviewThenBootFresh } from "../review/tracker";
import { noteDraftUpdated } from "../durability/status";
import { handleDraftEdit } from "./edits";
import { diffPlans } from "./plan-diff";
export const GO_PATTERN = /^\s*(go|approve(?:d)?|lgtm|ship it)\s*[.!]*\s*$/i;

export interface DraftSections {
  requirements: string[];
  evidence: string[];
  plan: string;
}

export function hashContent(content: string): string {
  return createHash("sha256").update(content, "utf8").digest("hex");
}

export function splicePlanSection(text: string, plan: string): string {
  const lines = text.split("\n");
  const start = lines.findIndex((line) => {
    const header = line.match(/^##\s+(.+?)\s*$/i);
    return header !== null && header[1].toLowerCase().includes("implementation plan");
  });
  if (start === -1) {
    const body = text.endsWith("\n") ? text : `${text}\n`;
    return `${body}\n## Implementation Plan\n\n${plan.trim()}\n`;
  }
  let end = lines.length;
  for (let i = start + 1; i < lines.length; i += 1) {
    if (/^##\s+.+?\s*$/.test(lines[i])) {
      end = i;
      break;
    }
  }
  return [...lines.slice(0, start + 1), "", plan.trim(), "", ...lines.slice(end)].join("\n");
}

export function parseDraftSections(text: string): DraftSections {
  const requirements: string[] = [];
  const evidence: string[] = [];
  const planLines: string[] = [];
  let section: "requirements" | "evidence" | "plan" | null = null;
  for (const line of text.split(/\r?\n/)) {
    const header = line.match(/^##\s+(.+?)\s*$/);
    if (header) {
      const name = header[1].toLowerCase();
      if (name.includes("requirement")) section = "requirements";
      else if (name.includes("evidence")) section = "evidence";
      else if (name.includes("implementation plan")) section = "plan";
      else section = null;
      continue;
    }
    if (section === "plan") {
      planLines.push(line);
      continue;
    }
    const bullet = line.match(/^\s*(?:[-*+]|\d+[.)])\s+(.+?)\s*$/);
    if (bullet && (section === "requirements" || section === "evidence")) {
      if (section === "requirements") requirements.push(bullet[1]);
      else evidence.push(bullet[1]);
    }
  }
  return { requirements, evidence, plan: planLines.join("\n").trim() };
}

export function meetsReviewThresholds(
  sections: DraftSections,
  thresholds = DEFAULT_CONFIG.draftThresholds,
): boolean {
  const req = sections.requirements.length;
  const ev = sections.evidence.length;
  const counts = req >= thresholds.requirements || (req >= 1 && ev >= thresholds.evidence);
  return counts && sections.plan.length > 0;
}

async function readDraftFile(ctx: PiCtx, state: QuestState): Promise<string | null> {
  if (state.qid === null) return null;
  try {
    return await readFile(join(ctx.cwd, draftPath(state.qid)), "utf8");
  } catch {
    return null;
  }
}

// Exported for tests: the re-review brief must diff against the previously
// reviewed plan, never against the plan being sent.
export function reviewMaterial(state: QuestState, sections: DraftSections): ReviewMaterial {
  const openRebuttal = [...state.reviewDialogue].reverse().find((d) => d.verdictAfter === undefined);
  const base: ReviewMaterial = {
    objective: state.pendingRootRequest ?? state.objective,
    plan: sections.plan,
    evidence: sections.evidence,
    amendments: state.amendments.map((a) => `${a.change} (${a.reasons})`),
    rebuttal: openRebuttal?.implementerRebuttal,
  };
  const last = state.lastReview;
  const previous = state.draft?.lastReviewedPlan;
  if (last === null || previous === undefined || previous === null) return base;
  // Prior verdict always travels: an evidence-only revision answers the last
  // findings even when the plan itself is unchanged (then diffPlans is null).
  const continued: ReviewMaterial = {
    ...base,
    previousVerdict: last.verdict,
    previousFindings: last.findings,
  };
  const planDiff = diffPlans(previous, sections.plan);
  if (planDiff === null) return continued;
  return { ...continued, planDiff };
}

const userPathSteered = new Set<string>();

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
  updateState((s) => s.draft === null ? s : { ...s, draft: { ...s.draft, lastReviewedPlan: sections.plan } });
  const outcome = await runIsolatedReview({
    pi,
    ctx,
    qid,
    target,
    prompt: buildReviewPrompt("draft", qid, target, material, config.draftThresholds),
    runnerTool: config.bindings.reviewRunner.tool,
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
    sendWake(pi, `Draft review failed to run (${outcome.detail}). Reply "go" to proceed on your judgment, or revise and save to retry.`);
    return;
  }
  if (outcome.status !== "verdict" || !outcome.settled) return;
  if (outcome.review.verdict === "PASS") {
    if (!researchRecorded(getState(), sections.evidence.length)) {
      sendSteer(pi, `Reviewer PASS recorded for ${qid}, but promotion needs recorded research: no evidence, refinements, or setback evidence on file. Record research via quest_update_state, or reply "go" to proceed on your judgment.`);
      return;
    }
    updateState((s) => promote(s, "review"));
    emitNow(pi);
    const advisories = outcome.review.advisories.trim();
    sendWake(pi, `Quest ${qid} promoted to implementing (reviewer PASS). Proceed autonomously from the draft plan.${advisories === "" ? "" : ` Non-blocking advisories: ${advisories} Record adopted ones via amendment as you work.`}`);
    return;
  }
  updateState((s) => noteDraftFindings(s));
  emitNow(pi);
  sendWake(pi, `Draft review FAIL (target ${target.slice(0, 12)}): ${outcome.review.findings} Revise the plan and save; saving boots a fresh review. Or reply "go" to proceed on your judgment.`);
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
