import { appendFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from "node:fs";
import { dirname, join } from "node:path";
import { QUEST_CURRENT_DIR } from "./constants.ts";
import { getActiveContext, getSessionId, getState, state } from "./state.ts";
import { ExtensionContext } from "./types.ts";

export type MajorPhaseName =
	| "INITIALIZATION"
	| "RESEARCH"
	| "PLANNING"
	| "CONFIRMATION"
	| "IMPLEMENTATION"
	| "VERIFICATION"
	| "REASSESSMENT"
	| "CHECKPOINT"
	| "COMPACTION"
	| "RESUME"
	| "RECOVERY"
	| "COMPLETION";

export type QuestLogEventType =
	// 0. Structured concrete tool activity
	| "TOOL_ACTIVITY"
	// 1. Quest initialization decisions
	| "QUEST_DETECTED"
	| "QUEST_REUSED"
	| "QUEST_CREATED"
	| "QUEST_START"
	| "QUEST_SWITCH"
	| "QUEST_INITIALIZATION_FAILED"
	| "QUEST_ACTIVATION_FAILED"
	// 2. Agent-turn boundaries
	| "TURN_START"
	| "TURN_END"
	// 3. Gate state transitions
	| "GATE_BLOCKED"
	| "GATE_OPENED"
	| "GATE_STATE_CHANGED"
	// 4. Research evidence lifecycle
	| "RESEARCH_REQUIRED"
	| "RESEARCH_EVIDENCE"
	| "RESEARCH_REJECTED"
	| "RESEARCH_COMPLETED"
	// 5. Reassessment lifecycle
	| "REASSESSMENT_REQUIRED"
	| "REASSESSMENT_EVIDENCE"
	| "REASSESSMENT_REJECTED"
	| "REASSESSMENT_COMPLETED"
	| "REASSESSMENT_RESOLUTION_FAILED"
	// 6. Implementation attempt & outcome
	| "IMPLEMENTATION_ATTEMPT"
	| "IMPLEMENTATION_ALLOWED"
	| "IMPLEMENTATION_BLOCKED"
	| "IMPLEMENTATION_COMPLETED"
	| "IMPLEMENTATION_FAILED"
	// 7. Tool classification anomalies
	| "UNKNOWN_TOOL"
	| "UNEXPECTED_TOOL_RESULT"
	| "TOOL_CLASSIFICATION_MISMATCH"
	// 8. Tool-result failures
	| "TOOL_FAILURE"
	| "TOOL_TIMEOUT"
	| "TOOL_CANCELLED"
	// 9. Test/build lifecycle
	| "TEST_STARTED"
	| "TEST_PASSED"
	| "TEST_FAILED"
	| "BUILD_STARTED"
	| "BUILD_PASSED"
	| "BUILD_FAILED"
	| "TEST_FAILURE"
	// 10. State-update lifecycle
	| "STATE_UPDATE_REJECTED"
	| "STATE_UPDATE_ACCEPTED"
	| "STATE_UPDATE_FAILED"
	| "STATE_RECONCILIATION_REQUIRED"
	// 11. Persistence lifecycle
	| "SAVE_STARTED"
	| "SAVE_VERIFIED"
	| "SAVE_REJECTED"
	| "SAVE_FAILED"
	| "PERSISTENCE_DEGRADED"
	| "PERSISTENCE_RECOVERED"
	// 12. Compaction transaction lifecycle
	| "CHECKPOINT"
	| "COMPACTION_PREPARED"
	| "COMPACTION_INVALIDATED"
	| "COMPACTION_STARTED"
	| "COMPACTION_COMPLETED"
	| "COMPACTION_FAILED"
	| "COMPACTION_INCONSISTENT"
	| "COMPACTION_EXTERNAL"
	| "COMPACTION_BLOCKED"
	// 13. Resume obligation lifecycle
	| "RESUME_OBLIGATION_CREATED"
	| "RESUME_ATTEMPTED"
	| "RESUME_DELIVERED"
	| "RESUME_FAILED"
	| "RESUME_RETRIED"
	| "RESUME_RECONCILIATION_REQUIRED"
	| "RESUME_OBSOLETED"
	// 14. Agent-message transport
	| "AGENT_MESSAGE_ATTEMPTED"
	| "AGENT_MESSAGE_DELIVERED"
	| "AGENT_MESSAGE_FAILED"
	| "AGENT_MESSAGE_QUEUED"
	| "AGENT_MESSAGE_RETRIED"
	// 15. Continuation/deadlock detection
	| "NO_PROGRESS"
	| "REPEATED_BLOCK"
	| "REPEATED_FAILURE"
	// 16. User interaction lifecycle
	| "CONFIRMATION_REQUESTED"
	| "CONFIRMATION_RECEIVED"
	| "CONFIRMATION_REJECTED"
	| "USER_REFINEMENT_RECEIVED"
	// 17. Subquest lifecycle
	| "SUBQUEST_START"
	| "SUBQUEST_SWITCH"
	| "SUBQUEST_RETURN"
	| "SUBQUEST_FAILED"
	| "SUBQUEST_RESUME_PENDING"
	| "SUBQUEST_RESUME_FAILED"
	| "SUBQUEST_COMPLETE"
	| "ARCHIVE"
	// 18. Critical Agent Review lifecycle
	| "CRITICAL_REVIEW_REQUESTED"
	| "CRITICAL_REVIEW_STARTED"
	| "CRITICAL_REVIEW_PASSED"
	| "CRITICAL_REVIEW_FAILED"
	| "CRITICAL_REVIEW_UNCERTAIN"
	| "CRITICAL_REVIEW_UNAVAILABLE"
	| "CRITICAL_REVIEW_ERROR"
	| "REMEDIATION_REQUIRED"
	| "SELF_CRITIQUE_STARTED"
	| "SELF_CRITIQUE_REVISED"
	// 19. Unknown/inconsistent states
	| "STATE_INCONSISTENT"
	| "RECOVERY_STARTED"
	| "RECOVERY_COMPLETED"
	| "RECOVERY_FAILED"
	| "ERROR";

export interface QuestLogContext {
	questId?: string;
	root?: string;
	rootQuest?: string;
	sessionId?: string;
	quest?: string;
	turn?: number | string;
	turnId?: string;
	correlationId?: string;
	tool?: string;
	operation?: "success" | "failure" | "blocked" | string;
	outcome?: string;
	phase?: string;
	path?: string;
	command?: string;
	query?: string;
	target?: string;
	action?: string;
	reason?: string;
	code?: string;
	readsCount?: number;
	searchesCount?: number;
	writesCount?: number;
	commandsCount?: number;
	logPath?: string;
	compactionId?: string;
	obligationId?: string;
	round?: number;
	version?: number;
	planVersion?: number;
	reassessmentVersion?: number;
	gate?: string;
	from?: string;
	to?: string;
	kind?: string;
	category?: string;
	categories?: string;
	requiredAction?: string;
	reads?: number;
	searches?: number;
	evidence?: number;
	allowed?: boolean;
	attempt?: number;
	checkpoint?: string;
	type?: string;
	subquest?: string;
	parent?: string;
	child?: string;
	status?: string;
	gen?: number;
	hash?: string;
	deliverAs?: string;
	substantive?: boolean;
	toolsUsed?: number;
	mutations?: number;
	failures?: number;
	questDirty?: boolean;
	implementationAllowed?: boolean;
	turns?: number;
	count?: number;
	permission?: string;
	investigation?: string;
	error?: string;
	[key: string]: any;
}

export interface QuestLogEntry {
	timestamp: string;
	type: QuestLogEventType;
	quest: string;
	context: Record<string, string>;
	message: string;
	raw: string;
}

export interface QuestRunSummary {
	quests: string[];
	majorPhases: string[];
	researchCycles: number;
	reassessmentCycles: number;
	implementationAttempts: number;
	implementationAllowedCount: number;
	implementationBlockedCount: number;
	blockedGates: string[];
	failureCount: number;
	failures: Array<{ type: string; code?: string; reason?: string }>;
	compactionCount: number;
	successfulCompactions: number;
	failedCompactions: number;
	inconsistentCompactions: number;
	compactions: Array<{ id?: string; status: string; phase?: string }>;
	resumeCount: number;
	resumeSuccessCount: number;
	resumeFailedCount: number;
	resumePendingCount: number;
	hasUnresolvedError: boolean;
	hasCriticalReviewFailure?: boolean;
	criticalReviewPassed?: boolean;
	lastError?: string;
	deadlockWarnings: string[];
	formattedSummary: string;
}

let isLoggingDegraded = false;
let hasWarnedLoggingDegraded = false;

const pinnedLogPaths = new Map<string, string>();

export function pinQuestLog(qid: string, targetPath: string): void {
	pinnedLogPaths.set(qid, targetPath);
}

export function isQuestLogPinned(qid: string): boolean {
	return pinnedLogPaths.has(qid);
}

export function getPinnedQuestLogPath(qid: string): string | undefined {
	return pinnedLogPaths.get(qid);
}

export function resetPinnedQuestLogs(): void {
	pinnedLogPaths.clear();
}

export function isQuestLoggingDegraded(): boolean {
	return isLoggingDegraded;
}

export function resetQuestLoggingDegraded(): void {
	isLoggingDegraded = false;
	hasWarnedLoggingDegraded = false;
}

export function getQuestLogPath(qidOrCtx?: string | ExtensionContext | null, baseDir?: string): string {
	let targetQid: string | null = null;
	if (typeof qidOrCtx === "string") {
		targetQid = qidOrCtx;
	} else if (qidOrCtx) {
		const s = getState(qidOrCtx);
		targetQid = s?.questId || null;
	} else {
		const c = getActiveContext();
		const s = getState(c);
		targetQid = s?.questId || null;
	}

	const qid = targetQid || "default";
	if (pinnedLogPaths.has(qid)) {
		return pinnedLogPaths.get(qid)!;
	}

	const parentDir = baseDir || QUEST_CURRENT_DIR;
	const logDirPath = join(parentDir, qid);
	try {
		mkdirSync(logDirPath, { recursive: true });
	} catch {}
	return join(logDirPath, "execution.log");
}

export function getRunLogPath(qid: string, baseDir?: string): string {
	return getQuestLogPath(qid, baseDir);
}

export function sanitizeLogString(str: any, maxLen = 300): string {
	if (str === null || str === undefined) return "";
	const s = typeof str === "string" ? str : String(str);
	const singleLine = s.replace(/[\r\n\t]+/g, " ").trim();
	if (singleLine.length <= maxLen) return singleLine;
	return singleLine.slice(0, maxLen - 3) + "...";
}

export function normalizeLogPath(p?: any, cwd?: string): string {
	if (!p || typeof p !== "string") return "";
	let norm = p.trim().replace(/\\/g, "/");
	if (!norm) return "";

	if ((norm.startsWith('"') && norm.endsWith('"')) || (norm.startsWith("'") && norm.endsWith("'"))) {
		norm = norm.slice(1, -1).trim();
	}

	const baseCwd = (cwd || (typeof process !== "undefined" && typeof process.cwd === "function" ? process.cwd() : "")).replace(/\\/g, "/").replace(/\/+$/, "");

	if (baseCwd && norm.startsWith(baseCwd + "/")) {
		norm = norm.slice(baseCwd.length + 1);
	}

	norm = norm.replace(/^\.\//, "");
	norm = norm.replace(/\/{2,}/g, "/");

	return norm;
}

export function formatContextFields(context?: QuestLogContext): string {
	if (!context) return "";
	const parts: string[] = [];

	const priorityKeys = [
		"questId",
		"root",
		"rootQuest",
		"sessionId",
		"turn",
		"turnId",
		"correlationId",
		"compactionId",
		"obligationId",
		"reviewId",
		"subquest",
		"parent",
		"child",
		"round",
		"version",
		"planVersion",
		"reassessmentVersion",
		"gate",
		"from",
		"to",
		"tool",
		"operation",
		"outcome",
		"phase",
		"path",
		"command",
		"kind",
		"category",
		"categories",
		"target",
		"query",
		"action",
		"code",
		"reason",
		"requiredAction",
		"severity",
		"verdict",
		"reads",
		"searches",
		"evidence",
		"allowed",
		"attempt",
		"checkpoint",
		"type",
		"status",
		"gen",
		"hash",
		"deliverAs",
		"substantive",
		"toolsUsed",
		"readsCount",
		"searchesCount",
		"writesCount",
		"commandsCount",
		"mutations",
		"failures",
		"questDirty",
		"implementationAllowed",
		"turns",
		"count",
		"permission",
		"investigation",
		"error",
	];

	const formatValue = (val: any): string => {
		const s = sanitizeLogString(val, 150);
		if (s === "") return "";
		if ((s.includes(" ") || s.includes("=") || s.includes('"')) && !(s.startsWith('"') && s.endsWith('"'))) {
			return `"${s.replace(/"/g, '\\"')}"`;
		}
		return s;
	};

	for (const key of priorityKeys) {
		const val = context[key];
		if (val !== undefined && val !== null && val !== "") {
			parts.push(`${key}=${formatValue(val)}`);
		}
	}

	for (const [key, val] of Object.entries(context)) {
		if (key === "quest" || priorityKeys.includes(key)) continue;
		if (val !== undefined && val !== null && val !== "") {
			parts.push(`${key}=${formatValue(val)}`);
		}
	}

	return parts.join(" ");
}

export function formatLogEntry(
	type: QuestLogEventType,
	message: string,
	context?: QuestLogContext,
	timestamp?: string,
): string {
	const ts = timestamp || new Date().toISOString();
	const questName = sanitizeLogString(context?.quest !== undefined ? context.quest : (state?.active || ""), 60);
	const questPart = `quest=${questName || "(none)"}`;
	const contextPart = formatContextFields(context);
	const msgPart = sanitizeLogString(message, 300);

	const segments = [ts, type, questPart];
	if (contextPart) {
		segments.push(contextPart);
	}
	segments.push(msgPart);

	return segments.join(" | ");
}

export function parseLogEntry(line: string): QuestLogEntry | null {
	const trimmed = line.trim();
	if (!trimmed) return null;

	const parts = trimmed.split(" | ");
	if (parts.length < 3) return null;

	const timestamp = parts[0];
	const type = parts[1] as QuestLogEventType;
	let quest = "";
	let contextStr = "";
	let message = "";

	if (parts.length === 3) {
		message = parts[2];
	} else if (parts.length === 4) {
		const middle = parts[2];
		message = parts[3];
		if (middle.startsWith("quest=") && !middle.includes(" ")) {
			quest = middle.slice("quest=".length);
		} else {
			contextStr = middle;
		}
	} else {
		const questPart = parts[2];
		quest = questPart.startsWith("quest=") ? questPart.slice("quest=".length) : questPart;
		contextStr = parts.slice(3, parts.length - 1).join(" | ");
		message = parts[parts.length - 1];
	}

	const context: Record<string, string> = {};
	if (contextStr) {
		const regex = /([a-zA-Z0-9_-]+)=(?:"([^"]*)"|'([^']*)'|([^\s]+))/g;
		let match;
		while ((match = regex.exec(contextStr)) !== null) {
			const k = match[1];
			const v = match[2] !== undefined ? match[2] : (match[3] !== undefined ? match[3] : match[4]);
			context[k] = v;
			if (k === "quest" && !quest) quest = v;
		}
	}

	if (!quest && parts.length >= 3) {
		const m = parts[2].match(/quest=(?:"([^"]*)"|'([^']*)'|([^\s]+))/);
		if (m) {
			quest = m[1] !== undefined ? m[1] : (m[2] !== undefined ? m[2] : m[3]);
		}
	}

	return {
		timestamp,
		type,
		quest: quest && quest !== "(none)" ? quest : "",
		context,
		message,
		raw: trimmed,
	};
}

export function logEvent(
	type: QuestLogEventType,
	message: string,
	context?: QuestLogContext,
): void {
	try {
		const c = getActiveContext();
		const s = getState(c);
		const questId = context?.questId || s?.questId || undefined;
		const rootQuest = context?.root || context?.rootQuest || (s?.stack && s.stack.length > 0 ? s.stack[0] : (s?.active || undefined));
		const activeQuest = context?.quest !== undefined ? context.quest : (s?.active || "");

		const enrichedContext: QuestLogContext = {
			questId,
			root: rootQuest,
			rootQuest,
			quest: activeQuest,
			sessionId: context?.sessionId || getSessionId(c) || undefined,
			...context,
		};

		const line = formatLogEntry(type, message, enrichedContext);
		const targetPath = context?.logPath || getQuestLogPath(questId);

		try {
			mkdirSync(dirname(targetPath), { recursive: true });
			appendFileSync(targetPath, line + "\n", "utf8");
		} catch (err: any) {
			isLoggingDegraded = true;
			if (!hasWarnedLoggingDegraded) {
				hasWarnedLoggingDegraded = true;
				try {
					if (c?.hasUI && typeof c.ui?.notify === "function") {
						c.ui.notify(
							`Quest Journal: Persistent logging failed (${err?.message || "fs error"}). Logging is running degraded in memory.`,
							"warning",
						);
					}
				} catch {}
			}
			// Deliberately ignore fs errors; logging must NEVER crash quest execution.
		}
	} catch {
		// Deliberately ignore any formatting or state resolution errors.
	}
}

// ----------------------------------------------------------------------------
// Structured Lifecycle Logging Helpers
// ----------------------------------------------------------------------------

export function logToolActivity(
	toolName: string,
	operation: "success" | "failure" | "blocked" | string,
	context?: QuestLogContext,
	message?: string,
): void {
	const tool = toolName || "unknown";
	const defaultMsg = message || `${tool}${context?.path ? ` ${context.path}` : ""}${context?.command ? ` ${context.command}` : ""}${context?.query ? ` ${context.query}` : ""}`.trim();
	logEvent("TOOL_ACTIVITY", defaultMsg, {
		tool,
		operation,
		...context,
	});
}

export function logQuestTransition(
	type:
		| "QUEST_DETECTED"
		| "QUEST_REUSED"
		| "QUEST_CREATED"
		| "QUEST_START"
		| "QUEST_SWITCH"
		| "QUEST_INITIALIZATION_FAILED"
		| "QUEST_ACTIVATION_FAILED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logTurnBoundary(
	type: "TURN_START" | "TURN_END",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logGateTransition(
	type: "GATE_BLOCKED" | "GATE_OPENED" | "GATE_STATE_CHANGED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logResearchTransition(
	type:
		| "RESEARCH_REQUIRED"
		| "RESEARCH_EVIDENCE"
		| "RESEARCH_REJECTED"
		| "RESEARCH_COMPLETED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logReassessmentTransition(
	type:
		| "REASSESSMENT_REQUIRED"
		| "REASSESSMENT_EVIDENCE"
		| "REASSESSMENT_REJECTED"
		| "REASSESSMENT_COMPLETED"
		| "REASSESSMENT_RESOLUTION_FAILED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logImplementationOutcome(
	type:
		| "IMPLEMENTATION_ATTEMPT"
		| "IMPLEMENTATION_ALLOWED"
		| "IMPLEMENTATION_BLOCKED"
		| "IMPLEMENTATION_COMPLETED"
		| "IMPLEMENTATION_FAILED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logToolAnomaly(
	type: "UNKNOWN_TOOL" | "UNEXPECTED_TOOL_RESULT" | "TOOL_CLASSIFICATION_MISMATCH",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logToolFailure(
	type: "TOOL_FAILURE" | "TOOL_TIMEOUT" | "TOOL_CANCELLED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logVerificationTransition(
	type:
		| "TEST_STARTED"
		| "TEST_PASSED"
		| "TEST_FAILED"
		| "BUILD_STARTED"
		| "BUILD_PASSED"
		| "BUILD_FAILED"
		| "TEST_FAILURE",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logStateUpdateTransition(
	type:
		| "STATE_UPDATE_REJECTED"
		| "STATE_UPDATE_ACCEPTED"
		| "STATE_UPDATE_FAILED"
		| "STATE_RECONCILIATION_REQUIRED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logPersistenceTransition(
	type:
		| "SAVE_STARTED"
		| "SAVE_VERIFIED"
		| "SAVE_REJECTED"
		| "SAVE_FAILED"
		| "PERSISTENCE_DEGRADED"
		| "PERSISTENCE_RECOVERED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logCompactionTransition(
	type:
		| "CHECKPOINT"
		| "COMPACTION_PREPARED"
		| "COMPACTION_INVALIDATED"
		| "COMPACTION_STARTED"
		| "COMPACTION_COMPLETED"
		| "COMPACTION_FAILED"
		| "COMPACTION_INCONSISTENT"
		| "COMPACTION_EXTERNAL"
		| "COMPACTION_BLOCKED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logResumeTransition(
	type:
		| "RESUME_OBLIGATION_CREATED"
		| "RESUME_ATTEMPTED"
		| "RESUME_DELIVERED"
		| "RESUME_FAILED"
		| "RESUME_RETRIED"
		| "RESUME_RECONCILIATION_REQUIRED"
		| "RESUME_OBSOLETED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logAgentMessageTransition(
	type:
		| "AGENT_MESSAGE_ATTEMPTED"
		| "AGENT_MESSAGE_DELIVERED"
		| "AGENT_MESSAGE_FAILED"
		| "AGENT_MESSAGE_QUEUED"
		| "AGENT_MESSAGE_RETRIED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logContinuationAnomaly(
	type: "NO_PROGRESS" | "REPEATED_BLOCK" | "REPEATED_FAILURE",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logUserInteraction(
	type:
		| "CONFIRMATION_REQUESTED"
		| "CONFIRMATION_RECEIVED"
		| "CONFIRMATION_REJECTED"
		| "USER_REFINEMENT_RECEIVED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logCriticalReviewTransition(
	type:
		| "CRITICAL_REVIEW_REQUESTED"
		| "CRITICAL_REVIEW_STARTED"
		| "CRITICAL_REVIEW_PASSED"
		| "CRITICAL_REVIEW_FAILED"
		| "CRITICAL_REVIEW_UNCERTAIN"
		| "CRITICAL_REVIEW_UNAVAILABLE"
		| "CRITICAL_REVIEW_ERROR"
		| "REMEDIATION_REQUIRED"
		| "SELF_CRITIQUE_STARTED"
		| "SELF_CRITIQUE_REVISED",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logSubquestTransition(
	type:
		| "SUBQUEST_START"
		| "SUBQUEST_SWITCH"
		| "SUBQUEST_RETURN"
		| "SUBQUEST_FAILED"
		| "SUBQUEST_RESUME_PENDING"
		| "SUBQUEST_RESUME_FAILED"
		| "SUBQUEST_COMPLETE"
		| "ARCHIVE",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function logRecoveryTransition(
	type: "STATE_INCONSISTENT" | "RECOVERY_STARTED" | "RECOVERY_COMPLETED" | "RECOVERY_FAILED" | "ERROR",
	message: string,
	context?: QuestLogContext,
): void {
	logEvent(type, message, context);
}

export function clearQuestLog(customPath?: string): void {
	try {
		const targetPath = customPath || getQuestLogPath();
		writeFileSync(targetPath, "", "utf8");
	} catch {}
}

export function readQuestLog(customPath?: string): string {
	try {
		const targetPath = customPath || getQuestLogPath();
		return readFileSync(targetPath, "utf8");
	} catch {
		return "";
	}
}

export function mapEventTypeToMajorPhase(type: QuestLogEventType | string, context?: Record<string, string>): MajorPhaseName | null {
	switch (type) {
		case "TOOL_ACTIVITY":
			if (context?.phase) {
				const p = String(context.phase).toUpperCase();
				if (p === "RESEARCH") return "RESEARCH";
				if (p === "PLANNING") return "PLANNING";
				if (p === "IMPLEMENTATION") return "IMPLEMENTATION";
				if (p === "VERIFICATION") return "VERIFICATION";
				if (p === "REASSESSMENT") return "REASSESSMENT";
				if (p === "CHECKPOINT") return "CHECKPOINT";
				if (p === "COMPACTION") return "COMPACTION";
				if (p === "RESUME") return "RESUME";
				if (p === "RECOVERY") return "RECOVERY";
				if (p === "COMPLETION") return "COMPLETION";
			}
			return null;

		case "QUEST_DETECTED":
		case "QUEST_REUSED":
		case "QUEST_CREATED":
		case "QUEST_START":
		case "QUEST_SWITCH":
		case "QUEST_INITIALIZATION_FAILED":
		case "QUEST_ACTIVATION_FAILED":
			return "INITIALIZATION";

		case "RESEARCH_REQUIRED":
		case "RESEARCH_EVIDENCE":
		case "RESEARCH_REJECTED":
		case "RESEARCH_COMPLETED":
			return "RESEARCH";

		case "STATE_UPDATE_REJECTED":
		case "STATE_UPDATE_ACCEPTED":
		case "STATE_UPDATE_FAILED":
		case "STATE_RECONCILIATION_REQUIRED":
			return "PLANNING";

		case "CONFIRMATION_REQUESTED":
		case "CONFIRMATION_RECEIVED":
		case "CONFIRMATION_REJECTED":
		case "USER_REFINEMENT_RECEIVED":
			return "CONFIRMATION";

		case "IMPLEMENTATION_ATTEMPT":
		case "IMPLEMENTATION_ALLOWED":
		case "IMPLEMENTATION_BLOCKED":
		case "IMPLEMENTATION_COMPLETED":
		case "IMPLEMENTATION_FAILED":
		case "UNKNOWN_TOOL":
		case "UNEXPECTED_TOOL_RESULT":
		case "TOOL_CLASSIFICATION_MISMATCH":
		case "TOOL_FAILURE":
		case "TOOL_TIMEOUT":
		case "TOOL_CANCELLED":
			return "IMPLEMENTATION";

		case "TEST_STARTED":
		case "TEST_PASSED":
		case "TEST_FAILED":
		case "BUILD_STARTED":
		case "BUILD_PASSED":
		case "BUILD_FAILED":
		case "TEST_FAILURE":
			return "VERIFICATION";

		case "REASSESSMENT_REQUIRED":
		case "REASSESSMENT_EVIDENCE":
		case "REASSESSMENT_REJECTED":
		case "REASSESSMENT_COMPLETED":
		case "REASSESSMENT_RESOLUTION_FAILED":
			return "REASSESSMENT";

		case "CHECKPOINT":
		case "SAVE_STARTED":
		case "SAVE_VERIFIED":
		case "SAVE_REJECTED":
		case "SAVE_FAILED":
		case "PERSISTENCE_DEGRADED":
		case "PERSISTENCE_RECOVERED":
			return "CHECKPOINT";

		case "COMPACTION_PREPARED":
		case "COMPACTION_INVALIDATED":
		case "COMPACTION_STARTED":
		case "COMPACTION_COMPLETED":
		case "COMPACTION_FAILED":
		case "COMPACTION_INCONSISTENT":
		case "COMPACTION_EXTERNAL":
		case "COMPACTION_BLOCKED":
			return "COMPACTION";

		case "RESUME_OBLIGATION_CREATED":
		case "RESUME_ATTEMPTED":
		case "RESUME_DELIVERED":
		case "RESUME_FAILED":
		case "RESUME_RETRIED":
		case "RESUME_RECONCILIATION_REQUIRED":
		case "RESUME_OBSOLETED":
			return "RESUME";

		case "STATE_INCONSISTENT":
		case "RECOVERY_STARTED":
		case "RECOVERY_COMPLETED":
		case "RECOVERY_FAILED":
		case "ERROR":
		case "NO_PROGRESS":
		case "REPEATED_BLOCK":
		case "REPEATED_FAILURE":
			return "RECOVERY";

		case "SUBQUEST_START":
		case "SUBQUEST_SWITCH":
		case "SUBQUEST_RETURN":
		case "SUBQUEST_FAILED":
		case "SUBQUEST_RESUME_PENDING":
		case "SUBQUEST_RESUME_FAILED":
		case "SUBQUEST_COMPLETE":
		case "ARCHIVE":
			return "COMPLETION";

		case "CRITICAL_REVIEW_REQUESTED":
		case "CRITICAL_REVIEW_STARTED":
		case "CRITICAL_REVIEW_PASSED":
		case "CRITICAL_REVIEW_FAILED":
		case "CRITICAL_REVIEW_UNCERTAIN":
		case "CRITICAL_REVIEW_UNAVAILABLE":
		case "CRITICAL_REVIEW_ERROR":
		case "REMEDIATION_REQUIRED":
		case "SELF_CRITIQUE_STARTED":
		case "SELF_CRITIQUE_REVISED":
			return "VERIFICATION";

		case "GATE_BLOCKED":
		case "GATE_OPENED":
		case "GATE_STATE_CHANGED":
		case "TURN_START":
		case "TURN_END":
		case "AGENT_MESSAGE_ATTEMPTED":
		case "AGENT_MESSAGE_DELIVERED":
		case "AGENT_MESSAGE_FAILED":
		case "AGENT_MESSAGE_QUEUED":
		case "AGENT_MESSAGE_RETRIED":
		default:
			return null;
	}
}

export function summarizeQuestJournalLog(logContentOrPath?: string): QuestRunSummary {
	let rawText = "";
	if (logContentOrPath && logContentOrPath.includes(" | ")) {
		rawText = logContentOrPath;
	} else if (logContentOrPath) {
		rawText = readQuestLog(logContentOrPath);
	} else {
		rawText = readQuestLog();
	}

	const lines = rawText.split("\n").filter((l) => l.trim().length > 0);
	const quests = new Set<string>();
	const majorPhases = new Set<string>();
	let researchCycles = 0;
	let reassessmentCycles = 0;
	let implementationAttempts = 0;
	let implementationAllowedCount = 0;
	let implementationBlockedCount = 0;
	const blockedGates = new Set<string>();
	let failureCount = 0;
	const failures: Array<{ type: string; code?: string; reason?: string }> = [];

	// Logical deduplication maps
	const compactionsMap = new Map<string, { id: string; status: string; phases: Set<string>; success: boolean; failed: boolean; inconsistent: boolean }>();
	let anonCompactionCounter = 0;

	const resumesMap = new Map<string, { id: string; success: boolean; failed: boolean; retried: number; obsolete: boolean }>();
	let anonResumeCounter = 0;

	let hasUnresolvedError = false;
	let hasCriticalReviewFailure = false;
	let criticalReviewPassed = false;
	let lastError: string | undefined;
	const deadlockWarnings: string[] = [];

	for (const line of lines) {
		const entry = parseLogEntry(line);
		if (!entry) continue;

		if (entry.quest && entry.quest !== "(none)") {
			quests.add(entry.quest);
		}

		const majorPhase = mapEventTypeToMajorPhase(entry.type, entry.context);
		if (majorPhase) {
			majorPhases.add(majorPhase);
		}

		switch (entry.type) {
			case "RESEARCH_REQUIRED":
			case "RESEARCH_EVIDENCE":
			case "RESEARCH_COMPLETED":
				if (entry.context.round) {
					const r = parseInt(entry.context.round, 10);
					if (!isNaN(r) && r > researchCycles) researchCycles = r;
				} else if (entry.message.includes("complete") || entry.message.includes("round") || entry.type === "RESEARCH_COMPLETED") {
					researchCycles++;
				}
				break;
			case "REASSESSMENT_REQUIRED":
			case "REASSESSMENT_EVIDENCE":
			case "REASSESSMENT_COMPLETED":
				if (entry.context.version || entry.context.reassessmentVersion) {
					const v = parseInt(entry.context.reassessmentVersion || entry.context.version || "1", 10);
					if (!isNaN(v) && v > reassessmentCycles) reassessmentCycles = v;
				} else {
					reassessmentCycles++;
				}
				break;
			case "IMPLEMENTATION_ATTEMPT":
				implementationAttempts++;
				if (entry.context.allowed === "true" || entry.message.startsWith("allowed")) {
					implementationAllowedCount++;
				}
				break;
			case "IMPLEMENTATION_ALLOWED":
				if (implementationAllowedCount === 0 || implementationAttempts > implementationAllowedCount) {
					implementationAllowedCount++;
				}
				break;
			case "GATE_BLOCKED":
			case "IMPLEMENTATION_BLOCKED":
				implementationBlockedCount++;
				if (entry.context.gate) {
					blockedGates.add(entry.context.gate);
				} else if (entry.message.includes("RESEARCH_PENDING")) {
					blockedGates.add("RESEARCH_PENDING");
				}
				break;
			case "TOOL_FAILURE":
			case "TEST_FAILURE":
			case "TEST_FAILED":
			case "BUILD_FAILED":
			case "TOOL_TIMEOUT":
			case "TOOL_CANCELLED":
				failureCount++;
				failures.push({
					type: entry.type,
					code: entry.context.code,
					reason: entry.context.reason || entry.message,
				});
				break;
			case "ERROR":
			case "SAVE_FAILED":
			case "RESUME_FAILED":
			case "RECOVERY_FAILED":
				failureCount++;
				hasUnresolvedError = true;
				lastError = entry.message;
				failures.push({
					type: entry.type,
					code: entry.context.code,
					reason: entry.context.reason || entry.message,
				});
				break;
			case "COMPACTION_PREPARED":
			case "COMPACTION_INVALIDATED":
			case "COMPACTION_STARTED":
			case "COMPACTION_COMPLETED":
			case "COMPACTION_FAILED":
			case "COMPACTION_INCONSISTENT":
			case "COMPACTION_EXTERNAL":
			case "COMPACTION_BLOCKED": {
				if (entry.type === "COMPACTION_FAILED") {
					failureCount++;
					hasUnresolvedError = true;
					lastError = entry.message;
					failures.push({
						type: entry.type,
						code: entry.context.code,
						reason: entry.context.reason || entry.message,
					});
				}
				if (entry.type === "COMPACTION_BLOCKED" || entry.type === "COMPACTION_INVALIDATED") {
					break;
				}
				const cmpId = entry.context.compactionId || (entry.quest && entry.quest !== "(none)" ? `cmp_${entry.quest}` : `cmp_anon_${++anonCompactionCounter}`);
				if (!compactionsMap.has(cmpId)) {
					compactionsMap.set(cmpId, {
						id: cmpId,
						status: entry.message,
						phases: new Set([entry.type]),
						success: entry.type === "COMPACTION_COMPLETED",
						failed: entry.type === "COMPACTION_FAILED",
						inconsistent: entry.type === "COMPACTION_INCONSISTENT" || entry.type === "COMPACTION_EXTERNAL",
					});
				} else {
					const existing = compactionsMap.get(cmpId)!;
					existing.phases.add(entry.type);
					existing.status = entry.message;
					if (entry.type === "COMPACTION_COMPLETED") existing.success = true;
					if (entry.type === "COMPACTION_FAILED") existing.failed = true;
					if (entry.type === "COMPACTION_INCONSISTENT" || entry.type === "COMPACTION_EXTERNAL") existing.inconsistent = true;
				}
				break;
			}
			case "RESUME_OBLIGATION_CREATED":
			case "RESUME_ATTEMPTED":
			case "RESUME_DELIVERED":
			case "RESUME_FAILED":
			case "RESUME_RETRIED":
			case "RESUME_RECONCILIATION_REQUIRED":
			case "RESUME_OBSOLETED": {
				const resId = entry.context.compactionId || entry.context.obligationId || entry.context.id || entry.context.child || (entry.quest && entry.quest !== "(none)" ? `res_${entry.quest}` : `res_anon_${++anonResumeCounter}`);
				if (!resumesMap.has(resId)) {
					resumesMap.set(resId, {
						id: resId,
						success: entry.type === "RESUME_DELIVERED",
						failed: entry.type === "RESUME_FAILED",
						retried: entry.type === "RESUME_RETRIED" ? 1 : 0,
						obsolete: entry.type === "RESUME_OBSOLETED",
					});
				} else {
					const existing = resumesMap.get(resId)!;
					if (entry.type === "RESUME_DELIVERED") {
						existing.success = true;
						existing.failed = false;
					} else if (entry.type === "RESUME_FAILED") {
						if (!existing.success) existing.failed = true;
					} else if (entry.type === "RESUME_RETRIED") {
						existing.retried++;
					} else if (entry.type === "RESUME_OBSOLETED") {
						existing.obsolete = true;
					}
				}
				if (hasUnresolvedError && entry.type === "RESUME_DELIVERED") {
					hasUnresolvedError = false;
				}
				break;
			}
			case "CRITICAL_REVIEW_PASSED":
				hasCriticalReviewFailure = false;
				criticalReviewPassed = true;
				break;
			case "CRITICAL_REVIEW_FAILED":
			case "CRITICAL_REVIEW_UNCERTAIN":
			case "CRITICAL_REVIEW_ERROR":
				hasCriticalReviewFailure = true;
				criticalReviewPassed = false;
				failureCount++;
				failures.push({
					type: entry.type,
					code: entry.context.code || entry.type,
					reason: entry.context.reason || entry.message,
				});
				break;
			case "NO_PROGRESS":
			case "REPEATED_BLOCK":
			case "REPEATED_FAILURE":
				deadlockWarnings.push(`${entry.type}: ${entry.message}`);
				break;
			default:
				break;
		}
	}

	const compactionCount = compactionsMap.size;
	let successfulCompactions = 0;
	let failedCompactions = 0;
	let inconsistentCompactions = 0;
	const compactions: Array<{ id?: string; status: string; phase?: string }> = [];

	for (const cmp of compactionsMap.values()) {
		if (cmp.success) successfulCompactions++;
		if (cmp.failed) failedCompactions++;
		if (cmp.inconsistent) inconsistentCompactions++;
		compactions.push({
			id: cmp.id,
			status: cmp.status,
			phase: Array.from(cmp.phases).pop(),
		});
	}

	const resumeCount = resumesMap.size;
	let resumeSuccessCount = 0;
	let resumeFailedCount = 0;
	let resumePendingCount = 0;

	for (const res of resumesMap.values()) {
		if (res.success) resumeSuccessCount++;
		else if (res.failed) resumeFailedCount++;
		else if (!res.obsolete) resumePendingCount++;
	}

	const formattedLines: string[] = [
		`=== Quest Journal Run Summary ===`,
		`Quests Tracked (${quests.size}): ${Array.from(quests).join(", ") || "(none)"}`,
		`Phases Observed: ${Array.from(majorPhases).join(", ") || "(none)"}`,
		`Research Rounds: ${researchCycles}`,
		`Reassessment Cycles: ${reassessmentCycles}`,
		`Implementation Attempts: ${implementationAttempts} (allowed: ${implementationAllowedCount}, blocked: ${implementationBlockedCount})`,
		`Blocked Gates: ${Array.from(blockedGates).join(", ") || "(none)"}`,
		`Compactions (${compactionCount}): Total ${compactionCount} (successful: ${successfulCompactions}, failed: ${failedCompactions}, inconsistent/external: ${inconsistentCompactions})`,
		`Resumes (${resumeCount}): Total ${resumeCount} (successful: ${resumeSuccessCount}, failed: ${resumeFailedCount}, pending: ${resumePendingCount})`,
		`Total Failures: ${failureCount}`,
	];

	if (deadlockWarnings.length > 0) {
		formattedLines.push(`Flow Warnings / Deadlocks (${deadlockWarnings.length}):`);
		for (const w of deadlockWarnings.slice(0, 5)) {
			formattedLines.push(`  - ${w}`);
		}
	}

	if (failures.length > 0) {
		formattedLines.push(`Recorded Failures (${failures.length}):`);
		for (const f of failures.slice(0, 5)) {
			formattedLines.push(`  - [${f.type}] ${f.code ? `(${f.code}) ` : ""}${f.reason || "unknown"}`);
		}
		if (failures.length > 5) {
			formattedLines.push(`  ... and ${failures.length - 5} more failures`);
		}
	}

	if (hasUnresolvedError) {
		formattedLines.push(`Status: UNRESOLVED ERROR (${lastError || "unknown"})`);
	} else {
		formattedLines.push(`Status: CLEAN / RECOVERED`);
	}

	return {
		quests: Array.from(quests),
		majorPhases: Array.from(majorPhases),
		researchCycles,
		reassessmentCycles,
		implementationAttempts,
		implementationAllowedCount,
		implementationBlockedCount,
		blockedGates: Array.from(blockedGates),
		failureCount,
		failures,
		compactionCount,
		successfulCompactions,
		failedCompactions,
		inconsistentCompactions,
		compactions,
		resumeCount,
		resumeSuccessCount,
		resumeFailedCount,
		resumePendingCount,
		hasUnresolvedError,
		hasCriticalReviewFailure,
		criticalReviewPassed,
		lastError,
		deadlockWarnings,
		formattedSummary: formattedLines.join("\n"),
	};
}
