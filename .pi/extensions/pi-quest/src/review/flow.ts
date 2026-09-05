// HIGH_LEVEL: #review and validation communication.
// HIGH_LEVEL: #review request — brief identifies qid, type, target, plan, evidence, criteria.
// HIGH_LEVEL: #review result — exactly one verdict, recorded in history.
// HIGH_LEVEL: #stale results — only the current target can change state.
// HIGH_LEVEL: #independent review contexts — fresh context per run.
// HIGH_LEVEL: #no direct mutation — the flow records evidence, transitions stay in domain.
import { createHash } from "node:crypto";
import { updateState } from "../app/store";
import { sendSteer } from "../app/interpreter";
import type { Pi, PiCtx } from "../hooks/events";
import type { Qid } from "../domain/qid";
import type { QuestState } from "../domain/quest";
import { recordReviewResult } from "../domain/quest";
import { createRunner, isReviewerAvailable } from "./runner";
import { parseReviewText, type ParsedReview } from "./verdicts";
import {
  cancelReview,
  isCurrentReview,
  settleReview,
  trackReview,
} from "./tracker";

export type FlowOutcome =
  | { status: "verdict"; review: ParsedReview; settled: boolean }
  | { status: "no-runner" }
  | { status: "aborted" }
  | { status: "failed"; detail: string };

export interface FlowArgs {
  pi: Pi;
  ctx: PiCtx;
  qid: Qid;
  target: string;
  prompt: string;
  runnerTool?: string;
}

export function reviewerAvailable(pi: Pi): boolean {
  return isReviewerAvailable(pi);
}

// One short notice per review target: the main agent must end its turn and
// wait — the verdict (which resumes work) arrives separately as a new turn.
// Never resumed from here: this path uses sendSteer, never sendWake.
const noticedReviews = new Set<string>();

export function reviewRunningNotice(qid: string): string {
  return `Review running for quest ${qid} — end your turn; the verdict arrives as a new turn.`;
}

export function shouldNoticeReview(qid: string, target: string): boolean {
  const key = `${qid}:${target}`;
  if (noticedReviews.has(key)) return false;
  noticedReviews.add(key);
  return true;
}

export function implementationFingerprint(state: QuestState): string {
  const stable = {
    qid: state.qid,
    objective: state.objective,
    refinements: state.refinements,
    amendments: state.amendments,
    setbacks: state.setbacks,
    children: state.children.map((c) => `${c.qid}:${c.status}`),
    exactNextAction: state.exactNextAction,
  };
  return createHash("sha256").update(JSON.stringify(stable), "utf8").digest("hex");
}

export async function runIsolatedReview(args: FlowArgs): Promise<FlowOutcome> {
  const { pi, ctx, qid, target } = args;
  const runnerTool = args.runnerTool ?? "subagent";
  if (!isReviewerAvailable(pi, runnerTool)) return { status: "no-runner" };
  const runner = createRunner({ pi, ctx, ownerRunId: qid, toolName: runnerTool });
  if (runner === null) return { status: "no-runner" };
  const controller = new AbortController();
  trackReview(qid, target, () => controller.abort());
  if (shouldNoticeReview(qid, target)) sendSteer(pi, reviewRunningNotice(qid));
  try {
    const launched = await runner.launch(args.prompt, controller.signal);
    const review = parseReviewText(launched.text);
    updateState((s) => recordReviewResult(s, review.verdict, target, review.findings));
    return { status: "verdict", review, settled: settleReview(qid, target) };
  } catch (err) {
    if (controller.signal.aborted) return { status: "aborted" };
    if (isCurrentReview(qid, target)) cancelReview(qid);
    return { status: "failed", detail: err instanceof Error ? err.message : String(err) };
  }
}
