import { CUSTOM_TYPE, LEGACY_CUSTOM_TYPE, QuestErrorCode } from "./constants.ts";
import { syncImplementationPermission } from "./gates.ts";
import { logError } from "./messaging.ts";
import { createDefaultState, setSessionState } from "./state.ts";
import type { ExtensionContext, StoredState } from "./types.ts";
import { updateUIStatus } from "./ui.ts";
import { reconstructActiveTransaction, reconstructObligationHistory, reconstructPendingNotifications, reconstructPendingResume, reconstructPendingSubquestResolution } from "./reconstruction/transaction.ts";

export * from "./reconstruction/epistemic.ts";
export * from "./reconstruction/resume.ts";
export * from "./reconstruction/transaction.ts";
export * from "./reconstruction/codecs.ts";

export function loadPersistedJournalSnapshot(ctx: ExtensionContext): StoredState | undefined {
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && (entry.customType === CUSTOM_TYPE || entry.customType === LEGACY_CUSTOM_TYPE) && entry.data) {
			latest = entry.data as unknown as StoredState;
		}
	}
	return latest;
}

export function restoreSessionState(latest: StoredState): StoredState {
	const txData = latest.activeTransaction && typeof latest.activeTransaction === "object" ? (latest.activeTransaction as any) : null;
	const isCompacting = txData ? (txData.phase === "in-flight" || txData.phase === "prepared") : false;
	return {
		questId: typeof latest.questId === "string" ? latest.questId : null,
		active: typeof latest.active === "string" ? latest.active : null,
		pendingRootQuest: typeof latest.pendingRootQuest === "boolean" ? latest.pendingRootQuest : false,
		pendingRootRequest: typeof latest.pendingRootRequest === "string" ? latest.pendingRootRequest : null,
		questIdentityEstablished: typeof latest.questIdentityEstablished === "boolean" ? latest.questIdentityEstablished : false,
		saveCount: typeof latest.saveCount === "number" ? latest.saveCount : 0,
		compactCount: typeof latest.compactCount === "number" ? latest.compactCount : 0,
		prompts: Array.isArray(latest.prompts) ? latest.prompts : [],
		refinements: Array.isArray(latest.refinements) ? latest.refinements : [],
		stack: Array.isArray(latest.stack) ? latest.stack : (latest.active ? [latest.active] : []),
		dirty: typeof latest.dirty === "boolean" ? latest.dirty : false,
		compactionPending: isCompacting || (typeof latest.compactionPending === "boolean" ? latest.compactionPending : false),
		archiveCompactionPending: null,
		subquestLaunchCompactionPending: typeof latest.subquestLaunchCompactionPending === "boolean" ? latest.subquestLaunchCompactionPending : false,
		pendingSubquestResume: typeof latest.pendingSubquestResume === "string" ? latest.pendingSubquestResume : null,
		pendingSubquestResumeResolution: reconstructPendingSubquestResolution(latest.pendingSubquestResumeResolution),
		preCompactionCheckpointPending: false,
		preCompactionSaveRequestPending: false,
		saveGeneration: (latest as any).saveGeneration || null,
		lastSavedHash: (latest as any).lastSavedHash || null,
		economyTokens: typeof latest.economyTokens === "number" ? latest.economyTokens : undefined,
		economyPercent: typeof latest.economyPercent === "number" ? latest.economyPercent : undefined,
		warningMarginTokens: typeof latest.warningMarginTokens === "number" ? latest.warningMarginTokens : undefined,
		subquestCompactTokens: typeof latest.subquestCompactTokens === "number" ? latest.subquestCompactTokens : undefined,
		lastWarnedCompactionTokens: undefined,
		lastPeriodicCheckpointAt: typeof (latest as any).lastPeriodicCheckpointAt === "number" ? (latest as any).lastPeriodicCheckpointAt : 0,
		lastPeriodicCheckpointTurn: typeof (latest as any).lastPeriodicCheckpointTurn === "number" ? (latest as any).lastPeriodicCheckpointTurn : 0,
		lastPeriodicSteerTurn: typeof (latest as any).lastPeriodicSteerTurn === "number" ? (latest as any).lastPeriodicSteerTurn : -1,
		lastPeriodicSteerAt: typeof (latest as any).lastPeriodicSteerAt === "number" ? (latest as any).lastPeriodicSteerAt : 0,
		lastPromptAt: typeof latest.lastPromptAt === "number" ? latest.lastPromptAt : Date.now(),
		lastResumePromptAt: typeof latest.lastResumePromptAt === "number" ? latest.lastResumePromptAt : 0,
		lastResumeTarget: typeof latest.lastResumeTarget === "string" ? latest.lastResumeTarget : null,
		lastResumeCompactCount: typeof latest.lastResumeCompactCount === "number" ? latest.lastResumeCompactCount : undefined,
		activeTransaction: reconstructActiveTransaction(txData),
		activeCompactionId: typeof latest.activeCompactionId === "string" ? latest.activeCompactionId : null,
		lastDeliveredCompactionId: typeof latest.lastDeliveredCompactionId === "string" ? latest.lastDeliveredCompactionId : null,
		pendingResume: reconstructPendingResume(latest.pendingResume),
		pendingNotifications: reconstructPendingNotifications(latest.pendingNotifications),
		obligationHistory: reconstructObligationHistory(latest.obligationHistory),
		pickerCancelled: typeof latest.pickerCancelled === "boolean" ? latest.pickerCancelled : false,
		researchRound: typeof latest.researchRound === "number" ? latest.researchRound : 1,
		researchComplete: typeof latest.researchComplete === "boolean" ? latest.researchComplete : false,
		researchRequired: typeof latest.researchRequired === "boolean" ? latest.researchRequired : (!latest.researchComplete),
		reassessmentRequired: typeof latest.reassessmentRequired === "boolean" ? latest.reassessmentRequired : false,
		reassessmentReason: typeof latest.reassessmentReason === "string" ? latest.reassessmentReason : null,
		reassessmentEvidence: typeof latest.reassessmentEvidence === "string" ? latest.reassessmentEvidence : null,
		reassessmentVersion: typeof latest.reassessmentVersion === "number" ? latest.reassessmentVersion : 0,
		resolvedReassessmentVersion: typeof latest.resolvedReassessmentVersion === "number" ? latest.resolvedReassessmentVersion : 0,
		lastPlanRevisionsText: typeof latest.lastPlanRevisionsText === "string" ? latest.lastPlanRevisionsText : null,
		confirmedQuests: Array.isArray(latest.confirmedQuests) ? latest.confirmedQuests : [],
		lastReassessmentPromptAt: typeof latest.lastReassessmentPromptAt === "number" ? latest.lastReassessmentPromptAt : 0,
		lastReassessmentReason: typeof latest.lastReassessmentReason === "string" ? latest.lastReassessmentReason : null,
		lastCheckpointPromptAt: typeof latest.lastCheckpointPromptAt === "number" ? latest.lastCheckpointPromptAt : 0,
		planVersion: typeof latest.planVersion === "number" ? latest.planVersion : 1,
		planConfidence: (latest.planConfidence as any) || "low",
		lastResearchAt: typeof latest.lastResearchAt === "number" ? latest.lastResearchAt : Date.now(),
		lastPlanRevisionAt: typeof latest.lastPlanRevisionAt === "number" ? latest.lastPlanRevisionAt : Date.now(),
		lastPromptedReassessmentVersion: typeof latest.lastPromptedReassessmentVersion === "number" ? latest.lastPromptedReassessmentVersion : 0,
		implementationAllowed: false,
		awaitingUserConfirmation: typeof latest.awaitingUserConfirmation === "boolean" ? latest.awaitingUserConfirmation : false,
		consecutiveFailures: typeof latest.consecutiveFailures === "number" ? latest.consecutiveFailures : 0,
		substantiveTurnsSinceCheckpoint: typeof latest.substantiveTurnsSinceCheckpoint === "number" ? latest.substantiveTurnsSinceCheckpoint : 0,
		lastCriticalReview: latest.lastCriticalReview ? { ...latest.lastCriticalReview } : null,
		criticalReviews: Array.isArray(latest.criticalReviews) ? latest.criticalReviews.map((r) => ({ ...r })) : [],
		criticalReviewAttempts: latest.criticalReviewAttempts ? { ...latest.criticalReviewAttempts } : {},
		lastReviewedSaveHash: (latest as any).lastReviewedSaveHash || null,
		lastReviewedPlanVersion: (latest as any).lastReviewedPlanVersion || null,
		lastReviewedSaveCount: (latest as any).lastReviewedSaveCount || null,
		lastPlanReviewApproval: latest.lastPlanReviewApproval ? { ...latest.lastPlanReviewApproval } : null,
		lastPlanReviewBoundaryKey: (latest as any).lastPlanReviewBoundaryKey || null,
		lastPlanReviewRequestedVersion: (latest as any).lastPlanReviewRequestedVersion || null,
		investigationEpoch: typeof latest.investigationEpoch === "number" ? latest.investigationEpoch : 1,
		currentReceipt: latest.currentReceipt || null,
		lastCompletedReceipt: latest.lastCompletedReceipt || null,
	} as StoredState;
}

export function reconcileDerivedState(reconstructedState: StoredState, ctx: ExtensionContext): StoredState {
	syncImplementationPermission(reconstructedState);
	setSessionState(ctx, reconstructedState);
	updateUIStatus(ctx);
	return reconstructedState;
}

export function reconstruct(ctx: ExtensionContext): StoredState {
	try {
		const latest = loadPersistedJournalSnapshot(ctx);
		const reconstructedState: StoredState = latest && (latest.active || latest.pendingRootQuest) ? restoreSessionState(latest) : createDefaultState();
		return reconcileDerivedState(reconstructedState, ctx);
	} catch (err: any) {
		logError("Failed to reconstruct state from session history", err, ctx, QuestErrorCode.STATE_RECONSTRUCTION_FAILURE);
		const fallback = createDefaultState();
		return reconcileDerivedState(fallback, ctx);
	}
}
