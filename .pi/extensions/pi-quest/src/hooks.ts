import { basename } from "node:path";
import { acceptRootConfirmation, classifyUserMessage, handleAskQuestionsResult } from "./classification.ts";
import { advanceSteerTurnCounter, checkAndTriggerDeferredCompaction, compactionReady, createOrGetCompactionTransaction, dispatchCompactionResume, drainPendingResumesAndNotifications, handleCompactionCompleted, requestPreCompactionCheckpoint, retryPendingResume, sendPostCompactionResumePrompt, sendSubquestLaunchPostCompactionDirective } from "./compaction.ts";
import { checkAndTriggerDirectionReview } from "./critical_agent.ts";
import { PROMPT_MAX_CHARS, PROMPT_MAX_COUNT, QUEST_CURRENT_DIR } from "./constants.ts";
import { withContext } from "./context.ts";
import { ensureRootQuestForPrompt } from "./lifecycle.ts";
import {
	logCompactionTransition,
	logContinuationAnomaly,
	logEvent,
	logImplementationOutcome,
	logResumeTransition,
	logToolActivity,
	logToolFailure,
	logTurnBoundary,
	logUserInteraction,
	logVerificationTransition,
	normalizeLogPath,
	sanitizeLogString,
} from "./logging.ts";
import { getWorkflowInstructions } from "./markdown.ts";
import { logDebug, logError, reportAgentError, sendInternalAgentMessage, shouldCapturePrompt } from "./messaging.ts";
import { questPath, shouldStartPersistentQuest } from "./paths.ts";
import { persist, verifyAndMarkSaved } from "./persistence.ts";
import { loadActiveQuestResumeContext } from "./reconstruction.ts";
import { recordObservedInvestigation, triggerReassessment } from "./research.ts";
import { getActiveContext, getSessionId, getState, sessionStates, state } from "./state.ts";
import { reconcilePendingSubquestResume } from "./subquest.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode, UserMessageClassification } from "./types.ts";
import { buildSessionAwarenessBlock, updateUIStatus } from "./ui.ts";
import { classifyToolCall, isCriticalReviewSubagentInvocation, isJournalPath, normalizePath } from "./utils.ts";

export function classifyActivityPhase(
	toolName: string,
	input: any,
	targetState: any,
	isFailure: boolean = false,
): string | undefined {
	const norm = (toolName || "").toLowerCase().trim();

	const cmd = typeof input === "string" ? input : typeof input?.command === "string" ? input.command : typeof input?.cmd === "string" ? input.cmd : "";
	const isTestOrBuild = /make\s+test|deno\s+test|npm\s+test|pytest|cargo\s+test|jest|vitest|make\b|npm\s+run\s+build|npm\s+build|cargo\s+build|tsc\b/i.test(cmd);

	if (isTestOrBuild) {
		return "verification";
	}

	if (targetState?.reassessmentRequired) {
		if (norm === "edit" || norm === "write" || norm === "user_edit" || norm === "user_write") {
			const p = typeof input?.path === "string" ? input.path : "";
			return isJournalPath(p) ? "checkpoint" : "implementation";
		}
		if (norm === "quest_update_state") {
			return "planning";
		}
		if (norm === "quest_mark_saved") {
			return "checkpoint";
		}
		return "reassessment";
	}

	if (targetState?.researchRequired || !targetState?.researchComplete) {
		if (norm === "quest_update_state") {
			return "planning";
		}
		if (norm === "quest_mark_saved") {
			return "checkpoint";
		}
		if (norm === "edit" || norm === "write" || norm === "user_edit" || norm === "user_write") {
			const p = typeof input?.path === "string" ? input.path : "";
			return isJournalPath(p) ? "checkpoint" : "implementation";
		}
		return "research";
	}

	if (norm === "quest_update_state" || norm === "quest_subquest") {
		return "planning";
	}
	if (norm === "quest_mark_saved") {
		return "checkpoint";
	}
	if (norm === "quest_archive") {
		return "completion";
	}

	if (norm === "edit" || norm === "write" || norm === "user_edit" || norm === "user_write") {
		const p = typeof input?.path === "string" ? input.path : "";
		return isJournalPath(p) ? "checkpoint" : "implementation";
	}

	if (norm === "subagent") {
		const action = input?.action;
		if (action === "list" || action === "get" || action === "status" || action === "doctor") {
			return "research";
		}
		if (isCriticalReviewSubagentInvocation(input)) {
			return "verification";
		}
		return "implementation";
	}

	if (norm.startsWith("bg_run")) {
		return "implementation";
	}

	if (norm.startsWith("fusion_") || norm === "bg_delegate" || norm === "doc_to_md") {
		return "research";
	}

	if (norm === "read") {
		const p = typeof input?.path === "string" ? input.path : "";
		if (isJournalPath(p)) return "checkpoint";
		// Do not guess implementation or research when implementation is allowed; omit phase
		return undefined;
	}

	const perm = classifyToolCall(norm, input);
	if (perm === "journal") return "checkpoint";

	// If phase cannot be reliably determined from state machine or tool semantics, omit rather than guessing
	return undefined;
}

