import { calculateCurrentTokens } from "../context.ts";
import { logAgentMessageTransition, logCompactionTransition, logEvent, logResumeTransition } from "../logging.ts";
import { logDebug, logError, reportAgentError, sendInternalAgentMessage } from "../messaging.ts";
import { questPath } from "../paths.ts";
import { persist } from "../persistence.ts";
import { asyncContext, getActiveContext, getSessionId, getState, sessionStates, state } from "../state.ts";
import { CompactionPressure, ExtensionAPI, ExtensionContext, QuestErrorCode, StoredState } from "../types.ts";
import { formatTokens } from "../utils.ts";
import { buildCriticalCompactionReadyPrompt, buildCriticalSavePrompt, buildWarningSavePrompt, compactionReady, getCompactionInstructions } from "./checkpoint.ts";
import { getCompactionPressure, getEconomyThreshold, getSubquestCompactThreshold, getWarningMargin } from "./policy.ts";
import { dispatchCompactionResume, retryPendingResume } from "./resume.ts";
import { createOrGetCompactionTransaction } from "./transaction.ts";

export let lastSteerTurnCounter = 0;
export let lastSteeredTurn = -1;
export let lastSteeredPressureState: CompactionPressure | null = null;
export let lastSteeredReadyState: boolean | null = null;
export let lastPreCompactionSteerTime = 0;

export function advanceSteerTurnCounter() {
	lastSteerTurnCounter++;
}

export function resetSteeredTrackingState(): void {
	lastSteeredPressureState = null;
	lastSteeredReadyState = null;
}

function handleScheduledEconomyError(
	pi: ExtensionAPI,
	c: ExtensionContext,
	sessionState: StoredState,
	err: any,
	targetCompactionId?: string | null,
): void {
	if (targetCompactionId && sessionState.activeTransaction?.id !== targetCompactionId) {
		logDebug(`Quest Journal: ignoring stale handleScheduledEconomyError for tx=${targetCompactionId}`);
		return;
	}
	sessionState.compactionPending = false;
	if (sessionState.activeTransaction && (!targetCompactionId || sessionState.activeTransaction.id === targetCompactionId)) {
		sessionState.activeTransaction.phase = "failed";
		sessionState.activeTransaction.failedAt = Date.now();
		sessionState.activeTransaction.error = err?.message || String(err);
	}
	if (!targetCompactionId || sessionState.activeCompactionId === targetCompactionId) {
		sessionState.activeCompactionId = null;
	}
	sessionState.lastWarnedCompactionTokens = null;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;

	const msg = err?.message || String(err);
	if (!msg.includes("Nothing to compact") && !msg.includes("Already compacted") && !msg.includes("session too small")) {
		if (c.hasUI) c.ui.notify(`Economy auto-compaction failed: ${msg}`, "error");
		reportAgentError(
			pi,
			c,
			`Economy auto-compaction failed: ${msg}`,
			{
				code: QuestErrorCode.COMPACTION_FAILURE,
				requiredNextAction: "Review working memory and continue execution; compaction will be re-attempted when context pressure warrants.",
			},
		);
	}
}

export function scheduleEconomyCompaction(
	pi: ExtensionAPI,
	c: ExtensionContext,
	targetSessionId: string,
	instructions: string,
): void {
	const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
	const targetQuestId = sessionState.questId;
	const targetActiveQuest = sessionState.active;
	const targetCompactionId = sessionState.activeTransaction?.id || sessionState.activeCompactionId || null;

	setTimeout(() => {
		asyncContext.run(c, () => {
			const currentSessionState = sessionStates.get(targetSessionId) ?? getState(c);
			if (
				currentSessionState.questId !== targetQuestId ||
				currentSessionState.active !== targetActiveQuest ||
				(targetCompactionId && currentSessionState.activeTransaction?.id !== targetCompactionId)
			) {
				logDebug(`Quest Journal: ignoring stale scheduled economy compaction callback (scheduled for questId=${targetQuestId}, active=${targetActiveQuest}, tx=${targetCompactionId})`);
				return;
			}
			try {
				c.compact!({
					customInstructions: instructions,
					onComplete: () => {},
					onError: (err: any) => {
						const latestState = sessionStates.get(targetSessionId) ?? getState(c);
						if (
							latestState.questId !== targetQuestId ||
							latestState.active !== targetActiveQuest ||
							(targetCompactionId && latestState.activeTransaction?.id !== targetCompactionId)
						) {
							logDebug(`Quest Journal: ignoring stale economy compaction error callback (scheduled for tx=${targetCompactionId})`);
							return;
						}
						handleScheduledEconomyError(pi, c, latestState, err, targetCompactionId);
					},
				});
			} catch (err: any) {
				handleScheduledEconomyError(pi, c, currentSessionState, err, targetCompactionId);
			}
		});
	}, 50);
}

