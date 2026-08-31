import { calculateCurrentTokens } from "../context.ts";
import { logCompactionTransition, logEvent, logRecoveryTransition, logResumeTransition } from "../logging.ts";
import { reportAgentError } from "../messaging.ts";
import { questPath } from "../paths.ts";
import { persist } from "../persistence.ts";
import { triggerReassessment } from "../research.ts";
import { getActiveContext, getSessionId, getState, sessionStates } from "../state.ts";
import { reconcileObligations } from "../obligations.ts";
import { reconcilePendingSubquestResume } from "../subquest.ts";
import { CompactionPressure, CompactionTransaction, ExtensionAPI, ExtensionContext, QuestErrorCode, ResumeReason, StoredState } from "../types.ts";
import { formatTokens } from "../utils.ts";
import { validateCheckpointMatching } from "./checkpoint.ts";
import { resetSteeredTrackingState } from "./execution.ts";
import { createCompactionResumeObligation, dispatchCompactionResume } from "./resume.ts";
import { generateCompactionId } from "./transaction.ts";

export async function handleUnknownCompaction(
	sessionState: StoredState,
	pi: ExtensionAPI,
	c?: ExtensionContext,
): Promise<void> {
	if (!sessionState.active) {
		sessionState.activeTransaction = null;
		sessionState.activeCompactionId = null;
		sessionState.pendingResume = null;
		sessionState.archiveCompactionPending = null;
		persist(pi, c);
		return;
	}

	const externalCompactionId = generateCompactionId();
	const activeQuest = sessionState.active || "";
	let reason: ResumeReason = sessionState.pendingResume?.reason || "normal-compaction";

	if (sessionState.pendingSubquestResume) {
		const subquestStatus = await reconcilePendingSubquestResume(sessionState.pendingSubquestResume, sessionState, pi, c);
		if (subquestStatus === "still-valid") {
			reason = "subquest-launch";
		} else if (subquestStatus === "inconsistent") {
			persist(pi, c);
			return;
		}
		sessionState.subquestLaunchCompactionPending = false;
	}

	const inconsistentTx: CompactionTransaction = {
		id: externalCompactionId,
		phase: "inconsistent",
		activeQuest,
		reason,
		stack: Array.isArray(sessionState.stack) ? [...sessionState.stack] : (activeQuest ? [activeQuest] : []),
		researchRound: sessionState.researchRound || 1,
		reassessmentVersion: (sessionState.reassessmentVersion || 0) + 1,
		planVersion: sessionState.planVersion || 1,
		createdAt: Date.now(),
		error: "Unexpected external compaction occurred without a prepared checkpoint transaction.",
		observedSaveCount: sessionState.saveCount,
		observedHash: sessionState.saveGeneration?.hash || sessionState.lastSavedHash || "",
		observedQuestPath: questPath(sessionState.questId),
	};

	logCompactionTransition("COMPACTION_EXTERNAL", "external compaction detected without transaction", {
		quest: activeQuest,
		compactionId: externalCompactionId,
	});
	logRecoveryTransition("STATE_INCONSISTENT", "state inconsistent due to unexpected external compaction", {
		quest: activeQuest,
		code: "RESUME_STATE_INCONSISTENT",
		reason: "external compaction without checkpoint",
	});
	logRecoveryTransition("RECOVERY_STARTED", "recovering from unexpected external compaction", {
		quest: activeQuest,
		compactionId: externalCompactionId,
	});

	if (!sessionState.activeTransaction || sessionState.activeTransaction.phase !== "resume-pending") {
		sessionState.activeTransaction = inconsistentTx;
		sessionState.activeCompactionId = externalCompactionId;
	}
	triggerReassessment(
		sessionState,
		"Unexpected external compaction occurred without a verified pre-compaction checkpoint.",
	);

	persist(pi, c);

	reportAgentError(
		pi,
		c,
		`[Quest Journal] RESUME_STATE_INCONSISTENT\n\nAn unexpected external compaction occurred without a verified pre-compaction checkpoint transaction.\n\nDo not assume the pre-compaction state was safely preserved.\n\nRequired action:\nreconstruct the current durable quest state, reconcile the discrepancy with quest_update_state, and continue only from the verified state.`,
		{
			code: QuestErrorCode.RESUME_STATE_INCONSISTENT,
			requiredNextAction: "reconstruct the current durable quest state, reconcile the discrepancy with quest_update_state, and continue only from the verified state.",
			details: {
				CompactionId: externalCompactionId,
				AuthoritativeQuest: activeQuest || "(none)",
				LiveGen: sessionState.saveCount,
				LiveHash: sessionState.saveGeneration?.hash || sessionState.lastSavedHash || "",
			},
		},
	);
}