export function isToolExecutionError(tr: any): boolean {
	return !!tr?.isError || !!tr?.error || !!(tr?.details && (tr.details.error || tr.details.success === false));
}

export function detectBashToolFailure(tr: any): { hasFailure: boolean; reason: string; evidence: string } {
	const toolFailed = isToolExecutionError(tr);
	const output = typeof tr?.content === "string" ? tr.content :
		Array.isArray(tr?.content) ? tr.content.map((c: any) => c.text || "").join("\n") :
		typeof tr?.output === "string" ? tr.output : "";
	const cmd = tr?.args?.command || tr?.input?.command || tr?.command || "";

	const isTestOrBuild = /make\s+test|deno\s+test|npm\s+test|pytest|cargo\s+test|jest|vitest|make\b|npm\s+run\s+build|npm\s+build|cargo\s+build|tsc\b/i.test(cmd);
	const hasFailureSignals = toolFailed || /\b(?:FAIL|FAILED|assertion failed|panic:|Segmentation fault|make:\s*\*\*\*|TypeError|SyntaxError)\b/i.test(output);

	if (hasFailureSignals && isTestOrBuild) {
		return {
			hasFailure: true,
			reason: `Test/build command failed: ${cmd || "test execution"}`,
			evidence: output ? output.slice(0, 1500) : `Command '${cmd}' failed`,
		};
	}
	if (toolFailed) {
		return {
			hasFailure: true,
			reason: `Command failed with error: ${cmd || "bash command"}`,
			evidence: output ? output.slice(0, 1500) : `Command '${cmd}' failed with error`,
		};
	}
	return { hasFailure: false, reason: "", evidence: "" };
}

export function isQuestUpdateTool(toolName: string): boolean {
	return (
		toolName.includes("quest_update_state") ||
		toolName.includes("quest_mark_saved") ||
		toolName.includes("quest_archive") ||
		toolName.includes("quest_subquest")
	);
}

export function isPathToActiveQuest(targetPath: string, activeQuest: string): boolean {
	const activeQid = state.questId;
	return Boolean(
		targetPath &&
			((activeQid && targetPath.includes(`.pi/quest/current/${activeQid}/`)) ||
				targetPath.includes(".pi/quest/current/") ||
				targetPath.endsWith("quest.md")),
	);
}

export function isSubstantiveToolName(toolName: string): boolean {
	return (
		toolName === "bash" ||
		toolName === "user_bash" ||
		toolName === "subagent" ||
		toolName.startsWith("bg_run") ||
		toolName.startsWith("fusion_") ||
		toolName === "doc_to_md"
	);
}

export function classifyToolResultForTurn(
	tr: any,
	activeQuest: string,
): {
	isQuestUpdate: boolean;
	isSubstantive: boolean;
	failure: { hasFailure: boolean; reason: string; evidence: string } | null;
} {
	const toolName = (tr?.toolName || tr?.name || "").toLowerCase();
	const toolFailed = isToolExecutionError(tr);
	let failure = null;

	if (toolName === "bash" || toolName === "user_bash") {
		const bashFailure = detectBashToolFailure(tr);
		if (bashFailure.hasFailure) {
			failure = bashFailure;
		}
	}

	let isQuestUpdate = false;
	let isSubstantive = false;

	if (!toolFailed && isQuestUpdateTool(toolName)) {
		isQuestUpdate = true;
	}

	if (toolName === "edit" || toolName === "write") {
		const targetPath = tr?.args?.path || tr?.input?.path || "";
		if (isPathToActiveQuest(targetPath, activeQuest)) {
			if (!toolFailed) {
				isQuestUpdate = true;
			}
		} else if (!toolFailed) {
			isSubstantive = true;
		}
	} else if (!toolFailed && isSubstantiveToolName(toolName)) {
		isSubstantive = true;
	}

	return { isQuestUpdate, isSubstantive, failure };
}

