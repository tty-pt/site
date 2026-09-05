// HIGH_LEVEL: #drafting — every content-changing save boots a fresh review.
// HIGH_LEVEL: #modes — PASS auto-promotes, FAIL returns findings, user "go" promotes.
// SPEC: B1.3 (supersede, thresholds, approval), B2 (go-override).
import { readFile } from "node:fs/promises";
import { createHash } from "node:crypto";
import { join } from "node:path";
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer } from "../app/interpreter";
import { DEFAULT_CONFIG, readQuestConfig, type QuestConfig } from "../config";
import type { ApprovedBy, QuestState } from "../domain/quest";
import { childDeviated, noteDraftFindings, promote, researchRecorded } from "../domain/quest";
import { draftPath } from "../domain/paths";
import type { Pi, PiCtx, ToolResultEvent } from "../hooks/events";
import { onToolResult, onUserMessage } from "../hooks/events";
import { buildReviewPrompt, type ReviewMaterial } from "../review/prompts";
import { runIsolatedReview } from "../review/flow";
import { cancelReview, isCurrentReview, supersedeReviewThenBootFresh } from "../review/tracker";
import { handleDraftEdit } from "./edits";

export const GO_PATTERN = /^\s*(go|approve(?:d)?|lgtm|ship it)\s*[.!]*\s*$/i;

export interface DraftSections {
  requirements: string[];
  evidence: string[];
  plan: string;
}

export function hashContent(content: string): string {
  return createHash("sha256").update(content, "utf8").digest("hex");
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
    const bullet = line.match(/^\s*[-*]\s+(.+?)\s*$/);
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

function reviewMaterial(state: QuestState, sections: DraftSections): ReviewMaterial {
  const openRebuttal = [...state.reviewDialogue].reverse().find((d) => d.verdictAfter === undefined);
  return {
    objective: state.pendingRootRequest ?? state.objective,
    plan: sections.plan,
    evidence: sections.evidence,
    amendments: state.amendments.map((a) => `${a.change} (${a.reasons})`),
    rebuttal: openRebuttal?.implementerRebuttal,
  };
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
  const material = reviewMaterial(getState(), sections);
  const outcome = await runIsolatedReview({
    pi,
    ctx,
    qid,
    kind: "draft",
    target,
    plan: sections.plan,
    evidence: sections.evidence,
    criteria: ["plan addresses the recorded request", "research sufficient", "no reviewer-preference blocks"],
    prompt: buildReviewPrompt("draft", qid, target, material),
    runnerTool: config.bindings.reviewRunner.tool,
  });
  if (outcome.status === "no-runner") {
    if (!userPathSteered.has(`${qid}:${target}`)) {
      userPathSteered.add(`${qid}:${target}`);
      sendSteer(pi, `No reviewer available. Draft plan for ${qid}:\n${sections.plan}\nReply "go" to promote to implementing, or keep revising.`);
    }
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
    const next = getState().exactNextAction;
    sendSteer(pi, `Quest ${qid} promoted to implementing (reviewer PASS). Proceed autonomously from the draft plan. ${next}`);
    return;
  }
  updateState((s) => noteDraftFindings(s));
  emitNow(pi);
  sendSteer(pi, `Draft review FAIL (target ${target.slice(0, 12)}): ${outcome.review.findings} Revise the plan and save; saving boots a fresh review. Or reply "go" to proceed on your judgment.`);
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
  if (!meetsReviewThresholds(sections, config.draftThresholds)) return;
  const target = hashContent(content);
  if (state.draft.approvedBy !== null && state.draft.contentHash === target) return;
  if (isCurrentReview(state.qid, target)) return;
  if (state.parentQid !== null && !childDeviated(getState())) return;
  supersedeReviewThenBootFresh(state.qid, target, () => {
    void bootDraftReview(pi, ctx, target, config);
  });
}

export function approveDraft(pi: Pi, qid: string, by: ApprovedBy): boolean {
  const state = getState();
  if (state.phase !== "drafting" || state.qid !== qid) return false;
  cancelReview(qid);
  updateState((s) => promote(s, by));
  emitNow(pi);
  const next = getState().exactNextAction;
  const how = by === "user" ? 'user "go"' : "reviewer PASS";
  sendSteer(pi, `Quest ${qid} promoted to implementing (${how}). Proceed autonomously from the draft plan. ${next}`);
  return true;
}

export function handleGoInput(pi: Pi, text: string): boolean {
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
  void maybeBootDraftReview(pi, ctx);
}

export function watchDraftEdits(pi: Pi): void {
  onToolResult(pi, (event, eventCtx) => {
    void onWriteResult(pi, eventCtx, event);
  });
}

export function watchGoInput(pi: Pi): void {
  onUserMessage(pi, (text) => {
    handleGoInput(pi, text);
  });
}
