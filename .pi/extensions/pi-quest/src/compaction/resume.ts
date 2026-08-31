import { INTERNAL_MESSAGE_PREFIX } from "../constants.ts";
import { syncImplementationPermission } from "../gates.ts";
import { logEvent, logResumeTransition } from "../logging.ts";
import { readQuestLog } from "../logging/summary/helpers.ts";
import { drainPendingAgentNotifications, logDebug, logError, reportAgentError, safeSendUserMessage, sendInternalAgentMessage } from "../messaging.ts";
import { questPath } from "../paths.ts";
import { persist } from "../persistence.ts";
import { getActiveContext, getSessionId, getState, isRootQuest, sessionStates } from "../state.ts";
import { CompactionTransaction, ExtensionAPI, ExtensionContext, PendingResume, QuestErrorCode, ResumeReason, StoredState } from "../types.ts";

function resolveChildStateAction(targetState: StoredState, childName: string): string {
	const qp = questPath(targetState.questId);
	if (targetState.reassessmentRequired) {
		return `⚡ **State: REASSESSMENT_PENDING** (Reason: ${targetState.reassessmentReason || "Unresolved contradiction"}).
1. Read \`${qp}\` using \`read\`.
2. Do NOT jump into implementation. First investigate the contradiction, challenge previous assumptions, and evaluate whether the child plan is still valid.
3. Update the quest file with the revised plan and call \`quest_update_state({ reassessmentComplete: true })\`.
4. Proceed with the revised Exact Next Action.`;
	}
	if (targetState.researchRequired || !targetState.researchComplete) {
		return `⚡ **State: RESEARCH_PENDING** (Research Round: ${targetState.researchRound || 1}).
1. Read \`${qp}\` using \`read\` to inspect inherited context and goal.
2. Independently investigate the relevant subsystem, execution paths, and dependencies.
3. Identify assumptions inherited or required, and test high-risk assumptions directly.
4. Formulate a provisional plan, challenge it against potential failure modes, and revise if needed.
5. Update \`${qp}\` (Current Understanding, Key Assumptions, Plan, Plan Confidence, Exact Next Action) and call \`quest_update_state({ researchComplete: true })\`.
6. Once the child research gate is satisfied, continue autonomously with implementation without waiting for user confirmation.`;
	}
	if (isRootQuest(targetState) && targetState.awaitingUserConfirmation) {
		return `⚡ **State: CONFIRMATION_PENDING**.
1. Read \`${qp}\` using \`read\`.
2. Present your research findings, tested assumptions, and proposed plan clearly to the user.
3. Await user confirmation before modifying project code.`;
	}
	return `⚡ **State: PLAN_ESTABLISHED** (Plan: v${targetState.planVersion || 1}, Confidence: ${targetState.planConfidence || "high"}).
1. Read \`${qp}\` using \`read\`.
2. Validate whether the current plan is still supported by the recovered state.
3. Proceed directly with executing the justified EXACT NEXT ACTION without waiting for user commands.`;
}

export function buildSubquestLaunchPostCompactionDirective(
	childName: string,
	targetState: StoredState,
): string {
	const isSubQuest = Array.isArray(targetState.stack) && targetState.stack.length > 1;
	const parentQuest = isSubQuest ? targetState.stack[targetState.stack.length - 2] : null;
	const parentInfo = parentQuest ? ` (parent: **${parentQuest}**)` : "";
	const specificAction = resolveChildStateAction(targetState, childName);

	const qp = questPath(targetState.questId);
	return `⚡ **Post-Compaction Autonomous Resumption Directive**:
Compaction finished after launching sub-quest \`${childName}\`${parentInfo}.

You are now working on that sub-quest.

The single authoritative source of truth on disk is \`${qp}\`.
Sub-quests do NOT inherit parent conclusions as immutable facts; treat them as hypotheses to independently verify.

Read ${qp}.
Independently verify inherited context.
Perform the child's required research.
Once the child research gate is satisfied, continue autonomously.
Do not return to the parent until this sub-quest is actually complete.

**Action Required Now**:
${specificAction}`;
}