function handleScheduledArchiveError(
	pi: ExtensionAPI,
	c: ExtensionContext,
	sessionState: StoredState,
	err: any,
	parentName: string | null,
	targetCompactionId?: string | null,
): void {
	if (targetCompactionId && sessionState.activeTransaction?.id !== targetCompactionId) {
		logDebug(`Quest Journal: ignoring stale handleScheduledArchiveError for tx=${targetCompactionId}`);
		return;
	}
	sessionState.compactionPending = false;
	if (sessionState.activeTransaction && (!targetCompactionId || sessionState.activeTransaction.id === targetCompactionId)) {
		sessionState.activeTransaction.phase = "failed";
		sessionState.activeTransaction.failedAt = Date.now();
		sessionState.activeTransaction.error = err?.message || String(err);
	}
	if (!targetCompactionId || sessionState.activeCompactionId === targetCompactionId) {
		sessionState.activeCompactionId = null;
	}
	sessionState.archiveCompactionPending = null;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;

	const msg = err?.message || String(err);
	if (!msg.includes("Nothing to compact") && !msg.includes("Already compacted") && !msg.includes("session too small")) {
		if (c.hasUI) c.ui.notify(`Post-archive compaction failed: ${msg}`, "error");
		reportAgentError(
			pi,
			c,
			`Post-archive compaction failed: ${msg}`,
			{
				code: QuestErrorCode.COMPACTION_FAILURE,
				requiredNextAction: parentName
					? `Read ${questPath(sessionState.questId)} to resume parent execution.`
					: "Review active memory and continue execution.",
			},
		);
	}
	if (parentName && (sessionState.active === parentName || (Array.isArray(sessionState.stack) && sessionState.stack.includes(parentName)))) {
		dispatchCompactionResume(pi, {
			questName: parentName,
			reason: "compaction-failure-fallback",
			ctx: c,
		});
	}
}

export function scheduleArchiveCompaction(
	pi: ExtensionAPI,
	c: ExtensionContext,
	targetSessionId: string,
	instructions: string,
	parentName: string | null,
): void {
	const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
	const targetQuestId = sessionState.questId;
	const targetActiveQuest = sessionState.active;
	const targetCompactionId = sessionState.activeTransaction?.id || sessionState.activeCompactionId || null;

	setTimeout(() => {
		asyncContext.run(c, () => {
			const currentSessionState = sessionStates.get(targetSessionId) ?? getState(c);
			if (
				currentSessionState.questId !== targetQuestId ||
				currentSessionState.active !== targetActiveQuest ||
				(targetCompactionId && currentSessionState.activeTransaction?.id !== targetCompactionId)
			) {
				logDebug(`Quest Journal: ignoring stale scheduled archive compaction callback`);
				return;
			}
			try {
				c.compact!({
					customInstructions: instructions,
					onComplete: () => {},
					onError: (err: any) => {
						const latestState = sessionStates.get(targetSessionId) ?? getState(c);
						if (
							latestState.questId !== targetQuestId ||
							latestState.active !== targetActiveQuest ||
							(targetCompactionId && latestState.activeTransaction?.id !== targetCompactionId)
						) {
							logDebug(`Quest Journal: ignoring stale archive compaction error callback`);
							return;
						}
						handleScheduledArchiveError(pi, c, latestState, err, parentName, targetCompactionId);
					},
				});
			} catch (err: any) {
				handleScheduledArchiveError(pi, c, currentSessionState, err, parentName, targetCompactionId);
			}
		});
	}, 50);
}