export interface TurnFailureDetail {
	category: string;
	reason: string;
	command?: string;
	tool?: string;
}

export interface TurnToolAnalysis {
	didUpdateQuestThisTurn: boolean;
	isSubstantiveTurn: boolean;
	meaningfulFailureDetected: boolean;
	failureCount: number;
	failureCategories: string[];
	failures: TurnFailureDetail[];
	failureReason: string;
	failureEvidence: string;
}

export function analyzeTurnToolResults(
	toolResults: any[],
	activeQuest: string,
): TurnToolAnalysis {
	let didUpdateQuestThisTurn = false;
	let isSubstantiveTurn = false;
	let failureCount = 0;
	const failureCategoriesSet = new Set<string>();
	const failures: TurnFailureDetail[] = [];
	let firstFailureReason = "";
	const failureEvidences: string[] = [];

	for (const tr of toolResults) {
		const classified = classifyToolResultForTurn(tr, activeQuest);
		const cmd = tr?.args?.command || tr?.input?.command || tr?.command || "";
		const isTestOrBuild = /make\s+test|deno\s+test|npm\s+test|pytest|cargo\s+test|jest|vitest|make\b|npm\s+run\s+build|npm\s+build|cargo\s+build|tsc\b/i.test(cmd);
		const isBuildOnly = /make\b(?!.*test)|npm\s+run\s+build|npm\s+build|cargo\s+build|tsc\b/i.test(cmd);

		if (classified.failure) {
			failureCount++;
			const category = isTestOrBuild ? (isBuildOnly ? "BUILD_FAILED" : "TEST_FAILED") : "TOOL_FAILURE";
			failureCategoriesSet.add(category);
			failures.push({
				category,
				reason: classified.failure.reason,
				command: cmd || undefined,
				tool: tr?.toolName || tr?.name || undefined,
			});

			if (!firstFailureReason) {
				firstFailureReason = classified.failure.reason;
			}
			if (classified.failure.evidence && failureEvidences.length < 3) {
				failureEvidences.push(classified.failure.evidence.slice(0, 500));
			}

			if (isTestOrBuild) {
				logVerificationTransition(isBuildOnly ? "BUILD_FAILED" : "TEST_FAILED", classified.failure.reason, {
					quest: activeQuest,
					command: cmd,
					category,
					reason: classified.failure.reason,
				});
			} else {
				logToolFailure("TOOL_FAILURE", classified.failure.reason, {
					quest: activeQuest,
					command: cmd,
					category,
					reason: classified.failure.reason,
				});
			}
		} else if (isTestOrBuild) {
			logVerificationTransition(isBuildOnly ? "BUILD_PASSED" : "TEST_PASSED", `command passed: ${cmd}`, {
				quest: activeQuest,
				command: cmd,
			});
		}

		if (classified.isQuestUpdate) {
			didUpdateQuestThisTurn = true;
		}
		if (classified.isSubstantive) {
			isSubstantiveTurn = true;
		}
	}

	const meaningfulFailureDetected = failureCount > 0;
	const failureCategories = Array.from(failureCategoriesSet);
	const failureReason = failureCount > 1
		? `${failureCount} failures detected (${failureCategories.join(", ")}): ${firstFailureReason}`
		: firstFailureReason;
	const failureEvidence = failureEvidences.join("\n---\n");

	return {
		didUpdateQuestThisTurn,
		isSubstantiveTurn,
		meaningfulFailureDetected,
		failureCount,
		failureCategories,
		failures,
		failureReason,
		failureEvidence,
	};
}