function resolveFallbackStateAction(targetState: StoredState, activeQuest: string): string {
	const qp = questPath(targetState.questId);
	if (targetState.reassessmentRequired) {
		return `⚡ **State: REASSESSMENT_PENDING** (Reason: ${targetState.reassessmentReason || "Unresolved contradiction"}).
1. Read \`${qp}\` using \`read\`.
2. Investigate the contradiction, update the quest file with the revised plan, and call \`quest_update_state({ reassessmentComplete: true })\`.
3. Proceed with the revised Exact Next Action.`;
	}
	if (targetState.researchRequired || !targetState.researchComplete) {
		return `⚡ **State: RESEARCH_PENDING** (Research Round: ${targetState.researchRound || 1}).
1. Read \`${qp}\` using \`read\`.
2. Complete the required research & falsification pass before writing feature code.
3. Update \`${qp}\` and call \`quest_update_state({ researchComplete: true })\`.
4. Proceed to implementation / confirmation once the research gate is satisfied.`;
	}
	if (isRootQuest(targetState) && targetState.awaitingUserConfirmation) {
		return `⚡ **State: CONFIRMATION_PENDING**.
1. Read \`${qp}\` using \`read\`.
2. Present your research findings and proposed plan to the user.
3. Await user confirmation before modifying project code.`;
	}
	return `⚡ **State: PLAN_ESTABLISHED** (Plan: v${targetState.planVersion || 1}, Confidence: ${targetState.planConfidence || "high"}).
1. Read \`${qp}\` using \`read\`.
2. Validate whether the current plan is still supported by current state.
3. Proceed directly with executing the justified EXACT NEXT ACTION.`;
}

export function buildCompactionFallbackResumeDirective(
	activeQuest: string,
	targetState: StoredState,
): string {
	const isSubQuest = Array.isArray(targetState.stack) && targetState.stack.length > 1;
	const parentQuest = isSubQuest ? targetState.stack[targetState.stack.length - 2] : null;

	const subquestContext = isSubQuest
		? `You are inside sub-quest **${activeQuest}** (parent: **${parentQuest}**).
This sub-quest is temporary work in service of the parent/root objective.`
		: `You are working on active quest **${activeQuest}**.`;

	const specificAction = resolveFallbackStateAction(targetState, activeQuest);

	const qp = questPath(targetState.questId);
	return `⚡ **Autonomous Resumption Directive (Compaction Skipped / Fallback)**:
Context compaction was skipped or failed. Working memory continues from durable quest state.

${subquestContext}
The single authoritative source of truth on disk is \`${qp}\`.

**Action Required Now**:
${specificAction}`;
}

function getRecentLogTail(targetState: StoredState): string {
	try {
		const raw = readQuestLog(targetState.questId || "", 10);
		if (!raw) return "";
		const trimmed = raw.slice(-1200);
		return `\n\n**Recent execution log tail (last 10):**\n\`\`\`\n${trimmed}\n\`\`\``;
	} catch {
		return "";
	}
}

