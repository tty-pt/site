// Per-quest async mutex — promise chain. Clear, predictable, efficient.
// Ensures state mutations for the same quest are serialized even when reviewers complete concurrently.
import { existsSync, openSync, closeSync, unlinkSync, mkdirSync, statSync } from "node:fs";
import { join } from "node:path";
import { QUEST_CURRENT_DIR, REVIEW_LOCK_STALE_MS_DEFAULT } from "../constants.ts";
import { questDirPath } from "../paths.ts";
import { logEvent } from "../logging.ts";
import { getReviewLockStaleMs } from "../config.ts";

export const REVIEW_LOCK_FILE = ".review.lock";
export const REVIEW_LOCK_STALE_MS = REVIEW_LOCK_STALE_MS_DEFAULT;

function getStaleMs(): number {
	try { return getReviewLockStaleMs(); } catch { return REVIEW_LOCK_STALE_MS_DEFAULT; }
}

const questLockChains = new Map<string, Promise<void>>();

export async function withQuestLock<T>(key: string, fn: () => T | Promise<T>): Promise<T> {
	const existing = questLockChains.get(key);
	const prev = existing ?? Promise.resolve();
	let release!: () => void;
	const next = new Promise<void>((resolve) => { release = resolve; });
	const chain = prev.then(() => next);
	questLockChains.set(key, chain);
	chain.catch(() => {});
	const hadContention = !!existing;
	let waitMs = 0;
	if (existing) {
		const start = Date.now();
		await prev;
		waitMs = Date.now() - start;
		logEvent("MUTEX_WAIT", `mutex wait ${key}`, { lockKey: key, waitMs } as any);
	}
	const holdStart = Date.now();
	try {
		return await fn();
	} finally {
		const holdMs = Date.now() - holdStart;
		logEvent("MUTEX_ACQUIRED", `mutex acquired ${key}`, { lockKey: key, holdMs, waitMs, contention: hadContention } as any);
		release();
		if (questLockChains.get(key) === chain) {
			questLockChains.delete(key);
		}
	}
}

export function getQuestLockKey(questId: string | null | undefined, sessionId?: string): string {
	const q = questId || "quest";
	const s = sessionId ? `:${sessionId}` : "";
	return `${q}${s}`;
}

export function getGlobalReviewLockKey(sessionId?: string): string {
	return `global:review:${sessionId ?? "default"}`;
}

export function getReviewLockKey(questId: string | null | undefined): string {
	return `review:${questId || "quest"}`;
}

export function getGlobalReviewLockKeyForQuest(_questId?: string | null): string {
	return "global:review";
}

export function getReviewLockPath(questId: string | null | undefined): string {
	const qid = questId || "quest";
	const dir = questDirPath(qid);
	return dir ? join(dir, REVIEW_LOCK_FILE) : `${QUEST_CURRENT_DIR}/${qid}/${REVIEW_LOCK_FILE}`;
}

export function isQuestLocked(key: string): boolean {
	return questLockChains.has(key);
}

export function clearAllQuestLocks(): void {
	questLockChains.clear();
}

export function acquireReviewFileLock(questId: string | null | undefined): { acquired: boolean; path: string; staleRecovered?: boolean; error?: string } {
	const lockPath = getReviewLockPath(questId);
	try {
		const dir = lockPath.slice(0, lockPath.lastIndexOf("/"));
		if (dir && !existsSync(dir)) {
			try { mkdirSync(dir, { recursive: true }); } catch {}
		}
		if (existsSync(lockPath)) {
			try {
				const st = statSync(lockPath);
				const age = Date.now() - st.mtimeMs;
				const staleMs = getStaleMs();
				if (age > staleMs) {
					try { unlinkSync(lockPath); } catch {}
					logEvent("REVIEW_LOCK_STALE_RECOVERED" as any, `review lock stale recovered age=${Math.round(age)}`, { quest: questId || "", lockPath, ageMs: age, staleMs } as any);
					// retry acquire once after stale unlink
				} else {
					return { acquired: false, path: lockPath, error: "EEXIST" };
				}
			} catch {
				return { acquired: false, path: lockPath, error: "EEXIST" };
			}
		}
		const fd = openSync(lockPath, "wx");
		try { closeSync(fd); } catch {}
		return { acquired: true, path: lockPath, staleRecovered: false };
	} catch (e: any) {
		const code = e?.code || String(e);
		if (code === "EEXIST" || code.includes("EEXIST")) {
			// second check for stale after race
			try {
				const st = statSync(lockPath);
				const age = Date.now() - st.mtimeMs;
				const staleMs = getStaleMs();
				if (age > staleMs) {
					try { unlinkSync(lockPath); logEvent("REVIEW_LOCK_STALE_RECOVERED" as any, `review lock stale recovered (race) age=${Math.round(age)}`, { quest: questId || "", lockPath, ageMs: age, staleMs } as any); } catch {}
					const fd2 = openSync(lockPath, "wx");
					try { closeSync(fd2); } catch {}
					return { acquired: true, path: lockPath, staleRecovered: true };
				}
			} catch {}
			return { acquired: false, path: lockPath, error: "EEXIST" };
		}
		return { acquired: false, path: lockPath, error: code };
	}
}

export function releaseReviewFileLock(lockPath: string, owned: boolean): void {
	if (!owned || !lockPath) return;
	try { if (existsSync(lockPath)) unlinkSync(lockPath); } catch {}
}