export function applyTurnEndStateTransitions(
	targetState: any,
	analysis: ReturnType<typeof analyzeTurnToolResults>,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): void {
	if (analysis.meaningfulFailureDetected) {
		targetState.consecutiveFailures = (targetState.consecutiveFailures || 0) + analysis.failureCount;
		triggerReassessment(targetState, analysis.failureReason, analysis.failureEvidence);
		persist(pi, ctx);
		updateUIStatus(ctx);
	} else if (analysis.didUpdateQuestThisTurn) {
		targetState.substantiveTurnsSinceCheckpoint = 0;
		persist(pi, ctx);
		updateUIStatus(ctx);
	} else if (analysis.isSubstantiveTurn) {
		targetState.dirty = true;
		targetState.substantiveTurnsSinceCheckpoint = (targetState.substantiveTurnsSinceCheckpoint || 0) + 1;
		persist(pi, ctx);
		updateUIStatus(ctx);
	}
}

export function installTurnStart(pi: ExtensionAPI) {
	pi.on(
		"turn_start",
		withContext(async (event: any, _ctx: ExtensionContext) => {
			if (state.pickerCancelled) return;
			if (!state.active && !state.pendingRootQuest) return;

			const turnIndex = typeof event?.turnIndex === "number" ? event.turnIndex : (state.currentTurn || 0) + 1;
			state.currentTurn = turnIndex;
			state.currentTurnCorrelationId = `turn_${turnIndex}_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;

			logTurnBoundary("TURN_START", "agent turn started", {
				quest: state.active || "",
				turn: state.currentTurn,
				correlationId: state.currentTurnCorrelationId,
			});
		}),
	);
}

export function installTurnEnd(pi: ExtensionAPI) {
	pi.on(
		"turn_end",
		withContext(async (event: any, ctx: ExtensionContext) => {
			if (state.pickerCancelled) return;

			// ALWAYS drain pending notifications and retry pending resumes at turn end,
			// even if compactionPending is true or active is null, so recovery obligations are never starved.
			drainPendingResumesAndNotifications(pi, ctx);

			if (state.compactionPending) return;

			// Handle deferred archive compaction even if active quest is now null
			if (state.archiveCompactionPending) {
				checkAndTriggerDeferredCompaction(pi, ctx);
				return;
			}

			if (!state.active) return;

			if (typeof event?.turnIndex === "number") {
				state.currentTurn = event.turnIndex;
			}
			if (!state.currentTurnCorrelationId) {
				state.currentTurnCorrelationId = `turn_${state.currentTurn || 1}_${Date.now().toString(36)}`;
			}

			const toolResults: any[] = Array.isArray(event.toolResults) ? event.toolResults : [];
			const analysis = analyzeTurnToolResults(toolResults, state.active);
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
			if (state.substantiveTurnsSinceCheckpoint === 5) {
				logContinuationAnomaly("NO_PROGRESS", `turns without state checkpoint reached threshold (turns=5)`, {
					quest: state.active || "",
					turns: 5,
				});
				await checkAndTriggerDirectionReview(pi, ctx, "no_progress");
			}

			// Proactive context-pressure save request before compaction
			advanceSteerTurnCounter();
			requestPreCompactionCheckpoint(pi, ctx, false, "turn_end");

			// Check deferred (archive / subquest launch) compaction
			checkAndTriggerDeferredCompaction(pi, ctx);
		}),
	);
}

export function installToolResultListener(pi: ExtensionAPI) {
	pi.on(
		"tool_result",
		withContext(async (event: any, ctx: ExtensionContext) => {
			if (!state.active && !state.pendingRootQuest) return;
			const toolName = event?.toolName || event?.name || "";
			const toolInput = event?.input || event?.args || {};
			const toolOutput = event?.content || event?.output || "";
			const isError = Boolean(event?.isError || event?.error || (event?.details && (event?.details?.error || event?.details?.success === false)));
			recordObservedInvestigation(state, toolName, toolInput, toolOutput, isError);

			const normName = (toolName || "").toLowerCase().trim();
			const activeQuest = state.active || "";

			// Determine operation & failure details
			let isFailure = isError;
			let failureReason: string | undefined = undefined;

			if (normName === "bash" || normName === "user_bash") {
				const bashFailure = detectBashToolFailure(event);
				if (bashFailure.hasFailure) {
					isFailure = true;
					failureReason = bashFailure.reason;
				}
			} else if (isError) {
				failureReason = event?.error?.message || event?.message || (event?.details && event.details.error) || "tool execution error";
			}

			const operation = isFailure ? "failure" : "success";
			const phase = classifyActivityPhase(normName, toolInput, state, isFailure);

			// Extract target fields for TOOL_ACTIVITY logging
			let targetPath: string | undefined = undefined;
			let command: string | undefined = undefined;
			let query: string | undefined = undefined;

			if (normName === "read" || normName === "edit" || normName === "write" || normName === "user_edit" || normName === "user_write" || normName === "doc_to_md") {
				const rawPath = typeof toolInput === "string" ? toolInput : toolInput?.path || toolInput?.file || "";
				targetPath = rawPath ? normalizeLogPath(rawPath, ctx?.cwd) : undefined;
			}

			if (normName === "bash" || normName === "user_bash" || normName.startsWith("bg_run")) {
				const rawCmd = typeof toolInput === "string" ? toolInput : toolInput?.command || toolInput?.cmd || toolInput?.name || "";
				command = rawCmd ? sanitizeLogString(rawCmd, 150) : undefined;
			}

			if (
				normName === "search_graph" ||
				normName === "query_graph" ||
				normName === "trace_path" ||
				normName === "get_code_snippet" ||
				normName === "search_code" ||
				normName === "get_graph_schema" ||
				normName === "get_architecture" ||
				normName === "check_index_coverage" ||
				normName === "detect_changes"
			) {
				const rawTarget = toolInput?.query || toolInput?.pattern || toolInput?.name_pattern || toolInput?.function_name || toolInput?.qualified_name || toolInput?.target || toolInput?.path || "";
				query = rawTarget ? sanitizeLogString(rawTarget, 100) : undefined;
				if (toolInput?.path) {
					targetPath = normalizeLogPath(toolInput.path, ctx?.cwd);
				}
			} else if (normName === "web_search" || normName === "source_check" || normName === "fetch_content" || normName === "get_search_content" || normName === "fetch") {
				const rawTarget = toolInput?.query || toolInput?.claim || toolInput?.url || toolInput?.path || "";
				query = rawTarget ? sanitizeLogString(rawTarget, 100) : undefined;
			} else if (normName === "subagent") {
				const rawTask = toolInput?.task || toolInput?.agent || "";
				command = rawTask ? sanitizeLogString(rawTask, 120) : undefined;
			} else if (normName.startsWith("fusion_") || normName === "bg_delegate") {
				const rawObjective = toolInput?.objective || toolInput?.prompt || toolInput?.name || toolInput?.changeSummary || "";
				query = rawObjective ? sanitizeLogString(rawObjective, 120) : undefined;
			} else if (normName === "memory_read" || normName === "memory_search" || normName === "memory_write" || normName === "memory_forget" || normName === "scratchpad") {
				const rawTarget = toolInput?.target || toolInput?.query || toolInput?.action || toolInput?.match || "";
				query = rawTarget ? sanitizeLogString(rawTarget, 80) : undefined;
			} else if (normName === "quest_update_state") {
				const rawDesc = toolInput?.status || toolInput?.name || toolInput?.goal || "";
				query = rawDesc ? sanitizeLogString(rawDesc, 80) : undefined;
			} else if (normName === "quest_mark_saved" || normName === "quest_archive") {
				const rawName = toolInput?.name || toolInput?.questName || "";
				query = rawName ? sanitizeLogString(rawName, 60) : undefined;
			} else if (normName === "quest_subquest") {
				const rawGoal = toolInput?.goal || toolInput?.name || "";
				query = rawGoal ? sanitizeLogString(rawGoal, 80) : undefined;
			} else if (normName === "ask_questions") {
				const rawQuestions = Array.isArray(toolInput?.questions) ? toolInput.questions.map((q: any) => q?.question || q?.header || "").filter(Boolean).join("; ") : "";
				query = rawQuestions ? sanitizeLogString(rawQuestions, 100) : undefined;
			}

			// Emit TOOL_ACTIVITY
			logToolActivity(normName, operation, {
				quest: activeQuest,
				phase,
				path: targetPath,
				command,
				query,
				turn: state.currentTurn,
				correlationId: state.currentTurnCorrelationId,
				reason: failureReason ? sanitizeLogString(failureReason, 120) : undefined,
			});

			if (normName === "edit" || normName === "write") {
				const rawPath = toolInput?.path || toolInput?.file || "";
				if (!isPathToActiveQuest(rawPath, activeQuest)) {
					if (isError) {
						logImplementationOutcome("IMPLEMENTATION_FAILED", `failed ${normName} ${rawPath}`, {
							quest: activeQuest,
							tool: normName,
							path: targetPath || rawPath,
							turn: state.currentTurn,
							correlationId: state.currentTurnCorrelationId,
						});
					} else {
						logImplementationOutcome("IMPLEMENTATION_COMPLETED", `completed ${normName} ${rawPath}`, {
							quest: activeQuest,
							tool: normName,
							path: targetPath || rawPath,
							turn: state.currentTurn,
							correlationId: state.currentTurnCorrelationId,
						});
					}
				}
			}
		}),
	);
}

export function installBeforeCompact(pi: ExtensionAPI) {
	pi.on(
		"session_before_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;

			// If a pending resume obligation exists, attempt delivery before starting a new transaction
			if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
				retryPendingResume(pi, ctx);
				if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
					logDebug("Quest Journal: cancelling session_before_compact because previous resume obligation is still pending delivery.");
					return { cancel: true };
				}
			}

			if (!compactionReady()) {
				state.compactionPending = false;
				state.preCompactionCheckpointPending = true;
				state.preCompactionSaveRequestPending = true;
				persist(pi, ctx);

				const activeFile = questPath(state.active);
				const msg = `⚡ **Compaction Blocked (Unsaved Working Memory)**:
Compaction is blocked because the active quest file \`${activeFile}\` contains unsaved changes or unverified state.

To allow auto-compaction and preserve continuity across the boundary:
1. Update \`${activeFile}\` with your current understanding, decisions, plan confidence, remaining work, and exact next step.
2. Call \`quest_mark_saved\` to persist the state.
Once saved, auto-compaction will safely proceed.`;

				logCompactionTransition("COMPACTION_BLOCKED", "compaction blocked: unsaved working memory", {
					quest: state.active || "",
					reason: "unsaved working memory",
				});

				sendInternalAgentMessage(pi, msg, "steer");

				if (ctx?.hasUI) {
					ctx.ui.notify(`Quest-journal: blocking compaction until '${activeFile}' is saved.`, "warning");
				}
				return { cancel: true };
			}

			// Establish transaction in-flight
			const tx = createOrGetCompactionTransaction(state, "normal-compaction");
			tx.phase = "in-flight";
			persist(pi, ctx);
			logCompactionTransition("COMPACTION_STARTED", "compaction started", {
				quest: state.active || "",
				compactionId: tx.id,
			});
		}),
	);
}