function resolvePostCompactionStateAction(targetState: StoredState, activeQuest: string): string {
	const qp = questPath(targetState.questId);
	const logTail = targetState.reassessmentRequired ? getRecentLogTail(targetState) : "";
	if (targetState.reassessmentRequired) {
		return `⚡ **State: REASSESSMENT_PENDING** (Reason: ${targetState.reassessmentReason || "Unresolved contradiction"}).
1. Read \`${qp}\` using \`read\`.
2. Do NOT jump into implementation. First investigate the contradiction, challenge previous assumptions, and evaluate whether the current plan is still valid.
3. Update the quest file with the revised plan and call \`quest_update_state({ reassessmentComplete: true })\`.
4. Proceed with the revised Exact Next Action.${logTail}`;
	}
	if (targetState.researchRequired || !targetState.researchComplete) {
		return `⚡ **State: RESEARCH_PENDING** (Research Round: ${targetState.researchRound || 1}).
1. Read \`${qp}\` using \`read\`.
2. Complete the required research & falsification pass before writing feature code.
3. Establish Current Understanding, Key Assumptions, and provisional Plan with medium/high confidence.
4. Update the quest file and call \`quest_update_state({ researchComplete: true })\`.
5. Only then proceed to implementation / confirmation.`;
	}
	if (isRootQuest(targetState) && targetState.awaitingUserConfirmation) {
		return `⚡ **State: CONFIRMATION_PENDING**.
1. Read \`${qp}\` using \`read\`.
2. Present your research findings, tested assumptions, and proposed plan clearly to the user (using ask_questions or a plain text question).
3. Await user confirmation before modifying project code.`;
	}
	return `⚡ **State: PLAN_ESTABLISHED** (Plan: v${targetState.planVersion || 1}, Confidence: ${targetState.planConfidence || "high"}).
1. Read \`${qp}\` using \`read\`.
2. Validate whether the current plan is still supported by the recovered state. Do not repeat research merely to reconstruct lost context; use the quest file to recover established knowledge. However, if an important assumption is uncertain, tests disagree with the model, or the plan no longer explains observed behavior, re-investigate that specific aspect before executing.
3. If the plan is established and supported, proceed directly with executing the justified EXACT NEXT ACTION without waiting for user commands and without modal questions.`;
}

export function buildPostCompactionResumeDirective(
	activeQuest: string,
	targetState: StoredState,
): string {
	const isSubQuest = Array.isArray(targetState.stack) && targetState.stack.length > 1;
	const parentQuest = isSubQuest ? targetState.stack[targetState.stack.length - 2] : null;

	const subquestContext = isSubQuest
		? `You are inside sub-quest **${activeQuest}** (parent: **${parentQuest}**).
This sub-quest is temporary work in service of the parent/root objective. Completing this sub-quest does not mean the overall objective is complete. After finishing it, return to the parent quest and continue its remaining work.`
		: `You are working on active quest **${activeQuest}**.`;

	const specificAction = resolvePostCompactionStateAction(targetState, activeQuest);
	const qp = questPath(targetState.questId);

	return `⚡ **Post-Compaction Autonomous Resumption Directive**:
Context compaction has finished. Working memory has been cleanly reset.

${subquestContext}
The single authoritative source of truth on disk is \`${qp}\`.

**Action Required Now**:
${specificAction}`;
}

export function resolveResumeDirectiveText(
	reason: ResumeReason,
	activeQuest: string,
	targetState: StoredState,
): string {
	if (reason === "subquest-launch") {
		return buildSubquestLaunchPostCompactionDirective(activeQuest, targetState);
	}
	if (reason === "compaction-failure-fallback") {
		return buildCompactionFallbackResumeDirective(activeQuest, targetState);
	}
	return buildPostCompactionResumeDirective(activeQuest, targetState);
}

export function createCompactionResumeObligation(
	tx: CompactionTransaction,
	sessionState: StoredState,
	activeQuest: string,
	reason: ResumeReason,
): PendingResume {
	return {
		compactionId: tx.id,
		activeQuest,
		reason,
		checkpointSaveCount: tx.checkpointSaveCount ?? -1,
		checkpointHash: tx.checkpointHash ?? "",
		checkpointQuestPath: tx.questPath ?? (activeQuest ? questPath(activeQuest) : ""),
		attempts: sessionState.pendingResume?.compactionId === tx.id ? (sessionState.pendingResume.attempts || 0) : 0,
		createdAt: sessionState.pendingResume?.compactionId === tx.id ? sessionState.pendingResume.createdAt : Date.now(),
	};
}

