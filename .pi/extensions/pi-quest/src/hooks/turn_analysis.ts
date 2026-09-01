import { isJournalPath, classifyToolCall, isCriticalReviewSubagentInvocation } from "../utils.ts";
import { logVerificationTransition, logToolFailure } from "../logging.ts";
import { triggerReassessment } from "../research.ts";
import { persist } from "../persistence.ts";
import { updateUIStatus } from "../ui.ts";
import { state } from "../state.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";

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
		return undefined;
	}

	const perm = classifyToolCall(norm, input);
	if (perm === "journal") return "checkpoint";

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

	// SEARCH/investigative binaries: rg/grep exit 1 = "no matches" not an error; wc/ls/fd/etc with empty output must not escalate.
	// 10: wc -l/ls/fd investigation whitelisted — do not trigger TOOL_FAILURE/REASSESSMENT
	if (toolFailed && /\b(rg|grep|egrep|fgrep|ag|ack|fd|find|wc|ls|stat|file|du|df|tree|cat|head|tail)\b/i.test(cmd)) {
		const exitCode = (tr as any)?.details?.exitCode ?? (tr as any)?.exitCode ?? (tr as any)?.details?.code;
		if (exitCode === 1) {
			return { hasFailure: false, reason: "", evidence: "" };
		}
		if (!output || !output.trim()) {
			return { hasFailure: false, reason: "", evidence: "" };
		}
		const hasRealErrorSignal = /\b(?:error:|FAILED|panic:|Segmentation fault|permission denied|no such file|cannot open|failed to)\b/i.test(output);
		if (!hasRealErrorSignal) {
			return { hasFailure: false, reason: "", evidence: "" };
		}
	}

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
	let failure = null;

	if (toolName === "bash" || toolName === "user_bash") {
		const bashFailure = detectBashToolFailure(tr);
		if (bashFailure.hasFailure) {
			failure = bashFailure;
		}
	}

	const toolFailed = isToolExecutionError(tr);
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

			const failureId = (state as any).lastFailureId || `fail_${state.currentTurn || 1}_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;
			(state as any).lastFailureId = failureId;

			if (isTestOrBuild) {
				logVerificationTransition(isBuildOnly ? "BUILD_FAILED" : "TEST_FAILED", classified.failure.reason, {
					quest: activeQuest,
					command: cmd,
					category,
					reason: classified.failure.reason,
					failureId,
					consequence: "TRIGGERED_REASSESSMENT",
					testStatus: "FAILED",
				});
			} else {
				logToolFailure("TOOL_FAILURE", classified.failure.reason, {
					quest: activeQuest,
					command: cmd,
					category,
					reason: classified.failure.reason,
					failureId,
					consequence: "TOOL_ERROR",
				});
			}
		} else if (isTestOrBuild) {
			const recoveryFor = (state as any).lastFailureId;
			logVerificationTransition(isBuildOnly ? "BUILD_PASSED" : "TEST_PASSED", `command passed: ${cmd}`, {
				quest: activeQuest,
				command: cmd,
				recoveryFor,
				testStatus: "PASSED",
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
