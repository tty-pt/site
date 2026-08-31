import { existsSync, mkdirSync } from "node:fs";
import { dirname, join } from "node:path";
import { QUEST_CURRENT_DIR } from "../constants.ts";
import { getActiveContext, getState } from "../state.ts";
import { ExtensionContext } from "../types.ts";

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
