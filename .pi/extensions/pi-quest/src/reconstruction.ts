import { existsSync, readdirSync, readFileSync } from "node:fs";
import { CUSTOM_TYPE, FUTURE_DIR, LEGACY_CUSTOM_TYPE, QuestErrorCode } from "./constants.ts";
import { syncImplementationPermission } from "./gates.ts";
import { logError } from "./messaging.ts";
import { createDefaultState, generateQuestId, setSessionState } from "./state.ts";
import type { ExtensionContext, StoredState } from "./types.ts";
import { updateUIStatus } from "./ui.ts";
import { reconstructActiveTransaction, reconstructObligationHistory, reconstructPendingNotifications, reconstructPendingResume, reconstructPendingSubquestResolution } from "./reconstruction/transaction.ts";

export * from "./reconstruction/epistemic.ts";
export * from "./reconstruction/resume.ts";
export * from "./reconstruction/transaction.ts";
export * from "./reconstruction/codecs.ts";

export function isStoredState(v: unknown): v is StoredState {
	return typeof v === "object" && v !== null && ("active" in v || "pendingRootQuest" in v || "activeDraft" in v);
}

export function loadPersistedJournalSnapshot(ctx: ExtensionContext): StoredState | undefined {
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && (entry.customType === CUSTOM_TYPE || entry.customType === LEGACY_CUSTOM_TYPE) && entry.data && isStoredState(entry.data)) {
			latest = entry.data as StoredState;
		}
	}
	return latest;
}

export function restoreSessionState(latest: StoredState): StoredState {
	const txData = latest.activeTransaction && typeof latest.activeTransaction === "object" ? latest.activeTransaction : null;
	const isCompacting = txData ? (txData.phase === "in-flight" || txData.phase === "prepared") : false;
	let activeDraft: string | null = typeof latest.activeDraft === "string" ? latest.activeDraft : null;
	let draftPrompts: string[] = Array.isArray(latest.draftPrompts) ? latest.draftPrompts : [];
	let draftCreatedAt: number | null = typeof latest.draftCreatedAt === "number" ? latest.draftCreatedAt : null;
	// 26: orphan fallback — if journal missed activeDraft (killed before flush) scan FUTURE_DIR for any .md
	if (!activeDraft) {
		try {
			const ents = readdirSync(FUTURE_DIR, { withFileTypes: true });
			const md = ents.filter((e) => e.isFile() && e.name.endsWith(".md")).map((e) => e.name.replace(/\.md$/, "")).sort();
			if (md.length) activeDraft = md[0];
		} catch {}
	}
	if (activeDraft && draftPrompts.length === 0) {
		try {
			const c = readFileSync(`${FUTURE_DIR}/${activeDraft}.md`, "utf8");
			const req = c.match(/## Requirements([\s\S]*?)(?:\n## |\n$)/)?.[1] || "";
			const items = req.split("\n").filter((l) => l.trim().startsWith("- ")).map((l) => l.replace(/^-+\s*/, "").trim()).filter(Boolean);
			if (items.length) draftPrompts = items;
		} catch {}
	}
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
		saveGeneration: latest.saveGeneration || null,
		lastSavedHash: latest.lastSavedHash || null,
		economyTokens: typeof latest.economyTokens === "number" ? latest.economyTokens : undefined,
		economyPercent: typeof latest.economyPercent === "number" ? latest.economyPercent : undefined,
		warningMarginTokens: typeof latest.warningMarginTokens === "number" ? latest.warningMarginTokens : undefined,
		subquestCompactTokens: typeof latest.subquestCompactTokens === "number" ? latest.subquestCompactTokens : undefined,
		lastWarnedCompactionTokens: undefined,
		lastPeriodicCheckpointAt: typeof latest.lastPeriodicCheckpointAt === "number" ? latest.lastPeriodicCheckpointAt : 0,
		lastPeriodicCheckpointTurn: typeof latest.lastPeriodicCheckpointTurn === "number" ? latest.lastPeriodicCheckpointTurn : 0,
		lastPeriodicSteerTurn: typeof latest.lastPeriodicSteerTurn === "number" ? latest.lastPeriodicSteerTurn : -1,
		lastPeriodicSteerAt: typeof latest.lastPeriodicSteerAt === "number" ? latest.lastPeriodicSteerAt : 0,
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
		planConfidence: latest.planConfidence || "low",
		lastResearchAt: typeof latest.lastResearchAt === "number" ? latest.lastResearchAt : Date.now(),
		lastPlanRevisionAt: typeof latest.lastPlanRevisionAt === "number" ? latest.lastPlanRevisionAt : Date.now(),
		lastPromptedReassessmentVersion: typeof latest.lastPromptedReassessmentVersion === "number" ? latest.lastPromptedReassessmentVersion : 0,
		implementationAllowed: false,
		awaitingUserConfirmation: typeof latest.awaitingUserConfirmation === "boolean" ? latest.awaitingUserConfirmation : false,
		consecutiveFailures: typeof latest.consecutiveFailures === "number" ? latest.consecutiveFailures : 0,
		substantiveTurnsSinceCheckpoint: typeof latest.substantiveTurnsSinceCheckpoint === "number" ? latest.substantiveTurnsSinceCheckpoint : 0,
		retryTurnsUsed: typeof latest.retryTurnsUsed === "number" ? latest.retryTurnsUsed : 0,
		retryLastStalledTurn: typeof latest.retryLastStalledTurn === "number" ? latest.retryLastStalledTurn : null,
		lastCriticalReview: latest.lastCriticalReview ? { ...latest.lastCriticalReview } : null,
		criticalReviews: Array.isArray(latest.criticalReviews) ? latest.criticalReviews.map((r) => ({ ...r })) : [],
		criticalReviewAttempts: latest.criticalReviewAttempts ? { ...latest.criticalReviewAttempts } : {},
		lastReviewedSaveHash: latest.lastReviewedSaveHash || null,
		lastReviewedPlanVersion: latest.lastReviewedPlanVersion || null,
		lastReviewedSaveCount: latest.lastReviewedSaveCount || null,
		lastPlanReviewApproval: latest.lastPlanReviewApproval ? { ...latest.lastPlanReviewApproval } : null,
		lastPlanReviewBoundaryKey: latest.lastPlanReviewBoundaryKey || null,
		lastPlanReviewRequestedVersion: latest.lastPlanReviewRequestedVersion || null,
		activeDraft,
		draftPrompts,
		draftCreatedAt,
		draftLastSavedHash: typeof latest.draftLastSavedHash === "string" ? latest.draftLastSavedHash : null,
		draftLastReviewKey: typeof latest.draftLastReviewKey === "string" ? latest.draftLastReviewKey : null,
		semanticSummaryEnabled: typeof latest.semanticSummaryEnabled === "boolean" ? latest.semanticSummaryEnabled : undefined,
		thoughtLoggingEnabled: typeof latest.thoughtLoggingEnabled === "boolean" ? latest.thoughtLoggingEnabled : undefined,
		initialPromptLogged: typeof latest.initialPromptLogged === "boolean" ? latest.initialPromptLogged : false,
		lastPlanReviewRequestKey: latest.lastPlanReviewRequestKey || null,
		lastDraftReviewRequestKey: latest.lastDraftReviewRequestKey || null,
		lastDirectionReviewKey: latest.lastDirectionReviewKey || null,
		lastDirectionReviewAt: typeof latest.lastDirectionReviewAt === "number" ? latest.lastDirectionReviewAt : null,
		awaitingReview: latest.awaitingReview ? { ...latest.awaitingReview } : null,
		investigationEpoch: typeof latest.investigationEpoch === "number" ? latest.investigationEpoch : 1,
		currentReceipt: latest.currentReceipt || null,
		lastCompletedReceipt: latest.lastCompletedReceipt || null,
	} as StoredState;
}

