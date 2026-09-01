import { QuestLogContext, QuestLogEntry, QuestLogEventType } from "./types.ts";

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
		"intent",
		"phase",
		"tool",
		"operation",
		"action",
		"targetAction",
		"result",
		"consequence",
		"failureId",
		"recoveryFor",
		"recoveryConclusion",
		"path",
		"command",
		"query",
		"target",
		"gate",
		"activeGate",
		"outcome",
		"filesModified",
		"testStatus",
		"completedTasks",
		"remainingTasks",
		"reason",
		"code",
		"requiredAction",
		"compactionId",
		"obligationId",
		"reviewId",
		"childSessionId",
		"parentSessionId",
		"subquest",
		"parent",
		"child",
		"round",
		"version",
		"planVersion",
		"reassessmentVersion",
		"reviewedVersion",
		"supersededByVersion",
		"from",
		"to",
		"tools",
		"kind",
		"reviewKind",
		"triggerReason",
		"boundaryKey",
		"category",
		"categories",
		"severity",
		"verdict",
		"reads",
		"searches",
		"writes",
		"commands",
		"files",
		"evidence",
		"allowed",
		"attempt",
		"checkpoint",
		"type",
		"status",
		"gen",
		"hash",
		"durationMs",
		"timeoutLayer",
		"deliverAs",
		"substantive",
		"toolsUsed",
		"mutations",
		"failures",
		"questDirty",
		"implementationAllowed",
		"turns",
		"count",
		"permission",
		"investigation",
		"error",
		"draftPromptsCount",
		"attemptKey",
		"attempts",
		"requireConfirm",
		"syntheticPrefix",
		"classification",
		"boundaryKey",
		"hash",
		"ref",
		"intentHash",
		"intentLen",
		"slice",
		"elapsedMs",
		"opencodeSessionId",
		"lockKey",
		"waitMs",
		"holdMs",
		"contention",
		"shard",
		"staleCount",
		"candidateCount",
		"chosenKind",
		"thoughtHash",
		"thoughtLen",
		"thoughtSlice",
		"semanticSummaryEnabled",
		"thoughtLoggingEnabled",
	];

	for (const key of priorityKeys) {
		if (context[key] !== undefined && context[key] !== null) {
			let val = sanitizeLogString(context[key]);
			if (val.includes(" ")) {
				val = `"${val}"`;
			}
			parts.push(`${key}=${val}`);
		}
	}

	for (const [k, v] of Object.entries(context)) {
		if (!priorityKeys.includes(k) && k !== "quest" && v !== undefined && v !== null) {
			let val = sanitizeLogString(v);
			if (val.includes(" ")) {
				val = `"${val}"`;
			}
			parts.push(`${k}=${val}`);
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
	const questName = sanitizeLogString(context?.quest !== undefined ? context.quest : "", 60);
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
