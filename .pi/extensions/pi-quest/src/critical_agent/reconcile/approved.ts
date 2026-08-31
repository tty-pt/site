import { logCriticalReviewTransition } from "../../logging.ts";
import { persist } from "../../persistence.ts";
import { ExtensionAPI, ExtensionContext, ReviewSnapshot } from "../../types.ts";

export function handleApprovedVerdict(
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
	const passEventType = isPlanReviewKind ? "PLAN_REVIEW_APPROVED" : "CRITICAL_REVIEW_PASSED";
	logCriticalReviewTransition(passEventType, `critical review passed (${snapshot.reviewKind})`, {
		quest: slug, questId, sessionId, reviewId: correlationId,
		childSessionId: parsedResult.childSessionId, parentSessionId: sessionId,
		reviewKind: snapshot.reviewKind, severity: parsedResult.severity,
		verdict: parsedResult.verdict, durationMs: parsedResult.durationMs, reviewedVersion: snapshot.planVersion,
	});
	targetState.lastReviewedPlanVersion = snapshot.planVersion;
	targetState.lastReviewedSaveHash = snapshot.stateHash;
	targetState.lastReviewedSaveCount = targetState.saveCount;
	if (isPlanReviewKind) {
		targetState.lastPlanReviewApproval = {
			questId, planVersion: snapshot.planVersion, reviewId: correlationId,
			boundaryKey: snapshot.boundaryKey || targetState.lastPlanReviewBoundaryKey || null,
			saveHash: snapshot.stateHash, saveCount: targetState.saveCount, timestamp: Date.now(),
		};
		if (snapshot.boundaryKey) targetState.lastPlanReviewBoundaryKey = snapshot.boundaryKey;
	}
	persist(pi, ctx);
	targetState.lastReviewedSaveCount = targetState.saveCount;
	reviewState.reviewedStateVersion.saveCount = targetState.saveCount;
	reviewState.reviewedStateVersion.saveHash = targetState.lastSavedHash || snapshot.stateHash;
	if (isPlanReviewKind && targetState.lastPlanReviewApproval) {
		targetState.lastPlanReviewApproval.saveCount = targetState.saveCount;
		targetState.lastPlanReviewApproval.saveHash = targetState.lastSavedHash || snapshot.stateHash;
	}
	return { success: true, available: true, review: reviewState };
}