export function recordCompactionFailureState(sessionState: any, errorMsg: string): void {
	sessionState.compactionPending = false;
	if (sessionState.activeTransaction) {
		sessionState.activeTransaction.phase = "failed";
		sessionState.activeTransaction.failedAt = Date.now();
		sessionState.activeTransaction.error = errorMsg;
	}
	logCompactionTransition("COMPACTION_FAILED", `compaction failed: ${errorMsg}`, {
		quest: sessionState.active || "",
		compactionId: sessionState.activeTransaction?.id || undefined,
		code: "COMPACTION_FAILURE",
		error: errorMsg,
		reason: errorMsg,
	});
	sessionState.activeCompactionId = null;
	sessionState.lastWarnedCompactionTokens = null;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;
	sessionState.subquestLaunchCompactionPending = false;
}

export function reportCompactionFailure(
	pi: ExtensionAPI,
	c: ExtensionContext | undefined,
	sessionState: any,
	errorMsg: string,
): void {
	reportAgentError(
		pi,
		c,
		errorMsg,
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

export function installAfterCompact(pi: ExtensionAPI) {
	pi.on(
		"session_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			await handleCompactionCompleted(pi, ctx);
		}),
	);
	pi.on(
		"session_compact_failed",
		withContext(async (event: any, ctx: ExtensionContext) => {
			await handleCompactionFailure(pi, ctx, event);
		}),
	);
}

