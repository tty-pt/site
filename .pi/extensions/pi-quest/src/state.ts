import { AsyncLocalStorage } from "node:async_hooks";
import { existsSync } from "node:fs";
import { CompactionPressure, ExtensionContext, StoredState } from "./types.ts";
import { QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR } from "./constants.ts";

export const sessionStates = new Map<string, StoredState>();

export const sessionStartMap = new Map<string, number>();

export const asyncContext = new AsyncLocalStorage<ExtensionContext>();

let lastGeneratedSec: number | null = null;

export function generateQuestId(now: number = Date.now()): string {
	let sec = Math.floor(now / 1000);
	// increment by one on same-second / monotonic collision (in-memory)
	if (lastGeneratedSec !== null && sec <= lastGeneratedSec) {
		sec = lastGeneratedSec + 1;
	}
	// increment by one on collision with existing quest dir or archive (keep existing dirs/zips)
	try {
		while (existsSync(`${QUEST_CURRENT_DIR}/${String(sec)}`) || existsSync(`${QUEST_ARCHIVE_DIR}/${String(sec)}.zip`)) {
			sec += 1;
			// safety guard against infinite loop
			if (sec > Math.floor(now / 1000) + 100000) break;
		}
	} catch {}
	// ensure monotonic across filesystem bumps too
	if (lastGeneratedSec !== null && sec <= lastGeneratedSec) {
		sec = lastGeneratedSec + 1;
	}
	lastGeneratedSec = sec;
	return String(sec);
}

export function __resetQuestIdSequenceForTests(): void {
	lastGeneratedSec = null;
}

export function getActiveContext(ctx?: ExtensionContext): ExtensionContext | undefined {
	return asyncContext.getStore() ?? ctx;
}

export function createDefaultState(): StoredState {
	return {
		questId: null,
		active: null,
		saveCount: 0,
		compactCount: 0,
		prompts: [],
		refinements: [],
		stack: [],
		dirty: false,
		compactionPending: false,
		archiveCompactionPending: null,
		subquestLaunchCompactionPending: false,
		pendingSubquestResume: null,
		pendingSubquestResumeResolution: null,
		preCompactionCheckpointPending: false,
		preCompactionSaveRequestPending: false,
		saveGeneration: null,
		lastSavedHash: null,
		economyTokens: undefined,
		economyPercent: undefined,
		warningMarginTokens: undefined,
		subquestCompactTokens: undefined,
		lastWarnedCompactionTokens: undefined,
		lastPeriodicCheckpointAt: 0,
		lastPeriodicCheckpointTurn: 0,
		lastPeriodicSteerTurn: -1,
		lastPeriodicSteerAt: 0,
		lastPromptAt: Date.now(),
		lastResumePromptAt: 0,
		lastResumeTarget: null,
		lastResumeCompactCount: undefined,
		activeTransaction: null,
		activeCompactionId: null,
		lastDeliveredCompactionId: null,
		pendingResume: null,
		pendingNotifications: [],
		obligationHistory: [],
		pickerCancelled: false,
		pendingRootQuest: false,
		pendingRootRequest: null,
		questIdentityEstablished: false,
		researchRound: 1,
		researchComplete: false,
		researchRequired: true,
		reassessmentRequired: false,
		reassessmentReason: null,
		reassessmentEvidence: null,
		reassessmentVersion: 0,
		resolvedReassessmentVersion: 0,
		lastPlanRevisionsText: null,
		confirmedQuests: [],
		lastReassessmentPromptAt: 0,
		lastReassessmentReason: null,
		lastCheckpointPromptAt: 0,
		planVersion: 1,
		planConfidence: "low",
		lastResearchAt: Date.now(),
		lastPlanRevisionAt: Date.now(),
		lastPromptedReassessmentVersion: 0,
		implementationAllowed: false,
		awaitingUserConfirmation: false,
		consecutiveFailures: 0,
		substantiveTurnsSinceCheckpoint: 0,
		sessionModifiedFiles: [],
		lastNotifiedPressure: CompactionPressure.NONE,
		lastContinuationTransitionKey: null,
		activeDraft: null,
		draftPrompts: [],
		draftCreatedAt: null,
		draftLastSavedHash: null,
		draftLastReviewKey: null,
		semanticSummaryEnabled: undefined,
		thoughtLoggingEnabled: true,
		autonomousSubquestDuringDrafting: false,
		initialPromptLogged: false,
		lastPlanReviewApproval: null,
		lastPlanReviewRequestedVersion: null,
		lastPlanReviewBoundaryKey: null,
		lastPlanReviewRequestKey: null,
		lastDraftReviewRequestKey: null,
		lastDirectionReviewKey: null,
		lastDirectionReviewAt: null,
		awaitingReview: null,
		investigationEpoch: 1,
		logCursor: 0,
		currentReceipt: {
			epoch: 1,
			epochType: "research",
			startedAt: Date.now(),
			toolCalls: 0,
			readTargets: [],
			searchTargets: [],
			commands: [],
			evidenceCount: 0,
		},
		lastCompletedReceipt: null,
	};
}

