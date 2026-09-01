// Per-quest async mutex — promise chain. Clear, predictable, efficient.
// Ensures state mutations for the same quest are serialized even when reviewers complete concurrently.
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
		try {
			const { logEvent } = await import("../logging/core.ts");
			logEvent("MUTEX_WAIT", `mutex wait ${key}`, { lockKey: key, waitMs } as any);
		} catch {}
	}
	const holdStart = Date.now();
	try {
		return await fn();
	} finally {
		const holdMs = Date.now() - holdStart;
		try {
			const { logEvent } = await import("../logging/core.ts");
			logEvent("MUTEX_ACQUIRED", `mutex acquired ${key}`, { lockKey: key, holdMs, waitMs, contention: hadContention } as any);
		} catch {}
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

export function isQuestLocked(key: string): boolean {
	return questLockChains.has(key);
}

export function clearAllQuestLocks(): void {
	questLockChains.clear();
}
