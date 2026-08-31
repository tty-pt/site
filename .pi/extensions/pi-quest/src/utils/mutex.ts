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
	if (existing) await prev;
	try {
		return await fn();
	} finally {
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
