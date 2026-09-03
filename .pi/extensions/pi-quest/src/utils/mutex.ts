// Per-quest async mutex — promise chain. Clear, predictable, efficient.
// Ensures state mutations for the same quest are serialized even when reviewers complete concurrently.
import { existsSync, openSync, closeSync, unlinkSync, mkdirSync, statSync, writeSync, utimesSync, readFileSync } from "node:fs";
import { join } from "node:path";
import { QUEST_CURRENT_DIR, REVIEW_LOCK_STALE_MS_DEFAULT } from "../constants.ts";
import { questDirPath } from "../paths.ts";
import { logEvent } from "../logging.ts";
import { getReviewLockStaleMs } from "../config.ts";

export const REVIEW_LOCK_FILE = ".review.lock";
export const REVIEW_ACTIVE_FILE = ".review.active";
export const REVIEW_LOCK_STALE_MS = REVIEW_LOCK_STALE_MS_DEFAULT;

export const REVIEW_LOCK_HEARTBEAT_MS = 10_000;

/** Refreshes the mtime of the lock file to prevent stale-lock recovery during a long review. */
export function touchReviewLockFile(lockPath: string): void {
	try { utimesSync(lockPath, new Date(), new Date()); } catch {}
}

/**
 * Starts heartbeating the .review.lock file so it outlives the 30 s stale threshold
 * for the full duration of the review. Returns a stop() function.
 */
export function startReviewLockHeartbeat(
	questId: string | null | undefined,
	lockPath: string,
): () => void {
	logEvent("REVIEW_LOCK_HEARTBEAT_STARTED", `review lock heartbeat started lockPath=${lockPath}`, {
		quest: questId || "",
		lockPath,
		intervalMs: REVIEW_LOCK_HEARTBEAT_MS,
	});
	const iv = setInterval(() => touchReviewLockFile(lockPath), REVIEW_LOCK_HEARTBEAT_MS);
	return () => {
		clearInterval(iv);
		logEvent("REVIEW_LOCK_HEARTBEAT_STOPPED", `review lock heartbeat stopped lockPath=${lockPath}`, {
			quest: questId || "",
			lockPath,
		});
	};
}

function getStaleMs(): number {
	try { return getReviewLockStaleMs(); } catch { return REVIEW_LOCK_STALE_MS_DEFAULT; }
}

const questLockChains = new Map<string, Promise<void>>();
const heldQuestLocks = new Set<string>();
const heldFileLocks = new Set<string>();