export function resetCompactionWorkingFlags(sessionState: StoredState): void {
	sessionState.compactionPending = false;
	sessionState.lastWarnedCompactionTokens = null;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;
	sessionState.lastNotifiedPressure = CompactionPressure.NONE;
	resetSteeredTrackingState();
}

export async function reconcileCompactionReason(
	tx: CompactionTransaction,
	sessionState: StoredState,
	pi: ExtensionAPI,
	c?: ExtensionContext,
): Promise<ResumeReason | null> {
	let reason: ResumeReason = tx.reason || "normal-compaction";
	const activeQuest = sessionState.active;

	if (sessionState.pendingSubquestResume) {
		const subquestStatus = await reconcilePendingSubquestResume(sessionState.pendingSubquestResume, sessionState, pi, c);
		if (subquestStatus === "still-valid") {
			reason = "subquest-launch";
		} else if (subquestStatus === "inconsistent") {
			tx.phase = "inconsistent";
			persist(pi, c);
			return null;
		}
		sessionState.subquestLaunchCompactionPending = false;
	} else if (sessionState.archiveCompactionPending) {
		sessionState.archiveCompactionPending = null;
		reason = "archive-compaction";
	} else {
		sessionState.subquestLaunchCompactionPending = false;
	}
	return reason;
}

function finalizeCompactionSuccess(
	tx: CompactionTransaction,
	sessionState: StoredState,
	reason: ResumeReason,
	tokens: number | null | undefined,
	pi: ExtensionAPI,
	c?: ExtensionContext,
): void {
	tx.phase = "completed";
	tx.completedAt = Date.now();
	sessionState.dirty = false;
	sessionState.compactCount = sessionState.saveCount;

	logCompactionTransition("COMPACTION_COMPLETED", "compaction completed", {
		quest: sessionState.active || "",
		compactionId: tx.id,
	});

	if (c?.hasUI && typeof tokens === "number" && tokens > 0) {
		c.ui.notify(`Economy auto-compaction completed at ${formatTokens(tokens)} tokens.`, "info");
	}

	const activeQuest = sessionState.active;
	if (!activeQuest) {
		persist(pi, c);
		return;
	}

	tx.phase = "resume-pending";
	tx.reason = reason;

	logResumeTransition("RESUME_OBLIGATION_CREATED", `resume obligation created for '${activeQuest}'`, {
		quest: activeQuest,
		compactionId: tx.id,
		id: tx.id,
		reason,
	});

	sessionState.pendingResume = createCompactionResumeObligation(tx, sessionState, activeQuest, reason);
	reconcileObligations(sessionState, pi, c);
	persist(pi, c);

	dispatchCompactionResume(pi, {
		compactionId: tx.id,
		questName: activeQuest,
		reason,
		ctx: c,
	});
}

export async function handleCompactionCompleted(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	completedAtTokens?: number | null,
): Promise<void> {
	const c = getActiveContext(ctx);
	const targetSessionId = getSessionId(c);
	const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
	const tokens = typeof completedAtTokens === "number" ? completedAtTokens : calculateCurrentTokens(c);

	resetCompactionWorkingFlags(sessionState);

	if (!sessionState.active) {
		sessionState.activeTransaction = null;
		sessionState.activeCompactionId = null;
		sessionState.pendingResume = null;
		sessionState.archiveCompactionPending = null;
		persist(pi, c);
		return;
	}

	const tx = sessionState.activeTransaction;
	if (tx && (tx.phase === "resume-delivered" || tx.phase === "failed" || sessionState.lastDeliveredCompactionId === tx.id)) {
		return;
	}

	const isKnownTransaction = Boolean(tx && (tx.phase === "prepared" || tx.phase === "in-flight"));
	if (!isKnownTransaction) {
		await handleUnknownCompaction(sessionState, pi, c);
		return;
	}

	if (!validateCheckpointMatching(tx, sessionState, pi, c)) {
		return;
	}

	const reason = await reconcileCompactionReason(tx!, sessionState, pi, c);
	if (!reason) return;

	finalizeCompactionSuccess(tx!, sessionState, reason, tokens, pi, c);
}
