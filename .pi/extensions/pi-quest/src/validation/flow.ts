// HIGH_LEVEL: #validation — validator archives on PASS, demotes on FAIL.
// HIGH_LEVEL: #validator communication — plan, snapshot, amendments, evidence, criteria.
// SPEC: B1.5 (completion + slim archive).
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer, sendWake } from "../app/interpreter";
import type { QuestState } from "../domain/quest";
import { demoteToImplementing, recordReviewResult } from "../domain/quest";
import { draftPath } from "../domain/paths";
import type { Pi, PiCtx } from "../hooks/events";
import { buildReviewPrompt } from "../review/prompts";
import { isReviewerAvailable } from "../review/runner";
import { implementationFingerprint, runIsolatedReview } from "../review/flow";
import type { ParsedReview } from "../review/verdicts";
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

// HIGH_LEVEL: #plan revision — the validator judges every plan move, so the
// brief carries the append-only revision record: note, superseded hash, and
// prior text truncated to a bounded window with the cut marked.
const REVISION_PLAN_CHARS = 500;

export function formatRevisionHistory(state: QuestState): string[] {
  return (state.draft?.planRevisions ?? []).map((r) => {
    const hash = r.hash === null || r.hash === "" ? "initial plan" : `superseded ${r.hash.slice(0, 12)}`;
    const when = new Date(r.at).toISOString().slice(0, 10);
    const prior = r.plan.length > REVISION_PLAN_CHARS
      ? `${r.plan.slice(0, REVISION_PLAN_CHARS)}… (truncated)`
      : r.plan;
    return `- ${r.note} (${hash}, ${when}): ${prior}`;
  });
}

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

// A PASS verdict concludes the quest: the archive summary carries the
// verdict's counts and advisories so nothing the validator said is lost
// when nobody archives manually.
const CONCLUSION_ADVISORY_CHARS = 300;

export function buildConclusionSummary(state: QuestState, target: string, review: ParsedReview): string {
  const advisories = review.advisories.trim();
  const noted = advisories === ""
    ? "(no advisories)"
    : advisories.length > CONCLUSION_ADVISORY_CHARS
    ? `${advisories.slice(0, CONCLUSION_ADVISORY_CHARS)}… (truncated)`
    : advisories;
  return `Validation PASS (${target.slice(0, 12)}). ${state.amendments.length} amendment(s), ${state.setbacks.length} setback(s). Advisories: ${noted}`;
}

export async function concludeValidationPass(
  pi: Pi,
  ctx: PiCtx,
  qid: string,
  target: string,
  review: ParsedReview,
  autoArchive: boolean,
): Promise<{ archived: boolean; zipPath: string | null }> {
  if (!autoArchive) return { archived: false, zipPath: null };
  // The manual path re-checks currency at archive time; conclude must too:
  // work that moved after the verdict needs a fresh validation, not an archive.
  if (implementationFingerprint(getState()) !== target) {
    sendSteer(pi, `Quest ${qid} changed during validation — conclusion skipped; a fresh validation will boot against the current work.`);
    return { archived: false, zipPath: null };
  }
  const done = await archiveActiveQuest(pi, ctx, "COMPLETED", buildConclusionSummary(getState(), target, review));
  wakeOnce(pi, `concluded:${qid}:${target}`, `Quest ${qid} concluded and archived as completed (${done.zipPath}).`);
  return { archived: true, zipPath: done.zipPath };
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
      planRevisionHistory: formatRevisionHistory(state),
      implementationSummary: `${state.exactNextAction} Children: ${state.children.map((c) => `${c.qid}=${c.status}`).join(", ") || "none"}.`,
    }),
  });
  if (outcome.status === "no-runner") {
    announceOnce(pi, `userpath:${qid}:${target}`, `No validator available. Only the user can accept completion by replying CONFIRM — your own CONFIRM text does nothing. Otherwise keep working (continueWork:true returns to implementing).`);
    return;
  }
  if (outcome.status === "failed") {
    wakeOnce(pi, `failed:${qid}:${target}`, `Validation failed to run (${outcome.detail}). Claim completion again to retry, keep working (continueWork:true), or hold for the user to reply CONFIRM.`);
    return;
  }
  if (outcome.status !== "verdict" || !outcome.settled) return;
  if (outcome.review.verdict === "PASS") {
    const concluded = await concludeValidationPass(pi, ctx, qid, target, outcome.review, config.autoArchive);
    if (!concluded.archived) {
      const advisories = outcome.review.advisories.trim();
      wakeOnce(pi, `accepted:${qid}:${target}`, `Validation PASS for ${qid} — run quest_archive to complete the quest.${advisories === "" ? "" : ` Non-blocking advisories: ${advisories} Record adopted ones via amendment as you work.`}`);
    }
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
  // User acceptance stands in for the missing validator verdict: record it
  // as the current PASS so the COMPLETED archive gate sees honest state.
  updateState((s) => recordReviewResult(s, "PASS", target, "Accepted on user confirmation (no validator available)."));
  emitNow(pi);
  await archiveActiveQuest(pi, ctx, "COMPLETED", "Accepted on user confirmation (no validator available).");
  return true;
}