export async function withQuestLock<T>(key: string, fn: () => T | Promise<T>, getActiveCount?: () => number): Promise<T> {
	// re-entrant: if already held in this async chain, just run
	if (heldQuestLocks.has(key)) {
		return await fn();
	}
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
		logEvent("MUTEX_WAIT", `mutex wait ${key}`, { lockKey: key, waitMs });
	}
	const holdStart = Date.now();
	heldQuestLocks.add(key);
	try {
		return await fn();
	} finally {
		const holdMs = Date.now() - holdStart;
		let activeCount = 0;
		try { if (getActiveCount) activeCount = getActiveCount(); } catch {}
		let questForLog = "";
		try {
			if (key.startsWith("review:")) questForLog = key.slice("review:".length).split(":")[0];
			else if (key.startsWith("global:")) questForLog = "";
		} catch {}
		const logCtx: any = { lockKey: key, holdMs, waitMs, contention: hadContention, activeCount };
		if (questForLog) logCtx.quest = questForLog;
		// Explicit questId ensures log goes to correct quest file even when global state is stale
		if (questForLog) logCtx.questId = questForLog;
		logEvent("MUTEX_ACQUIRED", `mutex acquired ${key}`, logCtx);
		heldQuestLocks.delete(key);
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

export function getReviewActivePath(questId: string | null | undefined): string {
	const qid = questId || "quest";
	const dir = questDirPath(qid);
	return dir ? join(dir, REVIEW_ACTIVE_FILE) : `${QUEST_CURRENT_DIR}/${qid}/${REVIEW_ACTIVE_FILE}`;
}

export function isReviewActive(questId: string | null | undefined): boolean {
	const p = getReviewActivePath(questId);
	try { return existsSync(p); } catch { return false; }
}

export function createReviewActiveFile(questId: string | null | undefined, reviewId: string): void {
	const p = getReviewActivePath(questId);
	try {
		const dir = p.slice(0, p.lastIndexOf("/"));
		if (dir && !existsSync(dir)) try { mkdirSync(dir, { recursive: true }); } catch {}
		const fd = openSync(p, "w");
		try { writeSync(fd, reviewId); } catch {}
		try { closeSync(fd); } catch {}
	} catch {}
}

export function removeReviewActiveFile(questId: string | null | undefined): void {
	const p = getReviewActivePath(questId);
	try { if (existsSync(p)) unlinkSync(p); } catch {}
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
					logEvent("REVIEW_LOCK_STALE_RECOVERED", `review lock stale recovered age=${Math.round(age)}`, { quest: questId || "", lockPath, ageMs: age, staleMs });
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
					try { unlinkSync(lockPath); logEvent("REVIEW_LOCK_STALE_RECOVERED", `review lock stale recovered (race) age=${Math.round(age)}`, { quest: questId || "", lockPath, ageMs: age, staleMs }); } catch {}
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

export async function withReviewFileLock<T>(questId: string | null | undefined, fn: () => T | Promise<T>, opts?: { waitMs?: number; retries?: number }): Promise<T> {
	const qKey = getReviewLockKey(questId);
	const lockPath = getReviewLockPath(questId);
	// re-entrant: if already held, just run
	if (heldFileLocks.has(lockPath) || heldQuestLocks.has(qKey)) {
		return await fn();
	}
	// per-process promise chain first
	return await withQuestLock(qKey, async () => {
		const maxRetries = opts?.retries ?? 100;
		const waitPerRetry = opts?.waitMs ?? 20;
		let attempts = 0;
		let acquiredPath: string | null = null;
		let owned = false;
		const startWait = Date.now();
		while (attempts <= maxRetries) {
			const res = acquireReviewFileLock(questId);
			if (res.acquired) {
				acquiredPath = res.path;
				owned = true;
				heldFileLocks.add(acquiredPath);
				if (attempts > 0) {
					const waitMs = Date.now() - startWait;
					try { logEvent("MUTEX_WAIT", `mutex wait file ${res.path} retries=${attempts}`, { lockKey: qKey, waitMs, retries: attempts, lockPath: res.path }); } catch {}
				}
				break;
			}
			// not acquired, check stale already handled inside acquire; if still EEXIST, wait
			if (attempts === maxRetries) {
				try { logEvent("MUTEX_WAIT", `mutex wait file timeout ${res.path}`, { lockKey: qKey, waitMs: Date.now() - startWait, retries: attempts, lockPath: res.path }); } catch {}
				throw new Error(`Review file lock timeout for ${questId}: ${res.path}`);
			}
			attempts++;
			await new Promise((r) => setTimeout(r, waitPerRetry));
		}
		if (!owned || !acquiredPath) throw new Error(`Failed to acquire review file lock for ${questId}`);
		try {
			const result = await fn();
			return result;
		} finally {
			try { releaseReviewFileLock(acquiredPath!, owned); } catch {}
			try { heldFileLocks.delete(acquiredPath!); } catch {}
		}
	});
}

// ---------------------------------------------------------------------------
// Durable session-liveness witness (#56)
//
// QUEST_REUSED mount coalescence needs to know whether ANOTHER live session is
// already actively working on the same quest. The in-process questLockChains and
// the `.review.lock` file only serialize concurrent mounts; neither refutes a
// second pi process re-mounting the same quest across separate invocations. So we
// write a durable per-session liveness marker under current/<qid>/ at mount time,
// mirroring the `.review.lock`/`getStaleMs()` staleness idiom so a crashed session's
// stale marker does not permanently block a legitimate remount.
// ---------------------------------------------------------------------------
export const SESSION_LIVENESS_FILE = ".session.liveness";

export function getSessionLivenessPath(questId?: string | null): string {
	const dir = questDirPath(questId);
	return dir ? join(dir, SESSION_LIVENESS_FILE) : "";
}

export function getSessionLivenessStaleMs(): number {
	return getStaleMs();
}

/** Writes (or refreshes) the durable per-session liveness marker for a quest. */
export function writeSessionLiveness(questId?: string | null, sessionId?: string | null): void {
	const path = getSessionLivenessPath(questId);
	if (!path) return;
	const dir = path.slice(0, path.lastIndexOf("/"));
	try { if (dir && !existsSync(dir)) mkdirSync(dir, { recursive: true }); } catch {}
	try {
		const payload = `sessionId=${sessionId || ""}\n`;
		const fd = openSync(path, "w");
		try { writeSync(fd, payload); } finally { closeSync(fd); }
	} catch {}
}

/** Heartbeats the liveness marker so a long-lived session isn't treated as stale. */
export function touchSessionLiveness(questId?: string | null): void {
	const path = getSessionLivenessPath(questId);
	if (!path) return;
	try { utimesSync(path, new Date(), new Date()); } catch {}
}

/** Starts a heartbeat interval for the session-liveness marker; returns stop(). */
export function startSessionLivenessHeartbeat(questId?: string | null): () => void {
	const iv = setInterval(() => touchSessionLiveness(questId), REVIEW_LOCK_HEARTBEAT_MS);
	return () => { clearInterval(iv); };
}

export interface SessionLivenessInfo {
	sessionId: string;
	ts: number;
	fresh: boolean;
}

/**
 * Reads the durable liveness marker. `fresh` is false if the marker is absent,
 * unreadable, or older than the stale threshold (crash recovery). Mirrors the
 * stale-recovery semantics of `acquireReviewFileLock`.
 */
export function readSessionLiveness(questId?: string | null): SessionLivenessInfo | null {
	const path = getSessionLivenessPath(questId);
	if (!path || !existsSync(path)) return null;
	try {
		const st = statSync(path);
		const ts = st.mtimeMs;
		const age = Date.now() - ts;
		const fresh = age <= getStaleMs();
		let sessionId = "";
		try {
			const raw = readFileSync(path, "utf8") as string;
			const m = raw.match(/sessionId=([^\n]*)/);
			if (m) sessionId = m[1].trim();
		} catch {}
		return { sessionId, ts, fresh };
	} catch {
		return null;
	}
}

/**
 * True when a FRESH liveness marker exists for a DIFFERENT live session than
 * `mySessionId`. Used at mount time to refuse/coalesce a second session. A stale
 * marker (crashed session) or the same session's own marker never blocks.
 */
export function isQuestSessionActive(questId?: string | null, mySessionId?: string | null): boolean {
	const info = readSessionLiveness(questId);
	if (!info || !info.fresh) return false;
	if (!info.sessionId || !mySessionId) return info.fresh && !!info.sessionId;
	if (info.sessionId === mySessionId) return false;
	return true;
}

/** Force-ages the liveness marker (test + crash-recovery helper). */
export function setSessionLivenessAsStale(questId?: string | null): void {
	const path = getSessionLivenessPath(questId);
	if (!path) return;
	try {
		const old = new Date(Date.now() - getStaleMs() - 60_000);
		utimesSync(path, old, old);
	} catch {}
}

/** Removes the liveness marker (clean teardown). */
export function removeSessionLiveness(questId?: string | null): void {
	const path = getSessionLivenessPath(questId);
	if (!path) return;
	try { if (existsSync(path)) unlinkSync(path); } catch {}
}

