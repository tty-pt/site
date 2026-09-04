// HIGH_LEVEL: #review and validation communication.
// HIGH_LEVEL: #review request — brief identifies qid, type, target, plan, evidence, criteria.
// HIGH_LEVEL: #review result — exactly one verdict, recorded in history.
// HIGH_LEVEL: #stale results — only the current target can change state.
// HIGH_LEVEL: #independent review contexts — fresh context per run.
// HIGH_LEVEL: #no direct mutation — the flow records evidence, transitions stay in domain.
import { createHash } from "node:crypto";
import { updateState } from "../app/store";
import type { Pi, PiCtx } from "../hooks/events";
import type { Qid } from "../domain/qid";
import type { QuestState, ReviewKind } from "../domain/quest";
import { recordReviewResult } from "../domain/quest";
import { createRunner, isReviewerAvailable } from "./runner";
import { parseReviewText, type ParsedReview } from "./verdicts";
import {
  cancelReview,
  isCurrentReview,
  noteReviewerSession,
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
  kind: ReviewKind;
  target: string;
  plan?: string;
  evidence: string[];
  criteria: string[];
  prompt: string;
}

export function reviewerAvailable(pi: Pi): boolean {
  return isReviewerAvailable(pi);
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
  const { pi, ctx, qid, kind, target } = args;
  if (!isReviewerAvailable(pi)) return { status: "no-runner" };
  const runner = createRunner({ pi, ctx, ownerRunId: qid });
  if (runner === null) return { status: "no-runner" };
  const controller = new AbortController();
  trackReview(qid, target, () => controller.abort());
  try {
    const launched = await runner.launch(
      {
        brief: {
          qid,
          kind,
          target,
          plan: args.plan,
          evidence: args.evidence,
          criteria: args.criteria,
        },
        prompt: args.prompt,
      },
      controller.signal,
      (child) => noteReviewerSession(qid, child),
    );
    const review = parseReviewText(launched.text);
    updateState((s) => recordReviewResult(s, review.verdict, target, review.findings));
    return { status: "verdict", review, settled: settleReview(qid, target) };
  } catch (err) {
    if (controller.signal.aborted) return { status: "aborted" };
    if (isCurrentReview(qid, target)) cancelReview(qid);
    return { status: "failed", detail: err instanceof Error ? err.message : String(err) };
  }
}