export function handleSubquestLaunchCompactionFailure(
	pi: ExtensionAPI,
	c: ExtensionContext,
	sessionState: StoredState,
	err: any,
	childName: string,
	isSchedulingError = false,
	targetCompactionId?: string | null,
): void {
	if (targetCompactionId && sessionState.activeTransaction?.id !== targetCompactionId) {
		logDebug(`Quest Journal: ignoring stale handleSubquestLaunchCompactionFailure for tx=${targetCompactionId}`);
		return;
	}
	sessionState.compactionPending = false;
	if (sessionState.activeTransaction && (!targetCompactionId || sessionState.activeTransaction.id === targetCompactionId)) {
		sessionState.activeTransaction.phase = "failed";
		sessionState.activeTransaction.failedAt = Date.now();
		sessionState.activeTransaction.error = err?.message || String(err);
	}
	if (!targetCompactionId || sessionState.activeCompactionId === targetCompactionId) {
		sessionState.activeCompactionId = null;
	}
	sessionState.subquestLaunchCompactionPending = false;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;

	const msg = err?.message || String(err);
	const prefix = isSchedulingError ? "Sub-quest launch compaction scheduling failed" : "Sub-quest launch compaction failed";
	if (isSchedulingError) {
		logError(prefix, err, c);
	}

	if (!msg.includes("Nothing to compact") && !msg.includes("Already compacted") && !msg.includes("session too small")) {
		if (c.hasUI) c.ui.notify(`${prefix}: ${msg}`, "error");
		reportAgentError(
			pi,
			c,
			`${prefix}: ${msg}`,
			{
				code: QuestErrorCode.COMPACTION_FAILURE,
				requiredNextAction: childName
					? `Read ${questPath(sessionState.questId)} to proceed with subquest execution.`
					: "Review active memory and continue execution.",
			},
		);
	}

	const fallbackTarget = (sessionState.pendingSubquestResume && sessionState.active === sessionState.pendingSubquestResume)
		? sessionState.pendingSubquestResume
		: childName;

	if (fallbackTarget && (sessionState.active === fallbackTarget || sessionState.pendingSubquestResume === fallbackTarget)) {
		if (fallbackTarget === sessionState.pendingSubquestResume) {
			logResumeTransition("RESUME_ATTEMPTED", `subquest resume after compaction failure fallback: ${fallbackTarget}`, {
				quest: fallbackTarget,
				subquest: fallbackTarget,
				reason: "post-launch-compaction-fallback",
			});
		}
		dispatchCompactionResume(pi, {
			questName: fallbackTarget,
			reason: "compaction-failure-fallback",
			ctx: c,
		});
	}
}

export function scheduleSubquestLaunchCompaction(
	pi: ExtensionAPI,
	c: ExtensionContext,
	targetSessionId: string,
	instructions: string,
	childName: string,
): void {
	const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
	const targetQuestId = sessionState.questId;
	const targetActiveQuest = sessionState.active;
	const targetCompactionId = sessionState.activeTransaction?.id || sessionState.activeCompactionId || null;

	setTimeout(() => {
		asyncContext.run(c, () => {
			const currentSessionState = sessionStates.get(targetSessionId) ?? getState(c);
			if (
				currentSessionState.questId !== targetQuestId ||
				currentSessionState.active !== targetActiveQuest ||
				(targetCompactionId && currentSessionState.activeTransaction?.id !== targetCompactionId)
			) {
				logDebug(`Quest Journal: ignoring stale scheduled subquest launch compaction callback`);
				return;
			}
			try {
				c.compact!({
					customInstructions: instructions,
					onComplete: () => {},
					onError: (err: any) => {
						const latestState = sessionStates.get(targetSessionId) ?? getState(c);
						if (
							latestState.questId !== targetQuestId ||
							latestState.active !== targetActiveQuest ||
							(targetCompactionId && latestState.activeTransaction?.id !== targetCompactionId)
						) {
							logDebug(`Quest Journal: ignoring stale subquest launch compaction error callback`);
							return;
						}
						handleSubquestLaunchCompactionFailure(pi, c, latestState, err, childName, false, targetCompactionId);
					},
				});
			} catch (err: any) {
				handleSubquestLaunchCompactionFailure(pi, c, currentSessionState, err, childName, true, targetCompactionId);
			}
		});
	}, 50);
}