export function recordResumeDeliverySuccess(
	targetState: StoredState,
	compactionId: string,
	activeQuest: string,
	reason: ResumeReason,
	pi: ExtensionAPI,
	c?: ExtensionContext,
): void {
	const now = Date.now();
	targetState.lastResumePromptAt = now;
	targetState.lastResumeTarget = activeQuest;
	targetState.lastResumeCompactCount = targetState.compactCount;
	targetState.lastDeliveredCompactionId = compactionId;
	targetState.activeCompactionId = null;
	targetState.pendingResume = null;
	if (targetState.activeTransaction && targetState.activeTransaction.phase !== "inconsistent") {
		if (reason === "compaction-failure-fallback" || targetState.activeTransaction.phase !== "failed") {
			targetState.activeTransaction.phase = "resume-delivered";
		}
	}
	if (targetState.pendingSubquestResume === activeQuest) {
		targetState.pendingSubquestResume = null;
	}
	persist(pi, c);
	syncImplementationPermission(targetState, c);
	logResumeTransition("RESUME_DELIVERED", `resume delivered for ${activeQuest}`, {
		quest: activeQuest,
		compactionId,
		planVersion: targetState.planVersion || 1,
		reason,
	});
}

export function recordResumeDeliveryFailure(
	targetState: StoredState,
	compactionId: string,
	activeQuest: string,
	reason: ResumeReason,
	pi: ExtensionAPI,
	c?: ExtensionContext,
): void {
	if (!targetState.pendingResume) {
		targetState.pendingResume = {
			compactionId,
			activeQuest,
			reason,
			checkpointSaveCount: targetState.activeTransaction?.checkpointSaveCount ?? -1,
			checkpointHash: targetState.activeTransaction?.checkpointHash ?? "",
			checkpointQuestPath: targetState.activeTransaction?.questPath ?? questPath(targetState.questId),
			attempts: 1,
			createdAt: Date.now(),
			lastAttemptAt: Date.now(),
		};
	} else {
		targetState.pendingResume.attempts = (targetState.pendingResume.attempts || 0) + 1;
		targetState.pendingResume.lastAttemptAt = Date.now();
	}
	if (
		reason !== "compaction-failure-fallback" &&
		targetState.activeTransaction &&
		targetState.activeTransaction.id === compactionId
	) {
		targetState.activeTransaction.phase = "resume-pending";
	}
	persist(pi, c);
	syncImplementationPermission(targetState, c);

	logResumeTransition("RESUME_FAILED", `resume delivery failed for ${activeQuest}`, {
		quest: activeQuest,
		compactionId,
		code: QuestErrorCode.CONTINUATION_FAILURE,
		reason,
		attempt: targetState.pendingResume?.attempts || 1,
	});
	logError(
		`Quest Journal: Failed to deliver post-compaction resume | compactionId=${compactionId} | reason=${reason} | quest=${activeQuest}`,
		undefined,
		c,
		QuestErrorCode.CONTINUATION_FAILURE,
	);
	const failureMessage = reason === "compaction-failure-fallback"
		? "Compaction failed, and the autonomous fallback resume directive could not be delivered.\n\nThe durable quest state remains authoritative."
		: "Compaction completed successfully, but the autonomous resume directive could not be delivered.\n\nThe durable quest state remains authoritative.";

	reportAgentError(
		pi,
		c,
		failureMessage,
		{
			code: QuestErrorCode.CONTINUATION_FAILURE,
			requiredNextAction: `Read ${questPath(targetState.questId)}, reconcile the recovered state if necessary, and continue from the recorded Exact Next Action.`,
			details: {
				CompactionId: compactionId,
				ActiveQuest: activeQuest,
				Reason: reason,
				Attempts: targetState.pendingResume?.attempts || 1,
			},
		},
	);
}