export function installContextListener(pi: ExtensionAPI) {
	pi.on(
		"context",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			if (state.pickerCancelled) return;
			requestPreCompactionCheckpoint(pi, ctx, false, "context");
		}),
	);
}

export function installBeforeSwitch(pi: ExtensionAPI) {
	pi.on(
		"session_before_switch",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			if (ctx.hasUI && !compactionReady()) {
				ctx.ui.notify(`Quest-journal: active quest '${state.active}' has unsaved changes before session switch.`, "warning");
			}
		}),
	);
}

export function installShutdownSave(pi: ExtensionAPI) {
	pi.on("session_shutdown", async (event: any, ctx: any) => {
		if (event.reason !== "quit") return;
		if (!state.active) return;
		if (ctx?.hasUI && !compactionReady()) {
			ctx.ui.notify(`Quest-journal: quest '${state.active}' has unsaved changes.`, "warning");
		}
	});
}

export function installFileWatch(pi: ExtensionAPI) {
	pi.on("tool_result", async (event: any, ctx: any) => {
		if (event.isError || event.error || (event.details && (event.details.error || event.details.success === false))) {
			return;
		}

		if (state.activeTransaction && state.activeTransaction.phase === "resume-delivered") {
			state.activeTransaction = null;
		}

		if (event.toolName === "ask_questions" || (typeof event.toolName === "string" && event.toolName.toLowerCase().includes("ask_question"))) {
			handleAskQuestionsResult(pi, event, ctx);
			return;
		}

		if (event.toolName !== "write" && event.toolName !== "edit") {
			if (
				event.toolName === "bash" ||
				event.toolName === "user_bash" ||
				event.toolName === "subagent" ||
				(typeof event.toolName === "string" && (event.toolName.startsWith("bg_run") || event.toolName.startsWith("fusion_") || event.toolName === "doc_to_md"))
			) {
				state.dirty = true;
			}
			return;
		}

		const p = event.input?.path as string | undefined;
		if (typeof p !== "string") return;
		const norm = normalizePath(p);

		if (state.active && norm === questPath(state.questId)) {
			await verifyAndMarkSaved(pi, ctx, state.active);
		} else if (!state.active && norm.startsWith(`${QUEST_CURRENT_DIR}/`) && norm.endsWith("quest.md")) {
			const parts = norm.split("/");
			const qid = parts[parts.length - 2];
			state.questId = qid;
			state.active = qid;
			if (!Array.isArray(state.stack)) state.stack = [qid];
			else if (!state.stack.includes(qid)) state.stack.push(qid);
			await verifyAndMarkSaved(pi, ctx, qid);
		} else {
			state.dirty = true;
			if (!Array.isArray(state.sessionModifiedFiles)) {
				state.sessionModifiedFiles = [];
			}
			if (!state.sessionModifiedFiles.includes(norm)) {
				state.sessionModifiedFiles.push(norm);
			}
		}
	});
}

