import { basename } from "node:path";
import { acceptRootConfirmation, classifyUserMessage, handleAskQuestionsResult } from "../classification.ts";
import {
	advanceSteerTurnCounter,
	checkAndTriggerDeferredCompaction,
	compactionReady,
	createOrGetCompactionTransaction,
	dispatchCompactionResume,
	drainPendingResumesAndNotifications,
	handleCompactionCompleted,
	requestPeriodicCheckpoint,
	retryPendingResume,
} from "../compaction.ts";
import { checkAndTriggerDirectionReview } from "../critical_agent.ts";
import { findActiveReviewForQuest, getActiveReviews, getPendingReview } from "../critical_agent/tracker.ts";
import { PROMPT_MAX_CHARS, PROMPT_MAX_COUNT, QUEST_CURRENT_DIR, SUBSTANTIVE_TURNS_PER_DIRECTION_REVIEW } from "../constants.ts";
import { ensureRootQuestForPrompt } from "../lifecycle.ts";
import {
	logCompactionTransition,
	logContinuationAnomaly,
	logEvent,
	logImplementationOutcome,
	logResumeTransition,
	logToolActivity,
	logTurnBoundary,
	logUserInteraction,
	normalizeLogPath,
	sanitizeLogString,
} from "../logging.ts";
import { getWorkflowInstructions } from "../markdown.ts";
import { logDebug, logError, reportAgentError, sendInternalAgentMessage, shouldCapturePrompt } from "../messaging.ts";
import { questPath, shouldStartPersistentQuest } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadActiveQuestResumeContext } from "../reconstruction.ts";
import { recordObservedInvestigation, triggerReassessment } from "../research.ts";
import { getActiveContext, getSessionId, getState, sessionStates, state } from "../state.ts";
import { reconcilePendingSubquestResume } from "../subquest.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode, UserMessageClassification } from "../types.ts";
import { buildSessionAwarenessBlock, updateUIStatus } from "../ui.ts";
import { classifyToolCall, normalizePath } from "../utils.ts";
import { analyzeTurnToolResults, applyTurnEndStateTransitions, classifyActivityPhase, detectBashToolFailure } from "./turn_analysis.ts";