function handleCriticalReadyCompaction(
	pi: ExtensionAPI,
	c: ExtensionContext,
	activeQuest: string,
	tokens: number,
	threshold: number,
	isPressureTransition: boolean,
): boolean {
	if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
		retryPendingResume(pi, c);
		if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
			logDebug("Quest Journal: postponing new compaction because previous resume obligation is still pending delivery.");
			return false;
		}
	}

	const text = buildCriticalCompactionReadyPrompt(activeQuest, tokens, threshold);
	sendInternalAgentMessage(pi, text, "steer");
	logCompactionTransition("COMPACTION_PREPARED", "compaction ready critical steer emitted", {
		quest: activeQuest,
		type: "compaction_ready_critical",
	});

	const tx = createOrGetCompactionTransaction(state, "normal-compaction");
	tx.phase = "in-flight";
	state.compactionPending = true;
	state.preCompactionCheckpointPending = false;
	state.preCompactionSaveRequestPending = false;
	persist(pi, c);

	if (isPressureTransition && c.hasUI) {
		c.ui.notify(
			`Quest-journal: CRITICAL context pressure (${formatTokens(tokens)}/${formatTokens(threshold)}) for '${activeQuest}' [saved & ready for compaction].`,
			"info",
		);
	}

	if (typeof c.compact === "function") {
		const instructions = getCompactionInstructions(activeQuest, tokens, threshold);
		const targetSessionId = getSessionId(c);
		scheduleEconomyCompaction(pi, c, targetSessionId, instructions);
	}
	return true;
}

export function handleCriticalCompactionPressure(
	pi: ExtensionAPI,
	c: ExtensionContext,
	activeQuest: string,
	tokens: number,
	threshold: number,
	isReady: boolean,
	isPressureTransition: boolean,
): boolean {
	if (isReady) {
		return handleCriticalReadyCompaction(pi, c, activeQuest, tokens, threshold, isPressureTransition);
	}

	state.preCompactionCheckpointPending = true;
	state.preCompactionSaveRequestPending = true;
	const text = buildCriticalSavePrompt(activeQuest, tokens, threshold);

	sendInternalAgentMessage(pi, text, "steer");
	logCompactionTransition("COMPACTION_BLOCKED", "checkpoint required critical steer emitted", {
		quest: activeQuest,
		type: "checkpoint_required_critical",
	});

	if (isPressureTransition && c.hasUI) {
		c.ui.notify(
			`Quest-journal: CRITICAL context pressure (${formatTokens(tokens)}/${formatTokens(threshold)}) for '${activeQuest}' [SAVE REQUIRED IMMEDIATELY].`,
			"error",
		);
	}
	return true;
}

export function handleWarningCompactionPressure(
	pi: ExtensionAPI,
	c: ExtensionContext,
	activeQuest: string,
	tokens: number,
	threshold: number,
	fraction: number,
	isPressureTransition: boolean,
): boolean {
	state.preCompactionCheckpointPending = true;
	const text = buildWarningSavePrompt(activeQuest, fraction, tokens, threshold);

	sendInternalAgentMessage(pi, text, "steer");
	logAgentMessageTransition("AGENT_MESSAGE_DELIVERED", "compaction warning prompt", {
		quest: activeQuest,
		type: "compaction_warning",
		deliverAs: "steer",
	});

	if (isPressureTransition && c.hasUI) {
		const levelStr = fraction < 0.5 ? "approaching" : "close to";
		c.ui.notify(
			`Quest-journal: context ${levelStr} compaction threshold for '${activeQuest}' (${formatTokens(tokens)}/${formatTokens(threshold)}).`,
			"warning",
		);
	}
	return true;
}