export function installWorkflowSystemPrompt(pi: ExtensionAPI) {
	pi.on(
		"before_agent_start",
		withContext(async (event: any, ctx: ExtensionContext) => {
			try {
				const raw = (event as { prompt?: unknown })?.prompt;
				if (typeof raw === "string" && shouldCapturePrompt(raw)) {
					const trimmed = raw.trim().slice(0, PROMPT_MAX_CHARS);

					if (state.active) {
						if (!Array.isArray(state.refinements)) state.refinements = [];
						if (!Array.isArray(state.prompts)) state.prompts = [];

						const isOriginal = state.prompts.length > 0 && state.prompts[0] === trimmed;
						const isLatestRefinement = state.refinements.length > 0 && state.refinements[state.refinements.length - 1] === trimmed;

						if (!isOriginal && !isLatestRefinement) {
							const classification = classifyUserMessage(trimmed);

							if (classification === UserMessageClassification.CONFIRMATION) {
								logUserInteraction("CONFIRMATION_RECEIVED", "user confirmation received", { quest: state.active || "" });
								acceptRootConfirmation(pi, ctx);
							} else if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT) {
								logUserInteraction("USER_REFINEMENT_RECEIVED", "user refinement received", { quest: state.active || "" });
								state.refinements.push(trimmed);
								state.prompts.push(trimmed);
								if (state.prompts.length > PROMPT_MAX_COUNT) {
									state.prompts = [state.prompts[0], ...state.prompts.slice(-(PROMPT_MAX_COUNT - 1))];
								}
								if (state.refinements.length > PROMPT_MAX_COUNT) {
									state.refinements = state.refinements.slice(-PROMPT_MAX_COUNT);
								}
								triggerReassessment(state, `User refinement received: "${trimmed.slice(0, 100)}..."`, trimmed);
								persist(pi, ctx);
								updateUIStatus(ctx);
							}
						}
					} else if (state.pendingRootQuest) {
						const classification = classifyUserMessage(trimmed);
						if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT) {
							if (!Array.isArray(state.refinements)) state.refinements = [];
							state.refinements.push(trimmed);
							if (!state.prompts.includes(trimmed)) {
								state.prompts.push(trimmed);
							}
							persist(pi, ctx);
							updateUIStatus(ctx);
						}
					} else if (shouldStartPersistentQuest(trimmed)) {
						await ensureRootQuestForPrompt(pi, ctx, trimmed);
					}
				}

				drainPendingResumesAndNotifications(pi, ctx);

				const awarenessBlock = buildSessionAwarenessBlock(ctx);
				const resumeContext = await loadActiveQuestResumeContext();
				const workflowInstructions = getWorkflowInstructions(resumeContext);

				if (event && typeof event.systemPrompt === "string") {
					return { systemPrompt: `${event.systemPrompt}\n\n${awarenessBlock}${workflowInstructions}` };
				}
			} catch (err: any) {
				logError("Failed in before_agent_start hook", err, ctx);
				return;
			}
		}),
	);
}