export async function handleTurnStart(event: any, _ctx: ExtensionContext): Promise<void> {
	if (state.pickerCancelled) return;
	if (!state.active && !state.pendingRootQuest && !state.activeDraft) return;

	const turnIndex = typeof event?.turnIndex === "number" ? event.turnIndex : (state.currentTurn || 0) + 1;
	state.currentTurn = turnIndex;
	state.currentTurnCorrelationId = `turn_${turnIndex}_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;

	const intent = state.prompts && state.prompts.length > 0 ? state.prompts[state.prompts.length - 1] : state.pendingRootRequest || state.active || "";
	const activeGate = state.reassessmentRequired ? "REASSESSMENT_PENDING" : (state.researchRequired || !state.researchComplete ? "RESEARCH_PENDING" : (state.awaitingUserConfirmation ? "CONFIRMATION_PENDING" : "IMPLEMENTATION_ALLOWED"));
	const phase = state.reassessmentRequired ? "reassessment" : (state.researchRequired || !state.researchComplete ? "research" : (state.awaitingUserConfirmation ? "confirmation" : "implementation"));

	logTurnBoundary("TURN_START", "agent turn started", {
		quest: state.active || "",
		turn: state.currentTurn,
		correlationId: state.currentTurnCorrelationId,
		intent: sanitizeLogString(intent, 200),
		phase,
		activeGate,
		planVersion: state.planVersion || 1,
		round: state.researchRound || 1,
		implementationAllowed: Boolean(state.implementationAllowed),
	});
}

export async function handleTurnEnd(pi: ExtensionAPI, ctx: ExtensionContext, event: any): Promise<void> {
	if (state.pickerCancelled) return;

	drainPendingResumesAndNotifications(pi, ctx);
	// Turn-stop steer for awaitingReview (A: plan_review/final_acceptance only)
	try {
		const c = getActiveContext(ctx);
		const targetSessionId = getSessionId(c);
		const targetState = sessionStates.get(targetSessionId) ?? getState(c);
		const aw = (targetState as any).awaitingReview as { kind: string; reviewId: string; triggerReason?: string } | null | undefined;
		if (aw && (aw.kind === "plan_review" || aw.kind === "final_acceptance")) {
			sendInternalAgentMessage(pi, `⏸ Awaiting ${aw.kind}/${aw.triggerReason || aw.kind} ${aw.reviewId} — verdict pending. No writes until verdict; reads and quest_mark_saved allowed.`, "steer");
		}
	} catch {}

	if (state.compactionPending) return;

	if (state.archiveCompactionPending) {
		checkAndTriggerDeferredCompaction(pi, ctx);
		return;
	}

	if (!state.active && !state.pendingRootQuest && !state.activeDraft) return;

	if (typeof event?.turnIndex === "number") {
		state.currentTurn = event.turnIndex;
	}
	if (!state.currentTurnCorrelationId) {
		state.currentTurnCorrelationId = `turn_${state.currentTurn || 1}_${Date.now().toString(36)}`;
	}

	const toolResults: any[] = Array.isArray(event.toolResults) ? event.toolResults : [];
	const analysis = analyzeTurnToolResults(toolResults, state.active || state.activeDraft || "");
	applyTurnEndStateTransitions(state, analysis, pi, ctx);

	const readsCount = toolResults.filter((tr: any) => {
		const name = (tr?.toolName || tr?.name || "").toLowerCase();
		return name === "read" || name === "doc_to_md" || name === "memory_read" || name === "memory_status";
	}).length;

	const searchesCount = toolResults.filter((tr: any) => {
		const name = (tr?.toolName || tr?.name || "").toLowerCase();
		return (
			name === "search_graph" ||
			name === "query_graph" ||
			name === "trace_path" ||
			name === "get_code_snippet" ||
			name === "search_code" ||
			name === "get_graph_schema" ||
			name === "get_architecture" ||
			name === "web_search" ||
			name === "source_check" ||
			name === "memory_search" ||
			name === "fetch_content" ||
			name === "get_search_content"
		);
	}).length;

	const writesCount = toolResults.filter((tr: any) => {
		const name = (tr?.toolName || tr?.name || "").toLowerCase();
		return name === "edit" || name === "write" || name === "user_edit" || name === "user_write";
	}).length;

	const commandsCount = toolResults.filter((tr: any) => {
		const name = (tr?.toolName || tr?.name || "").toLowerCase();
		return name === "bash" || name === "user_bash" || name.startsWith("bg_run");
	}).length;

	const mutationsCount = toolResults.filter((tr: any) => {
		const name = (tr?.toolName || tr?.name || "").toLowerCase();
		return name === "edit" || name === "write" || name === "bash" || name === "user_bash";
	}).length;
	const failuresCount = analysis.failureCount;

	let turnConsequence: string | undefined = undefined;
	if (analysis.meaningfulFailureDetected) {
		turnConsequence = "TRIGGERED_REASSESSMENT";
	} else if (analysis.didUpdateQuestThisTurn) {
		turnConsequence = "CHECKPOINT_SAVED";
	}

	logTurnBoundary("TURN_END", "turn execution completed", {
		quest: state.active || "",
		turn: state.currentTurn,
		correlationId: state.currentTurnCorrelationId,
		substantive: analysis.isSubstantiveTurn,
		toolsUsed: toolResults.length,
		reads: readsCount,
		searches: searchesCount,
		writes: writesCount,
		commands: commandsCount,
		mutations: mutationsCount,
		failures: failuresCount,
		filesModified: Array.isArray(state.sessionModifiedFiles) && state.sessionModifiedFiles.length > 0 ? state.sessionModifiedFiles.slice(-5).join(",") : undefined,
		consequence: turnConsequence,
		activeGate: state.reassessmentRequired ? "REASSESSMENT_PENDING" : (state.researchRequired || !state.researchComplete ? "RESEARCH_PENDING" : (state.awaitingUserConfirmation ? "CONFIRMATION_PENDING" : "IMPLEMENTATION_ALLOWED")),
		categories: analysis.failureCategories.length > 0 ? analysis.failureCategories.join(",") : undefined,
		questDirty: Boolean(state.dirty),
		implementationAllowed: Boolean(state.implementationAllowed),
	});

	if (state.consecutiveFailures === 3) {
		logContinuationAnomaly("REPEATED_FAILURE", `consecutive failures reached threshold (count=3)`, {
			quest: state.active || "",
			count: 3,
		});
	}
	if ((state.substantiveTurnsSinceCheckpoint || 0) >= SUBSTANTIVE_TURNS_PER_DIRECTION_REVIEW) {
		logContinuationAnomaly("NO_PROGRESS", `turns without state checkpoint reached threshold (turns=${state.substantiveTurnsSinceCheckpoint})`, {
			quest: state.active || "",
			turns: state.substantiveTurnsSinceCheckpoint,
		});
		// Plan-block throttle at handler layer to avoid even entering review launch path
		const hasActivePlan =
			(state.active && findActiveReviewForQuest(state.active)?.kind === "plan_review") ||
			[...getActiveReviews().values()].some((r) => r.kind === "plan_review" && (r.status === "starting" || r.status === "running"));
		const hasPendingPlan = state.active ? !!getPendingReview(state.active, "plan_review") : false;
		if (hasActivePlan || hasPendingPlan) {
			logEvent("DIRECTION_REVIEW_THROTTLED", `direction review throttled at handler (plan_review active/pending)`, {
				quest: state.active || "",
				triggerReason: "no_progress",
				reason: hasActivePlan ? "plan_review_active" : "plan_review_pending",
			});
			state.substantiveTurnsSinceCheckpoint = 0;
		} else {
			await checkAndTriggerDirectionReview(pi, ctx, "no_progress");
		}
	}

	advanceSteerTurnCounter();
	requestPeriodicCheckpoint(pi, ctx, false);
	checkAndTriggerDeferredCompaction(pi, ctx);
}

export async function handleToolResult(event: any, _ctx: ExtensionContext): Promise<void> {
	if (!state.active && !state.pendingRootQuest && !state.activeDraft) return;
	const toolName = event?.toolName || event?.name || "";
	const toolInput = event?.input || event?.args || {};
	const toolOutput = event?.content || event?.output || "";
	const rawIsError = Boolean(event?.isError || event?.error || (event?.details && (event?.details?.error || event?.details?.success === false)));
	const normName = (toolName || "").toLowerCase().trim();
	// Whitelist rg/grep exit 1 (no matches) — not an error, still counts as investigation
	let effectiveIsError = rawIsError;
	if ((normName === "bash" || normName === "user_bash") && rawIsError) {
		const bashFailure = detectBashToolFailure(event);
		if (!bashFailure.hasFailure) {
			effectiveIsError = false;
		}
	}
	recordObservedInvestigation(state, toolName, toolInput, toolOutput, effectiveIsError);

	const activeQuest = state.active || "";

	let isFailure = effectiveIsError;
	let failureReason: string | undefined = undefined;

	if (normName === "bash" || normName === "user_bash") {
		const bashFailure = detectBashToolFailure(event);
		if (bashFailure.hasFailure) {
			isFailure = true;
			failureReason = bashFailure.reason;
		} else {
			// Whitelisted search no-match — ensure not treated as failure even if rawIsError was true
			isFailure = false;
		}
	} else if (effectiveIsError) {
		failureReason = event?.error?.message || event?.message || (event?.details && event.details.error) || "tool execution error";
	}

	const operation = isFailure ? "failure" : "success";
	const phase = classifyActivityPhase(normName, toolInput, state, isFailure);

	let targetPath: string | undefined = undefined;
	let command: string | undefined = undefined;
	let query: string | undefined = undefined;
	let filesModified: string | undefined = undefined;
	let failureId: string | undefined = undefined;
	let consequence: string | undefined = undefined;
	let recoveryFor: string | undefined = undefined;

	if (normName === "read" || normName === "edit" || normName === "write" || normName === "user_edit" || normName === "user_write" || normName === "doc_to_md") {
		targetPath = normalizeLogPath(typeof toolInput === "string" ? toolInput : toolInput?.path || toolInput?.file || "");
		if ((normName === "edit" || normName === "write" || normName === "user_edit" || normName === "user_write") && !isFailure && targetPath) {
			filesModified = targetPath;
		}
	} else if (normName === "bash" || normName === "user_bash" || normName.startsWith("bg_run")) {
		const rawCmd = typeof toolInput === "string" ? toolInput : toolInput?.command || toolInput?.cmd || "";
		command = sanitizeLogString(rawCmd, 150);
	} else if (normName === "search_graph" || normName === "search_code" || normName === "web_search" || normName === "source_check") {
		query = sanitizeLogString(typeof toolInput === "string" ? toolInput : toolInput?.query || toolInput?.name_pattern || toolInput?.name || toolInput?.pattern || "");
	}

	if (isFailure) {
		failureId = `fail_${state.currentTurn || 1}_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;
		(state as any).lastFailureId = failureId;
		consequence = "FAILURE_RECORDED";
	} else if ((state as any).lastFailureId) {
		recoveryFor = (state as any).lastFailureId;
	}

	logToolActivity(toolName, operation, {
		quest: activeQuest,
		phase,
		path: targetPath,
		command,
		query,
		turn: state.currentTurn,
		correlationId: state.currentTurnCorrelationId,
		filesModified,
		failureId,
		consequence,
		recoveryFor,
		reason: failureReason,
	});
}