export function reconcileDerivedState(reconstructedState: StoredState, ctx: ExtensionContext): StoredState {
	// invariant: questId never null while drafting/active/pending — fix 2026-09-02
	if ((reconstructedState.active || reconstructedState.activeDraft || reconstructedState.pendingRootQuest) && !reconstructedState.questId) {
		reconstructedState.questId = generateQuestId();
	}
	syncImplementationPermission(reconstructedState);
	setSessionState(ctx, reconstructedState);
	updateUIStatus(ctx);
	return reconstructedState;
}

export function reconstruct(ctx: ExtensionContext): StoredState {
	try {
		const latest = loadPersistedJournalSnapshot(ctx);
		// 26: hasDraft includes disk scan — orphan future/<slug>.md without journal
		const hasDraftOnDisk = (() => { try { return readdirSync(FUTURE_DIR).filter((f) => f.endsWith(".md")).length > 0; } catch { return false; } })();
		const hasDraft = !!latest?.activeDraft || !!(latest?.draftPrompts?.length) || hasDraftOnDisk;
		const reconstructedState: StoredState = latest && (latest.active || latest.pendingRootQuest || hasDraft) ? restoreSessionState(latest) : hasDraftOnDisk ? restoreSessionState((latest || {}) as StoredState) : createDefaultState();
		return reconcileDerivedState(reconstructedState, ctx);
	} catch (err: any) {
		logError("Failed to reconstruct state from session history", err, ctx, QuestErrorCode.STATE_RECONSTRUCTION_FAILURE);
		const fallback = createDefaultState();
		return reconcileDerivedState(fallback, ctx);
	}
}
