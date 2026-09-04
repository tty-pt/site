import { QuestErrorCode } from "../../constants.ts";
import {
  logCriticalReviewTransition,
  logEvent,
  logToolActivity,
} from "../../logging.ts";
import { reportAgentError } from "../../messaging.ts";
import {
  CriticalReviewKind,
  ExtensionAPI,
  ExtensionContext,
  ReviewActivityStats,
  ReviewSnapshot,
  StoredState,
} from "../../types.ts";
import { classifyTimeoutLayer, PiSubagentReviewer } from "../pi_adapter.ts";
import { createReviewSnapshot } from "../snapshot.ts";
import {
  completeActiveReview,
  updateReviewActivity,
  updateReviewerUIStatus,
} from "../tracker.ts";
import { reconcileReviewResult } from "./reconcile.ts";

export async function executeReviewBackground(
  pi: ExtensionAPI,
  ctx: ExtensionContext,
  params: {
    slug: string;
    questId: string;
    sessionId: string;
    correlationId: string;
    targetState: StoredState;
    currentPlanVersion: number;
    currentHash: string | null;
    currentSaveCount: number;
    provisionalSnapshot: ReviewSnapshot;
    kind: CriticalReviewKind;
    options: any;
    reviewer: PiSubagentReviewer;
    resolveExecution: (res: any) => void;
    abortController?: AbortController;
    onPending?: (snapshot: ReviewSnapshot) => void | Promise<void>;
  },
): Promise<void> {
  const {
    slug,
    questId,
    sessionId,
    correlationId,
    targetState,
    currentPlanVersion,
    currentHash,
    provisionalSnapshot,
    kind,
    options,
    reviewer,
    resolveExecution,
    abortController,
    onPending,
  } = params;

  let childSessionIdRecorded: string | undefined = undefined;
  const activityStats: ReviewActivityStats = {
    turns: 0,
    tools: 0,
    reads: 0,
    searches: 0,
    writes: 0,
    commands: 0,
    files: 0,
    lastActivityAt: Date.now(),
  };

  const onActivity = (activityEvent: any) => {
    if (activityEvent?.childSessionId && !childSessionIdRecorded) {
      childSessionIdRecorded = activityEvent.childSessionId;
      logEvent(
        "SUBAGENT_STARTED",
        `child reviewer session started (${childSessionIdRecorded})`,
        {
          quest: slug,
          questId,
          sessionId,
          reviewId: correlationId,
          childSessionId: childSessionIdRecorded,
          parentSessionId: sessionId,
          kind,
        },
      );
    }

    const updated = updateReviewActivity(correlationId, activityEvent, ctx);
    if (updated) {
      Object.assign(activityStats, updated);
      logEvent(
        "SUBAGENT_ACTIVITY",
        `subagent activity: turns=${activityStats.turns} tools=${activityStats.tools} files=${activityStats.files}`,
        {
          quest: slug,
          questId,
          sessionId,
          reviewId: correlationId,
          childSessionId: childSessionIdRecorded,
          parentSessionId: sessionId,
          kind,
          turns: activityStats.turns,
          tools: activityStats.tools,
          reads: activityStats.reads,
          searches: activityStats.searches,
          writes: activityStats.writes,
          commands: activityStats.commands,
          files: activityStats.files,
        },
      );
    }
  };

  targetState.inCriticalReview = true;
  updateReviewerUIStatus(ctx, "⚖ Critical: reviewer ⟳ starting");

  let snapshot: ReviewSnapshot = provisionalSnapshot;
  try {
    snapshot = await createReviewSnapshot(
      slug,
      correlationId,
      kind,
      sessionId,
      targetState,
      {
        planVersion: currentPlanVersion,
        saveGeneration: params.currentSaveCount,
        stateHash: currentHash,
      },
    );

    const parsedResult = await reviewer.review({
      kind,
      questSlug: slug,
      triggerReason: options.triggerReason,
      boundaryKey: options.boundaryKey,
      context: {
        originalRequest: snapshot.originalUserRequest,
        refinements: snapshot.refinements || [],
        currentUnderstanding: snapshot.currentUnderstanding,
        keyAssumptions: snapshot.assumptions,
        openQuestions: snapshot.findings,
        plan: snapshot.plan,
        planConfidence: targetState.planConfidence || "medium",
        planRevisions: snapshot.planRevisions,
        findings: snapshot.findings,
        filesModified: snapshot.filesChanged,
        testStatus: snapshot.testStatus,
        executionSnapshot: snapshot.executionSnapshot || "",
        exactNextAction: snapshot.nextAction,
        remainingWork: snapshot.remainingWork || "",
        status: snapshot.status || "",
      },
      snapshot,
      rebuttal: options.rebuttal,
      model: options.model,
      reviewId: correlationId,
      childSessionId: childSessionIdRecorded,
      parentSessionId: sessionId,
      timeoutMs: options.timeoutMs,
      signal: abortController?.signal,
      onActivity,
    });

    // If aborted after review returned normally, discard result (runner didn't throw)
    if (abortController?.signal.aborted) {
      logCriticalReviewTransition(
        "REVIEW_CANCELLED",
        `review execution cancelled: ${
          String(abortController.signal.reason || "aborted")
        }`,
        {
          quest: slug,
          questId,
          sessionId,
          reviewId: correlationId,
          reason: String(abortController.signal.reason || "aborted"),
        },
      );
      resolveExecution({
        success: false,
        available: true,
        skipped: true,
        error: "cancelled",
      });
      return;
    }

    logToolActivity("subagent", "success", {
      quest: slug,
      questId,
      sessionId,
      phase: "verification",
      command: `[critical review] ${kind}`,
      turn: targetState.currentTurn,
      correlationId,
      childSessionId: parsedResult.childSessionId || childSessionIdRecorded,
      durationMs: parsedResult.durationMs,
    });

    completeActiveReview(correlationId, parsedResult.verdict, undefined, ctx);

    if (parsedResult.selfCritique) {
      logCriticalReviewTransition(
        "SELF_CRITIQUE_STARTED",
        "reviewer self-critique pass started",
        {
          quest: slug,
          questId,
          sessionId,
          reviewId: correlationId,
          childSessionId: parsedResult.childSessionId,
          parentSessionId: sessionId,
          reviewKind: kind,
          from: parsedResult.selfCritique.initialJudgment,
        },
      );
      if (
        parsedResult.selfCritique.initialJudgment !==
          parsedResult.selfCritique.revisedJudgment
      ) {
        logCriticalReviewTransition(
          "SELF_CRITIQUE_REVISED",
          `reviewer self-critique revised verdict from ${parsedResult.selfCritique.initialJudgment} to ${parsedResult.selfCritique.revisedJudgment}`,
          {
            quest: slug,
            questId,
            sessionId,
            reviewId: correlationId,
            childSessionId: parsedResult.childSessionId,
            parentSessionId: sessionId,
            reviewKind: kind,
            from: parsedResult.selfCritique.initialJudgment,
            to: parsedResult.selfCritique.revisedJudgment,
          },
        );
      }
    }

    parsedResult.activity = activityStats;
    const res = await reconcileReviewResult(
      snapshot,
      parsedResult,
      targetState,
      correlationId,
      pi,
      ctx,
    );
    resolveExecution(res);
  } catch (err: any) {
    // If the review was cancelled (superseded), discard result silently
    const { getActiveReviews } = await import("../tracker.ts");
    if (
      (err?.name === "AbortError") || abortController?.signal.aborted ||
      getActiveReviews().get(correlationId)?.cancelled
    ) {
      logCriticalReviewTransition(
        "REVIEW_CANCELLED",
        `review execution cancelled: ${err?.message || "aborted"}`,
        {
          quest: slug,
          questId,
          sessionId,
          reviewId: correlationId,
          reason: err?.message || "aborted",
        },
      );
      resolveExecution({
        success: false,
        available: true,
        skipped: true,
        error: "cancelled",
      });
      return;
    }
    const timeoutLayer = err?.timeoutLayer ||
      classifyTimeoutLayer(err?.message || "");
    logToolActivity("subagent", "failure", {
      quest: slug,
      questId,
      sessionId,
      phase: "verification",
      command: `[critical review] ${kind}`,
      turn: targetState.currentTurn,
      correlationId,
      childSessionId: childSessionIdRecorded,
      reason: err?.message,
      timeoutLayer,
    });

    logCriticalReviewTransition(
      "CRITICAL_REVIEW_ERROR",
      `critical review execution error: ${
        err?.message || "subagent failure"
      } (${timeoutLayer})`,
      {
        quest: slug,
        questId,
        sessionId,
        reviewId: correlationId,
        childSessionId: childSessionIdRecorded,
        parentSessionId: sessionId,
        reviewKind: kind,
        error: err?.message,
        timeoutLayer,
      },
    );

    completeActiveReview(correlationId, undefined, err?.message, ctx);

    reportAgentError(
      pi,
      ctx,
      `Critical review execution failed: ${
        err?.message || "Subagent execution error"
      }`,
      {
        code: QuestErrorCode.CRITICAL_REVIEW_ERROR,
        correlationId,
        requiredNextAction: timeoutLayer === "provider_model_timeout"
          ? "Review model unavailable. Check PI_CRITICAL_REVIEW_MODEL or verify model credentials, then retry critical review."
          : "Investigate subagent execution error and retry critical review.",
        details: { Quest: slug, ReviewKind: kind, TimeoutLayer: timeoutLayer },
      },
    );

    resolveExecution({
      success: false,
      available: true,
      error: err?.message || "Subagent execution error",
    });
  } finally {
    targetState.inCriticalReview = false;
    if (targetState.awaitingReview?.reviewId === correlationId) {
      targetState.awaitingReview = null;
    }
    updateReviewerUIStatus(ctx);
    if (onPending) await onPending(snapshot);
  }
}