export function recordCompactionFailureState(
	sessionState: any,
	errorMsg: string,
): void {
	sessionState.compactionPending = false;
	if (sessionState.activeTransaction) {
		sessionState.activeTransaction.phase = "failed";
		sessionState.activeTransaction.failedAt = Date.now();
		sessionState.activeTransaction.error = errorMsg;
	}
	sessionState.activeCompactionId = null;
	sessionState.archiveCompactionPending = null;
	sessionState.subquestLaunchCompactionPending = false;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;
}

export function reportCompactionFailure(
	pi: ExtensionAPI,
	c: ExtensionContext | undefined,
	sessionState: any,
	errorMsg: string,
): void {
	logCompactionTransition("COMPACTION_FAILED", "compaction failed", {
		quest: sessionState.active || "",
		reason: errorMsg,
	});

	if (c?.hasUI) {
		c.ui.notify(`Session context compaction failed: ${errorMsg}`, "error");
	}

	reportAgentError(
		pi,
		c,
		`Context compaction failed: ${errorMsg}`,
		{
			code: QuestErrorCode.COMPACTION_FAILURE,
			requiredNextAction: sessionState.active
				? `Read ${questPath(sessionState.questId) || `.pi/quest/current/${sessionState.questId || "<qid>"}/quest.md`} and continue execution. Compaction will be re-attempted when context pressure warrants.`
				: "Review active memory and continue execution.",
			details: { ActiveQuest: sessionState.active || "(none)" },
		},
	);
}

