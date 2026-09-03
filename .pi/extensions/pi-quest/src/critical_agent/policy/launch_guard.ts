import { logCriticalReviewTransition, logEvent } from "../../logging.ts";
import { sendInternalAgentMessage } from "../../messaging.ts";
import { getCustomSubagentRunner } from "../pi_adapter.ts";
import {
  buildReviewBoundaryKey,
  canLaunchReview,
  setPendingReview,
} from "../tracker.ts";

export function checkLaunchGuard(
  slug: string,
  kind: any,
  targetState: any,
  options: any,
  currentPlanVersion: number,
  currentHash: string | null,
  currentSaveCount: number,
): { blocked: boolean; response?: any } {
  if (!options.force) {
    const launchCheck = canLaunchReview(
      slug,
      kind,
      currentPlanVersion,
      currentHash,
      targetState,
    );
    if (!launchCheck.allowed) {
      setPendingReview(slug, {
        questSlug: slug,
        kind,
        planVersion: currentPlanVersion,
        stateHash: currentHash,
        boundaryKey: options.boundaryKey ||
          targetState.lastPlanReviewBoundaryKey || null,
        saveCount: currentSaveCount,
        requestedAt: Date.now(),
        rebuttal: options.rebuttal,
        model: options.model,
        timeoutMs: options.timeoutMs,
        force: options.force,
      });
      return {
        blocked: true,
        response: {
          success: true,
          available: true,
          inProgress: true,
          skipped: true,
          error: launchCheck.reason,
        },
      };
    }
  }
  return { blocked: false };
}

export function checkAttemptLimit(
  slug: string,
  kind: any,
  targetState: any,
  options: any,
  correlationId: string,
  sessionId: string,
  questId: string,
  currentPlanVersion: number,
  currentHash: string | null,
  currentSaveCount: number,
  pi: any,
): { limited: boolean; response?: any } {
  const attemptKey = buildReviewBoundaryKey(
    slug,
    kind,
    currentPlanVersion,
    currentHash,
    currentSaveCount,
  );
  if (!targetState.criticalReviewAttempts) {
    targetState.criticalReviewAttempts = {};
  }
  const attempts = (targetState.criticalReviewAttempts[attemptKey] || 0) + 1;
  targetState.criticalReviewAttempts[attemptKey] = attempts;
  logEvent(
    "ATTEMPT_INCREMENTED",
    `attempt incremented (attempts=${attempts})`,
    {
      quest: slug,
      questId,
      sessionId,
      reviewId: correlationId,
      reviewKind: kind,
      attemptKey,
      attempts,
      planVersion: currentPlanVersion,
      stateHash: currentHash,
      boundaryKey: (
        options.boundaryKey ||
        targetState.lastPlanReviewBoundaryKey ||
        attemptKey
      )?.slice(0, 8),
      saveHash: (currentHash || "").slice(0, 8),
      saveCount: currentSaveCount,
    },
  );
  if (!options.force && attempts > 3) {
    const isPlanRev = kind === "plan_review";
    const eventType = isPlanRev
      ? "PLAN_REVIEW_FAILED"
      : "CRITICAL_REVIEW_FAILED";
    logCriticalReviewTransition(
      eventType,
      `critical review attempt limit reached (attempts=${attempts})`,
      {
        quest: slug,
        questId,
        sessionId,
        reviewId: correlationId,
        parentSessionId: sessionId,
        reviewKind: kind,
        severity: "CRITICAL",
        reason: "Review loop bound exceeded for state version",
        reviewedVersion: currentPlanVersion,
      },
    );
    if (isPlanRev) {
      sendInternalAgentMessage(
        pi,
        `[Quest Journal] PLAN REVIEW ATTEMPT LIMIT REACHED (v${currentPlanVersion})\n\nThe plan draft has been rejected ${attempts} times without resolution. The plan remains unapproved. You must materially revise your execution plan and plan revisions to address reviewer findings before implementation can proceed.`,
        "steer",
        "plan_review_loop_bound",
        correlationId,
      );
    }
    return {
      limited: true,
      response: {
        success: false,
        available: true,
        error: "Review loop bound reached for current state version",
      },
    };
  }
  return { limited: false };
}
