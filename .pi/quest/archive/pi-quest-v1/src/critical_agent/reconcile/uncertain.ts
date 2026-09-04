import { QuestErrorCode } from "../../constants.ts";
import { logCriticalReviewTransition } from "../../logging.ts";
import {
  createAgentObligation,
  queueAgentObligation,
} from "../../obligations.ts";
import { sendInternalAgentMessage } from "../../messaging.ts";
import { persist } from "../../persistence.ts";
import { isReviewSnapshotCurrent } from "../snapshot.ts";
import { ExtensionAPI, ExtensionContext, ReviewSnapshot } from "../../types.ts";

export function handleUncertainVerdict(
  snapshot: ReviewSnapshot,
  parsedResult: any,
  targetState: any,
  correlationId: string,
  reviewState: any,
  isPlanReviewKind: boolean,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): { success: boolean; available: boolean; review: any } {
  const slug = snapshot.questId;
  const questId = targetState.questId || slug;
  const sessionId = snapshot.sessionId;
  const uncertainEventType = isPlanReviewKind
    ? "PLAN_REVIEW_UNCERTAIN"
    : "CRITICAL_REVIEW_UNCERTAIN";
  logCriticalReviewTransition(
    uncertainEventType,
    `critical review uncertain: ${
      parsedResult.findings.map((f: any) => f.issue).join("; ") ||
      "missing evidence"
    }`,
    {
      quest: slug,
      questId,
      sessionId,
      reviewId: correlationId,
      childSessionId: parsedResult.childSessionId,
      parentSessionId: sessionId,
      reviewKind: snapshot.reviewKind,
      severity: parsedResult.severity,
      verdict: "UNCERTAIN",
      durationMs: parsedResult.durationMs,
      reviewedVersion: snapshot.planVersion,
    },
  );
  logCriticalReviewTransition(
    "REMEDIATION_REQUIRED",
    `targeted investigation required: ${
      parsedResult.requiredActions.join("; ") || "verify missing evidence"
    }`,
    {
      quest: slug,
      questId,
      sessionId,
      reviewId: correlationId,
      childSessionId: parsedResult.childSessionId,
      parentSessionId: sessionId,
      reviewKind: snapshot.reviewKind,
      severity: parsedResult.severity,
      requiredAction: parsedResult.requiredActions.join("; "),
      reviewedVersion: snapshot.planVersion,
    },
  );

  if (isPlanReviewKind) targetState.lastPlanReviewApproval = null;

  const missingEvidenceSummary = parsedResult.findings.map((f: any) =>
    `- ${f.issue}${f.evidence ? `\n  Missing Evidence: ${f.evidence}` : ""}`
  ).join("\n");
  const actionsSummary = parsedResult.requiredActions.map((a: any) => `- ${a}`)
    .join("\n");
  const uncertainMsg = isPlanReviewKind
    ? `[Quest Journal] ADVERSARIAL PLAN REVIEW UNCERTAIN\n\nMissing evidence:\n${
      missingEvidenceSummary || "(Uncertain evidence)"
    }\n\nRequired action:\n${
      actionsSummary ||
      "Perform targeted read/search investigation to establish conclusive evidence, then update the plan and retry plan review."
    }\n\nThe plan draft is NOT approved until conclusive evidence is established.`
    : `[Quest Journal] CRITICAL REVIEW UNCERTAIN\n\nMissing evidence:\n${
      missingEvidenceSummary || "(Uncertain evidence)"
    }\n\nRequired action:\n${
      actionsSummary ||
      "Perform targeted read/search investigation to establish conclusive evidence."
    }`;

  const uncObl = createAgentObligation(targetState, {
    kind: "critical_review",
    code: isPlanReviewKind
      ? QuestErrorCode.PLAN_REVIEW_REQUIRED
      : QuestErrorCode.CRITICAL_REVIEW_FAILED,
    message: uncertainMsg,
    deliverAs: "followUp",
    requiredNextAction: actionsSummary ||
      "Perform targeted read/search investigation to establish conclusive evidence.",
    correlationId,
    isCurrent: (s) => isReviewSnapshotCurrent(snapshot, s).current,
  });
  queueAgentObligation(targetState, uncObl);
  sendInternalAgentMessage(
    pi,
    uncertainMsg,
    "followUp",
    isPlanReviewKind ? "plan_review_uncertain" : "critical_review_uncertain",
    correlationId,
    { triggerTurn: true, display: true },
  );
  targetState.inCriticalReview = false;
  if (targetState.awaitingReview?.reviewId === correlationId) {
    targetState.awaitingReview = null;
  }
  persist(pi, ctx);
  return { success: false, available: true, review: reviewState };
}
