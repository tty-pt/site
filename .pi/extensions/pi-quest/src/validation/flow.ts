// HIGH_LEVEL: #validation — validator archives on PASS, demotes on FAIL.
// HIGH_LEVEL: #validator communication — plan, snapshot, amendments, evidence, criteria.
// SPEC: B1.5 (completion + slim archive).
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer, sendWake } from "../app/interpreter";
import type { QuestState } from "../domain/quest";
import { demoteToImplementing } from "../domain/quest";
import { draftPath } from "../domain/paths";
import type { Pi, PiCtx } from "../hooks/events";
import { buildReviewPrompt } from "../review/prompts";
import { isReviewerAvailable } from "../review/runner";
import { implementationFingerprint, runIsolatedReview } from "../review/flow";
import { readQuestConfig } from "../config";
import { hasInFlight } from "../review/tracker";
import { archiveActiveQuest } from "../surface/tools/archive";

export const CONFIRM_PATTERN = /^\s*confirm(?:ed)?\s*[.!]*$/i;

async function approvedPlan(ctx: PiCtx, state: QuestState): Promise<string> {
  if (state.qid === null) return "(none)";
  try {
    return await readFile(join(ctx.cwd, draftPath(state.qid)), "utf8");
  } catch {
    return "(draft file removed after promotion)";
  }
}

const announced = new Set<string>();

function announceOnce(pi: Pi, key: string, text: string): void {
  if (announced.has(key)) return;
  announced.add(key);
  sendSteer(pi, text);
}

function wakeOnce(pi: Pi, key: string, text: string): void {
  if (announced.has(key)) return;
  announced.add(key);
  sendWake(pi, text);
}

export async function ensureValidationFlow(pi: Pi, ctx: PiCtx): Promise<void> {
  const state = getState();
  if (state.phase !== "validating" || state.qid === null) return;
  const qid = state.qid;
  const target = implementationFingerprint(state);
  if (state.lastReview?.verdict === "PASS" && state.lastReview.target === target) {
    wakeOnce(pi, `accepted:${qid}:${target}`, `Validation PASS recorded for ${qid} — run quest_archive to complete the quest.`);
    return;
  }
  if (hasInFlight(qid)) return;
  const config = await readQuestConfig(ctx.cwd);
  const plan = await approvedPlan(ctx, state);
  const evidence = [
    ...state.refinements,
    ...state.setbacks.map((s) => `${s.reason} — ${s.evidence.join("; ")}`),
    ...state.children.map((c) => `child ${c.qid} (${c.status}): ${c.findings ?? "no findings yet"}`),
  ];
  const outcome = await runIsolatedReview({
    pi,
    ctx,
    qid,
    target,
    runnerTool: config.bindings.reviewRunner.tool,
    prompt: buildReviewPrompt("validation", qid, target, {
      objective: state.pendingRootRequest ?? state.objective,
      plan,
      evidence,
      amendments: state.amendments.map((a) => `${a.change} (${a.reasons})`),
      implementationSummary: `${state.exactNextAction} Children: ${state.children.map((c) => `${c.qid}=${c.status}`).join(", ") || "none"}.`,
    }),
  });
  if (outcome.status === "no-runner") {
    announceOnce(pi, `userpath:${qid}:${target}`, `No validator available. The approved plan and implementation summary are above — reply CONFIRM to accept completion, or keep working.`);
    return;
  }
  if (outcome.status === "failed") {
    wakeOnce(pi, `failed:${qid}:${target}`, `Validation failed to run (${outcome.detail}). Claim completion again to retry, or reply CONFIRM only when no validator is available.`);
    return;
  }
  if (outcome.status !== "verdict" || !outcome.settled) return;
  if (outcome.review.verdict === "PASS") {
    wakeOnce(pi, `accepted:${qid}:${target}`, `Validation PASS for ${qid} — run quest_archive to complete the quest.`);
    return;
  }
  updateState((s) => demoteToImplementing(s));
  emitNow(pi);
  sendWake(pi, `Validation FAIL (target ${target.slice(0, 12)}): ${outcome.review.findings} Address the findings, then claim completion again.`);
}

export async function handleConfirmInput(pi: Pi, ctx: PiCtx, text: string): Promise<boolean> {
  if (!CONFIRM_PATTERN.test(text)) return false;
  const state = getState();
  if (state.phase !== "validating" || state.qid === null) return false;
  if (hasInFlight(state.qid)) return false;
  const target = implementationFingerprint(state);
  if (state.lastReview?.verdict === "PASS" && state.lastReview.target === target) return false;
  const config = await readQuestConfig(ctx.cwd);
  if (isReviewerAvailable(pi, config.bindings.reviewRunner.tool)) return false;
  void archiveActiveQuest(pi, ctx, "COMPLETED", "Accepted on user confirmation (no validator available).");
  return true;
}