export function snapshotState(ctx?: ExtensionContext): StoredState {
	const s = getState(ctx);
	return {
		questId: s.questId,
		active: s.active,
		pendingRootQuest: s.pendingRootQuest,
		pendingRootRequest: s.pendingRootRequest,
		questIdentityEstablished: s.questIdentityEstablished,
		saveCount: s.saveCount,
		compactCount: s.compactCount,
		prompts: Array.isArray(s.prompts) ? [...s.prompts] : [],
		refinements: Array.isArray(s.refinements) ? [...s.refinements] : [],
		stack: Array.isArray(s.stack) ? [...s.stack] : [],
		dirty: s.dirty,
		compactionPending: s.compactionPending,
		archiveCompactionPending: s.archiveCompactionPending,
		subquestLaunchCompactionPending: s.subquestLaunchCompactionPending,
		pendingSubquestResume: s.pendingSubquestResume,
		pendingSubquestResumeResolution: s.pendingSubquestResumeResolution ? { ...s.pendingSubquestResumeResolution } : null,
		preCompactionCheckpointPending: s.preCompactionCheckpointPending,
		preCompactionSaveRequestPending: s.preCompactionSaveRequestPending,
		saveGeneration: s.saveGeneration ? { ...s.saveGeneration } : null,
		lastSavedHash: s.lastSavedHash,
		economyTokens: s.economyTokens,
		economyPercent: s.economyPercent,
		warningMarginTokens: s.warningMarginTokens,
		subquestCompactTokens: s.subquestCompactTokens,
		lastWarnedCompactionTokens: s.lastWarnedCompactionTokens,
		lastPeriodicCheckpointAt: (s as any).lastPeriodicCheckpointAt || 0,
		lastPeriodicCheckpointTurn: (s as any).lastPeriodicCheckpointTurn || 0,
		lastPeriodicSteerTurn: (s as any).lastPeriodicSteerTurn ?? -1,
		lastPeriodicSteerAt: (s as any).lastPeriodicSteerAt || 0,
		lastPromptAt: s.lastPromptAt,
		lastResumePromptAt: s.lastResumePromptAt,
		lastResumeTarget: s.lastResumeTarget,
		lastResumeCompactCount: s.lastResumeCompactCount,
		activeTransaction: s.activeTransaction ? { ...s.activeTransaction, stack: [...(s.activeTransaction.stack || [])] } : null,
		activeCompactionId: s.activeCompactionId,
		lastDeliveredCompactionId: s.lastDeliveredCompactionId,
		pendingResume: s.pendingResume
			? {
					compactionId: s.pendingResume.compactionId,
					activeQuest: s.pendingResume.activeQuest,
					reason: s.pendingResume.reason,
					checkpointSaveCount: s.pendingResume.checkpointSaveCount,
					checkpointHash: s.pendingResume.checkpointHash,
					checkpointQuestPath: s.pendingResume.checkpointQuestPath,
					attempts: s.pendingResume.attempts,
					createdAt: s.pendingResume.createdAt,
					lastAttemptAt: s.pendingResume.lastAttemptAt,
					deliveredAt: s.pendingResume.deliveredAt,
			  }
			: null,
		pendingNotifications: Array.isArray(s.pendingNotifications) ? s.pendingNotifications.map((n) => ({ ...n })) : [],
		obligationHistory: Array.isArray(s.obligationHistory) ? s.obligationHistory.map((n) => ({ ...n })) : [],
		pickerCancelled: s.pickerCancelled,
		researchRound: s.researchRound,
		researchComplete: s.researchComplete,
		researchRequired: s.researchRequired,
		reassessmentRequired: s.reassessmentRequired,
		reassessmentReason: s.reassessmentReason,
		reassessmentEvidence: s.reassessmentEvidence,
		reassessmentVersion: s.reassessmentVersion,
		resolvedReassessmentVersion: s.resolvedReassessmentVersion,
		lastPlanRevisionsText: s.lastPlanRevisionsText,
		confirmedQuests: Array.isArray(s.confirmedQuests) ? [...s.confirmedQuests] : [],
		lastReassessmentPromptAt: s.lastReassessmentPromptAt,
		lastReassessmentReason: s.lastReassessmentReason,
		lastCheckpointPromptAt: s.lastCheckpointPromptAt,
		planVersion: s.planVersion,
		planConfidence: s.planConfidence,
		lastResearchAt: s.lastResearchAt,
		lastPlanRevisionAt: s.lastPlanRevisionAt,
		lastPromptedReassessmentVersion: s.lastPromptedReassessmentVersion,
		implementationAllowed: s.implementationAllowed ?? false,
		awaitingUserConfirmation: s.awaitingUserConfirmation,
		lastNotifiedPressure: s.lastNotifiedPressure || CompactionPressure.NONE,
		consecutiveFailures: s.consecutiveFailures || 0,
		substantiveTurnsSinceCheckpoint: s.substantiveTurnsSinceCheckpoint || 0,
		lastCriticalReview: s.lastCriticalReview ? { ...s.lastCriticalReview } : null,
		criticalReviews: Array.isArray(s.criticalReviews) ? s.criticalReviews.map((r) => ({ ...r })) : [],
		criticalReviewAttempts: s.criticalReviewAttempts ? { ...s.criticalReviewAttempts } : {},
		lastReviewedSaveHash: s.lastReviewedSaveHash || null,
		lastReviewedPlanVersion: s.lastReviewedPlanVersion || null,
		lastReviewedSaveCount: s.lastReviewedSaveCount || null,
		activeDraft: s.activeDraft || null,
		draftPrompts: Array.isArray(s.draftPrompts) ? [...s.draftPrompts] : [],
		draftCreatedAt: s.draftCreatedAt || null,
		draftLastSavedHash: s.draftLastSavedHash || null,
		draftLastReviewKey: s.draftLastReviewKey || null,
		semanticSummaryEnabled: (s as any).semanticSummaryEnabled,
		thoughtLoggingEnabled: (s as any).thoughtLoggingEnabled,
		autonomousSubquestDuringDrafting: (s as any).autonomousSubquestDuringDrafting,
		initialPromptLogged: !!(s as any).initialPromptLogged,
		lastPlanReviewApproval: s.lastPlanReviewApproval ? { ...s.lastPlanReviewApproval } : null,
		lastPlanReviewRequestedVersion: s.lastPlanReviewRequestedVersion || null,
		lastPlanReviewBoundaryKey: s.lastPlanReviewBoundaryKey || null,
		lastPlanReviewRequestKey: (s as any).lastPlanReviewRequestKey || null,
		lastDraftReviewRequestKey: (s as any).lastDraftReviewRequestKey || null,
		lastDirectionReviewKey: (s as any).lastDirectionReviewKey || null,
		lastDirectionReviewAt: (s as any).lastDirectionReviewAt || null,
		awaitingReview: (s as any).awaitingReview ? { ...(s as any).awaitingReview } : null,
		investigationEpoch: s.investigationEpoch || 1,
		logCursor: s.logCursor || 0,
		currentReceipt: s.currentReceipt
			? {
					epoch: s.currentReceipt.epoch,
					epochType: s.currentReceipt.epochType,
					startedAt: s.currentReceipt.startedAt,
					toolCalls: s.currentReceipt.toolCalls,
					readTargets: [...(s.currentReceipt.readTargets || [])],
					searchTargets: [...(s.currentReceipt.searchTargets || [])],
					commands: [...(s.currentReceipt.commands || [])],
					evidenceCount: s.currentReceipt.evidenceCount,
					lastEvidenceAt: s.currentReceipt.lastEvidenceAt,
					completedAt: s.currentReceipt.completedAt,
			  }
			: null,
		lastCompletedReceipt: s.lastCompletedReceipt
			? {
					epoch: s.lastCompletedReceipt.epoch,
					epochType: s.lastCompletedReceipt.epochType,
					startedAt: s.lastCompletedReceipt.startedAt,
					toolCalls: s.lastCompletedReceipt.toolCalls,
					readTargets: [...(s.lastCompletedReceipt.readTargets || [])],
					searchTargets: [...(s.lastCompletedReceipt.searchTargets || [])],
					commands: [...(s.lastCompletedReceipt.commands || [])],
					evidenceCount: s.lastCompletedReceipt.evidenceCount,
					lastEvidenceAt: s.lastCompletedReceipt.lastEvidenceAt,
					completedAt: s.lastCompletedReceipt.completedAt,
			  }
			: null,
	};
}