export function dispatchCompactionResume(
	pi: ExtensionAPI,
	options: {
		compactionId?: string;
		questName?: string;
		reason: ResumeReason;
		ctx?: ExtensionContext;
	},
): boolean {
	const c = getActiveContext(options.ctx);
	const targetSessionId = getSessionId(c);
	const targetState = sessionStates.get(targetSessionId) ?? getState(c);

	const activeQuest = targetState.active;
	if (!activeQuest) {
		logDebug("Quest Journal: dispatchCompactionResume skipped; no authoritative active quest.");
		return false;
	}

	const compactionId = options.compactionId || targetState.pendingResume?.compactionId || targetState.activeTransaction?.id || targetState.activeCompactionId || `cmp_${targetState.compactCount}`;

	if (
		options.reason !== "compaction-failure-fallback" &&
		targetState.lastDeliveredCompactionId &&
		targetState.lastDeliveredCompactionId === compactionId
	) {
		targetState.pendingResume = null;
		targetState.activeCompactionId = null;
		if (targetState.activeTransaction && targetState.activeTransaction.id === compactionId && targetState.activeTransaction.phase !== "inconsistent" && targetState.activeTransaction.phase !== "failed") {
			targetState.activeTransaction.phase = "resume-delivered";
		}
		return true;
	}

	const directiveText = resolveResumeDirectiveText(options.reason, activeQuest, targetState);
	logResumeTransition("RESUME_ATTEMPTED", `attempting resume delivery for '${activeQuest}'`, {
		quest: activeQuest,
		compactionId,
		reason: options.reason,
	});
	const delivered = sendInternalAgentMessage(pi, directiveText, "followUp");

	if (delivered) {
		recordResumeDeliverySuccess(targetState, compactionId, activeQuest, options.reason, pi, c);
		return true;
	}

	recordResumeDeliveryFailure(targetState, compactionId, activeQuest, options.reason, pi, c);
	return false;
}

export function retryPendingResume(pi: ExtensionAPI, ctx?: ExtensionContext): boolean {
	const c = getActiveContext(ctx);
	const targetSessionId = getSessionId(c);
	const targetState = sessionStates.get(targetSessionId) ?? getState(c);

	if (!targetState.pendingResume) return false;

	const { compactionId, reason } = targetState.pendingResume;
	const activeQuest = targetState.active;

	logResumeTransition("RESUME_RETRIED", `retrying pending resume for '${activeQuest || targetState.pendingResume.activeQuest}'`, {
		quest: activeQuest || targetState.pendingResume.activeQuest,
		compactionId,
		reason,
		attempt: (targetState.pendingResume.attempts || 0) + 1,
	});

	if (!activeQuest) {
		logDebug(`Quest Journal: Pending resume for '${targetState.pendingResume.activeQuest}' cannot be retried because there is no authoritative active quest.`);
		reportAgentError(
			pi,
			c,
			`[Quest Journal] A pending resume obligation exists for '${targetState.pendingResume.activeQuest}', but no authoritative active quest is currently set.\n\nUse /quest <name> to activate a quest or reconcile the session state.`,
			{
				code: QuestErrorCode.RESUME_DELIVERY_FAILURE,
				requiredNextAction: "Set an active quest with /quest <name> to resume execution.",
				details: {
					PendingQuest: targetState.pendingResume.activeQuest,
					CompactionId: compactionId,
				},
			},
		);
		return false;
	}

	return dispatchCompactionResume(pi, {
		compactionId,
		questName: activeQuest,
		reason,
		ctx: c,
	});
}

export function drainPendingResumesAndNotifications(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
): void {
	const c = getActiveContext(ctx);
	const targetSessionId = getSessionId(c);
	const targetState = sessionStates.get(targetSessionId) ?? getState(c);

	if (targetState.activeTransaction && targetState.activeTransaction.phase === "resume-delivered") {
		targetState.activeTransaction = null;
	}

	drainPendingAgentNotifications(pi, c);
	retryPendingResume(pi, c);
}

export function sendPostCompactionUserMessage(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	text: string = "",
): boolean {
	return safeSendUserMessage(pi, `${INTERNAL_MESSAGE_PREFIX}\n${text}`);
}

export function sendSubquestLaunchPostCompactionDirective(
	pi: ExtensionAPI,
	childName: string,
	targetState: StoredState,
	ctx?: ExtensionContext,
): boolean {
	return dispatchCompactionResume(pi, {
		questName: childName,
		reason: "subquest-launch",
		ctx,
	});
}

export function sendPostCompactionResumePrompt(
	pi: ExtensionAPI,
	activeQuest: string,
	isCompaction = true,
	ctx?: ExtensionContext,
): boolean {
	return dispatchCompactionResume(pi, {
		questName: activeQuest,
		reason: isCompaction ? "normal-compaction" : "compaction-failure-fallback",
		ctx,
	});
}