export function requestPreCompactionCheckpoint(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	force = false,
	triggerSource: "turn_end" | "context" | "manual" = "turn_end",
): boolean {
	const c = getActiveContext(ctx);
	if (!c || !state.active) return false;
	if (state.compactionPending) return false;
	if (state.pickerCancelled) return false;

	const now = Date.now();
	if (!force && now - lastPreCompactionSteerTime < 50) {
		return false;
	}

	const { pressure, tokens, threshold, fraction } = getCompactionPressure(c);
	if (pressure === CompactionPressure.NONE || tokens === null) {
		if (state.lastNotifiedPressure !== CompactionPressure.NONE) {
			state.lastNotifiedPressure = CompactionPressure.NONE;
			resetSteeredTrackingState();
			persist(pi, c);
		}
		return false;
	}

	const isReady = compactionReady();
	const isPressureTransition = state.lastNotifiedPressure !== pressure;
	const isReadinessTransition = lastSteeredReadyState !== null && lastSteeredReadyState !== isReady;
	const isNewTurn = lastSteeredTurn !== lastSteerTurnCounter;

	if (triggerSource === "context" && !force && !isPressureTransition && !isReadinessTransition && !isNewTurn) {
		return false;
	}

	lastPreCompactionSteerTime = now;
	lastSteeredTurn = lastSteerTurnCounter;
	lastSteeredPressureState = pressure;
	lastSteeredReadyState = isReady;
	state.lastWarnedCompactionTokens = tokens;

	if (isPressureTransition) {
		state.lastNotifiedPressure = pressure;
	}
	persist(pi, c);

	if (pressure === CompactionPressure.CRITICAL) {
		return handleCriticalCompactionPressure(pi, c, state.active, tokens, threshold, isReady, isPressureTransition);
	}
	if (pressure === CompactionPressure.WARNING) {
		return handleWarningCompactionPressure(pi, c, state.active, tokens, threshold, fraction, isPressureTransition);
	}
	return false;
}

function triggerDeferredArchiveCompaction(pi: ExtensionAPI, c: ExtensionContext, targetName: string): boolean {
	const tx = createOrGetCompactionTransaction(state, "archive-compaction", state.active || targetName);
	tx.phase = "in-flight";
	state.compactionPending = true;
	persist(pi, c);

	const targetSessionId = getSessionId(c);
	const parentName = state.active;
	const parentPath = parentName ? questPath(state.questId) : "";
	const instructions = parentName
		? `Sub-quest '${targetName}' completed and archived. Returning to parent quest '${parentName}'. Focus summary on key architecture decisions, completed sub-quest findings, and remaining parent roadmap. Parent quest state is safely preserved on disk in ${parentPath}. Following compaction, read ${parentPath} first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified parent action.`
		: `Quest '${targetName}' completed and archived. Focus summary on key architecture decisions, completed work, and remaining roadmap.`;

	scheduleArchiveCompaction(pi, c, targetSessionId, instructions, parentName);
	return true;
}

function triggerDeferredSubquestLaunchCompaction(pi: ExtensionAPI, c: ExtensionContext, childName: string): boolean {
	const subLaunchThreshold = getSubquestCompactThreshold();
	const tokens = calculateCurrentTokens(c);

	if (!childName || !compactionReady(childName) || subLaunchThreshold <= 0 || tokens === null || tokens < subLaunchThreshold) {
		return false;
	}

	const tx = createOrGetCompactionTransaction(state, "subquest-launch", childName);
	tx.phase = "in-flight";
	state.compactionPending = true;
	persist(pi, c);

	const isSubQuest = Array.isArray(state.stack) && state.stack.length > 1;
	const parentName = isSubQuest ? state.stack[state.stack.length - 2] : null;
	const childPath = questPath(state.questId);
	const parentPath = parentName ? questPath(state.questId) : "";
	const instructions = parentName
		? `Launching sub-quest '${childName}' (parent: '${parentName}'). Focus summary on parent quest status, key architectural decisions, and why sub-quest '${childName}' was launched. Child sub-quest state is safely saved on disk in ${childPath}. Following compaction, read ${childPath} first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified next action.`
		: `Launching sub-quest '${childName}'. Focus summary on key architectural decisions and why sub-quest '${childName}' was launched. Child sub-quest state is safely saved on disk in ${childPath}. Following compaction, read ${childPath} first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified next action.`;

	const targetSessionId = getSessionId(c);
	scheduleSubquestLaunchCompaction(pi, c, targetSessionId, instructions, childName);
	return true;
}

export function checkAndTriggerDeferredCompaction(pi: ExtensionAPI, ctx?: ExtensionContext): boolean {
	const c = getActiveContext(ctx);
	if (!c || state.pickerCancelled || state.compactionPending || typeof c.compact !== "function") {
		return false;
	}

	if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
		retryPendingResume(pi, c);
		if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
			logDebug("Quest Journal: postponing deferred compaction because previous resume obligation is still pending delivery.");
			return false;
		}
	}

	if (state.archiveCompactionPending) {
		return triggerDeferredArchiveCompaction(pi, c, state.archiveCompactionPending);
	}

	if (state.subquestLaunchCompactionPending && state.active) {
		return triggerDeferredSubquestLaunchCompaction(pi, c, state.active);
	}

	return false;
}