export function getSessionId(ctx?: ExtensionContext): string {
	const c = getActiveContext(ctx);
	if (!c) return "default";
	const sm = c.sessionManager;
	const id = sm?.id || sm?.sessionId || (typeof sm?.getSessionId === "function" ? sm.getSessionId() : null) || (c as any).sessionId;
	return id && typeof id === "string" ? id : "default";
}

export function getState(ctx?: ExtensionContext): StoredState {
	const c = getActiveContext(ctx);
	const id = getSessionId(c);
	let s = sessionStates.get(id);
	if (!s) {
		s = createDefaultState();
		sessionStates.set(id, s);
	}
	return s;
}

export function getQuestId(ctx?: ExtensionContext): string | null {
	const s = getState(ctx);
	return s.questId || null;
}

export function ensureQuestId(ctx?: ExtensionContext): string {
	const s = getState(ctx);
	if (!s.questId) {
		s.questId = generateQuestId();
	}
	return s.questId;
}

let stateChangeHandler: ((ctx?: ExtensionContext) => void) | null = null;
export function registerStateChangeHandler(fn: (ctx?: ExtensionContext) => void) {
	stateChangeHandler = fn;
}

export function setSessionState(ctx: ExtensionContext | undefined, newState: StoredState) {
	const id = getSessionId(ctx);
	sessionStates.set(id, newState);
	if (stateChangeHandler) {
		stateChangeHandler(ctx);
	}
}

export const state = new Proxy({} as StoredState, {
	get(_target, prop: string) {
		const s = getState();
		return (s as any)[prop];
	},
	set(_target, prop: string, value: any) {
		const s = getState();
		(s as any)[prop] = value;
		return true;
	},
});

export function isRootQuest(targetState?: StoredState): boolean {
	const s = targetState || state;
	return !Array.isArray(s.stack) || s.stack.length <= 1;
}