export function registerQuestJournalCRBHook() {
	if (typeof globalThis !== "undefined") {
		const g = globalThis as any;
		if (!g.__pi_crb_providers) {
			g.__pi_crb_providers = [];
		}
		g.__pi_crb_providers.push((_ctx: ExtensionContext, tools: string[]) => {
			const set = new Set(tools.map((t) => t.toLowerCase()));
			if (set.has("quest_mark_saved") || set.has("quest_update_state") || state.active || state.pendingRootQuest) {
				return [
					"Never propose anything without doing your homework first: thoroughly investigate codebase architecture, read files, discover build/run commands, and evaluate constraints before proposing plans or code changes.",
					"Research-Grounded Quest Formation: Investigate first to understand the actual problem, establish a short intelligible semantic quest identity, and initialize the durable quest with research findings via quest_update_state.",
					"Turn 1 Confirmation: In turn 1 of any root quest, present research findings, key assumptions tested, architectural trade-offs, and revised plan clearly to the user, and ask for confirmation BEFORE writing code.",
					"Continuous Durable Epistemic Memory: `.pi/quest/current/<qid>/quest.md` is your durable working memory and single source of truth on disk. Proactively record understanding, assumptions, plan confidence, plan revisions, and exact next action whenever discoveries occur.",
					"Dynamic Epistemic Re-Investigation: Use the quest file to recover established knowledge without repeating routine research (no unnecessary re-research). Re-investigate whenever new evidence contradicts an assumption, tests fail, or the plan fails to explain observed behavior.",
					"Autonomous Continuation: Following compaction or sub-quest return, read `.pi/quest/current/<qid>/quest.md`, validate the plan against recovered state, and proceed immediately without user interruption.",
					"Meaningful Sub-Quest Decomposition: Decompose according to the discovered structure of the problem, not arbitrary bullet counts. During research, identify genuinely separable workstreams (distinct subsystems, independent investigations, separate verification boundaries) and create sub-quests (`quest_subquest({ switchNow: false })`) linked into the parent plan (`[[subquest-name]]`). Avoid artificial fragmentation for trivial or tightly coupled steps. Sub-quests independently verify inherited context.",
					"Durable-State Reconciliation: The quest file must describe what is true NOW. After substantive changes, synchronize Completed, Files Modified, Test Status, Remaining Work, and Exact Next Action. Exact Next Action is a live pointer to the next justified action, never a repeat of completed work. Calibrate plan confidence against evidence and explain plan revisions.",
					"Full Test Suite Quality Gate: Before completing/archiving a top-level quest, restart the test server/daemon, run the fresh FULL test suite (`make test`), and verify zero errors.",
					"Top-level Quest Completion: When root quest is done, prompt user via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
				];
			}
			return [];
		});
	}
}
