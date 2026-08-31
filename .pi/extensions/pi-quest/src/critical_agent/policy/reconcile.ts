import { logCriticalReviewTransition } from "../../logging.ts";
import { persist } from "../../persistence.ts";
import { ExtensionAPI, ExtensionContext, ReviewSnapshot, StoredState, CriticalReviewState } from "../../types.ts";
import { getQuestLockKey, withQuestLock } from "../../utils/mutex.ts";
import { isReviewSnapshotCurrent } from "../snapshot.ts";
import { handleApprovedVerdict } from "../reconcile/approved.ts";
import { handleRejectedVerdict } from "../reconcile/rejected.ts";
import { handleUncertainVerdict } from "../reconcile/uncertain.ts";

export async function reconcileReviewResult(
	snapshot: ReviewSnapshot,
	parsedResult: any,
	targetState: StoredState,
	correlationId: string,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): Promise<any> {
	const lockKey = getQuestLockKey(snapshot.questId, snapshot.sessionId);
	return withQuestLock(lockKey, async () => {
	const slug = snapshot.questId;
	const questId = targetState.questId || slug;
	const sessionId = snapshot.sessionId;
	const isPlanReviewKind = snapshot.reviewKind === "plan_review";

	const isApproved = parsedResult.verdict === "PASS" || parsedResult.verdict === "APPROVE";
	const isRejected = parsedResult.verdict === "FAIL" || parsedResult.verdict === "REVISE";

	const reviewState: CriticalReviewState = {
		id: correlationId,
		questId,
		kind: snapshot.reviewKind,
		reviewId: correlationId,
		childSessionId: parsedResult.childSessionId,
		parentSessionId: sessionId,
		reviewedStateVersion: {
			planVersion: snapshot.planVersion,
			saveHash: snapshot.stateHash,
			saveCount: snapshot.saveGeneration,
		},
		snapshot,
		verdict: parsedResult.verdict,
		severity: parsedResult.severity,
		findings: parsedResult.findings,
		requiredActions: parsedResult.requiredActions,
		originalRequestCheck: parsedResult.originalRequestCheck,
		selfCritique: parsedResult.selfCritique,
		resolved: isApproved,
		durationMs: parsedResult.durationMs,
		activity: parsedResult.activity,
		childTranscriptRef: parsedResult.childTranscriptRef,
		timestamp: Date.now(),
		correlationId,
	};

	const currentness = isReviewSnapshotCurrent(snapshot, targetState);
	if (!currentness.current) {
		reviewState.superseded = true;
		reviewState.resolved = false;
		reviewState.supersededBy = {
			planVersion: targetState.planVersion || 1,
			saveHash: targetState.lastSavedHash || null,
			saveCount: targetState.saveCount || 0,
			reason: currentness.reason,
		};

		logCriticalReviewTransition("CRITICAL_REVIEW_SUPERSEDED", `review result superseded by newer state (${currentness.reason})`, {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			childSessionId: parsedResult.childSessionId,
			parentSessionId: sessionId,
			reviewKind: snapshot.reviewKind,
			reviewedVersion: snapshot.planVersion,
			supersededByVersion: targetState.planVersion || 1,
			reason: currentness.reason,
		});

		if (!Array.isArray(targetState.criticalReviews)) {
			targetState.criticalReviews = [];
		}
		targetState.criticalReviews.push(reviewState);
		if (targetState.criticalReviews.length > 20) {
			targetState.criticalReviews = targetState.criticalReviews.slice(-20);
		}

		// Superseded path: clear stale awaitingReview gate if still pointing at this correlationId
		if ((targetState as any).awaitingReview?.reviewId === correlationId) {
			(targetState as any).awaitingReview = null;
		}
		persist(pi, ctx);
		return { success: false, available: true, superseded: true, review: reviewState };
	}

	targetState.lastCriticalReview = reviewState;
	if (!Array.isArray(targetState.criticalReviews)) {
		targetState.criticalReviews = [];
	}
	targetState.criticalReviews.push(reviewState);
	if (targetState.criticalReviews.length > 20) {
		targetState.criticalReviews = targetState.criticalReviews.slice(-20);
	}

	if (isApproved) {
		return handleApprovedVerdict(snapshot, parsedResult, targetState, correlationId, reviewState, isPlanReviewKind, pi, ctx);
	}
	if (isRejected) {
		return await handleRejectedVerdict(snapshot, parsedResult, targetState, correlationId, reviewState, isPlanReviewKind, pi, ctx);
	}
	return handleUncertainVerdict(snapshot, parsedResult, targetState, correlationId, reviewState, isPlanReviewKind, pi, ctx);
	});
}