export async function handleCompactionFailureResume(
	pi: ExtensionAPI,
	c: ExtensionContext | undefined,
	sessionState: any,
): Promise<void> {
	if (sessionState.pendingSubquestResume) {
		const subquestStatus = await reconcilePendingSubquestResume(sessionState.pendingSubquestResume, sessionState, pi, c);
		if (subquestStatus === "still-valid") {
			const childName = sessionState.pendingSubquestResume;
			dispatchCompactionResume(pi, {
				questName: childName,
				reason: "compaction-failure-fallback",
				ctx: c,
			});
			return;
		} else if (subquestStatus === "inconsistent") {
			return;
		}
	}
	if (sessionState.active) {
		dispatchCompactionResume(pi, {
			questName: sessionState.active,
			reason: "compaction-failure-fallback",
			ctx: c,
		});
	}
}

export async function handleCompactionFailure(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	event: any,
): Promise<void> {
	const c = getActiveContext(ctx);
	const targetSessionId = getSessionId(c);
	const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
	if (!sessionState.active) {
		sessionState.activeTransaction = null;
		sessionState.activeCompactionId = null;
		sessionState.pendingResume = null;
		sessionState.archiveCompactionPending = null;
		sessionState.compactionPending = false;
		return;
	}
	const errorMsg = event?.error?.message || event?.message || "Session context compaction failed. Context has not been compacted.";

	recordCompactionFailureState(sessionState, errorMsg);
	reportCompactionFailure(pi, c, sessionState, errorMsg);
	await handleCompactionFailureResume(pi, c, sessionState);
}
