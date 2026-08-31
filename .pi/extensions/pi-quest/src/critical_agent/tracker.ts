import { ActiveReview, CriticalReviewKind, ExtensionContext, PendingReviewRequest, ReviewActivityStats, ReviewSnapshot, StoredState } from "../types.ts";
import { getActiveContext } from "../state.ts";
import { computeContentHash } from "../utils/files.ts";
import { clearAllQuestLocks } from "../utils/mutex.ts";

const activeReviews = new Map<string, ActiveReview>();
const pendingReviews = new Map<string, PendingReviewRequest>();
let latestCompletedStatus: { text: string; updatedAt: number } | null = null;

export function getActiveReviews(): Map<string, ActiveReview> {
	return activeReviews;
}

export function getPendingReviews(): Map<string, PendingReviewRequest> {
	const copy = new Map(pendingReviews);
	const origGet = copy.get.bind(copy);
	(copy as any).get = (k: string) => {
		const direct = origGet(k);
		if (direct) return direct as PendingReviewRequest;
		for (const v of pendingReviews.values()) {
			if (v.questSlug === k) return v;
		}
		return undefined;
	};
	return copy as Map<string, PendingReviewRequest>;
}

export function pendingKey(slug: string, kind: CriticalReviewKind): string {
	return `${slug}:${kind}`;
}

export function getPendingReview(questSlug: string, kind?: CriticalReviewKind): PendingReviewRequest | undefined {
	if (kind) return pendingReviews.get(pendingKey(questSlug, kind));
	return pendingReviews.get(questSlug) ?? [...pendingReviews.values()].find((r) => r.questSlug === questSlug);
}

export function getAllPendingForSlug(questSlug: string): PendingReviewRequest[] {
	return [...pendingReviews.values()].filter((r) => r.questSlug === questSlug);
}

export function setPendingReview(questSlug: string, req: PendingReviewRequest): void {
	pendingReviews.set(pendingKey(questSlug, req.kind), req);
}

export function clearPendingReview(questSlug: string, kind?: CriticalReviewKind): void {
	if (kind) {
		pendingReviews.delete(pendingKey(questSlug, kind));
		return;
	}
	for (const k of [...pendingReviews.keys()]) {
		if (k === questSlug || k.startsWith(`${questSlug}:`)) pendingReviews.delete(k);
	}
}

export function clearActiveReviews(): void {
	activeReviews.clear();
	pendingReviews.clear();
	latestCompletedStatus = null;
	clearAllQuestLocks();
}

export function createEmptyActivityStats(): ReviewActivityStats {
	return {
		turns: 0,
		tools: 0,
		reads: 0,
		searches: 0,
		writes: 0,
		commands: 0,
		files: 0,
		lastActivityAt: Date.now(),
		observedFilePaths: [],
	};
}

export function buildReviewBoundaryKey(
	questSlug: string,
	kind: CriticalReviewKind,
	planVersion: number,
	stateHash?: string | null,
	saveCount?: number,
): string {
	const hashPart = stateHash || saveCount || "0";
	return `${questSlug}:${kind}:v${planVersion}:h${hashPart}`;
}

export function computePlanReviewBoundaryKey(
	questSlug: string,
	planVersion: number,
	normalizedPlan: string,
	normalizedRevisions?: string,
): string {
	const content = `${normalizedPlan || ""}\n---\n${normalizedRevisions || ""}`;
	const digest = computeContentHash(content);
	return `${questSlug}:plan_review:v${planVersion}:${digest}`;
}

export function findActiveReviewForQuest(
	questSlug: string,
): ActiveReview | undefined {
	for (const review of activeReviews.values()) {
		if (
			(review.questSlug === questSlug || review.questId === questSlug || (review.snapshot && review.snapshot.questId === questSlug)) &&
			(review.status === "starting" || review.status === "running")
		) {
			return review;
		}
	}
	return undefined;
}

export function findActiveReviewByBoundary(
	questSlug: string,
	kind: CriticalReviewKind,
	planVersion: number,
	stateHash?: string | null,
): ActiveReview | undefined {
	return findActiveReviewForQuest(questSlug);
}

export function canLaunchReview(
	questSlug: string,
	kind: CriticalReviewKind,
	planVersion?: number,
	stateHash?: string | null,
	targetState?: StoredState,
): { allowed: boolean; reason?: string; existingReview?: ActiveReview } {
	// 1. Consult inCriticalReview flag on the quest state
	if (targetState && (targetState.active === questSlug || targetState.questId === questSlug) && targetState.inCriticalReview) {
		const existing = findActiveReviewForQuest(questSlug);
		return {
			allowed: false,
			reason: `Quest ${questSlug} is already in active critical review (inCriticalReview=true)`,
			existingReview: existing,
		};
	}

	// 2. Strict Invariant: ONE QUEST -> ZERO OR ONE ACTIVE CRITICAL REVIEW
	const existing = findActiveReviewForQuest(questSlug);
	if (existing) {
		return {
			allowed: false,
			reason: `Active review ${existing.reviewId} (${existing.kind}, v${existing.snapshot.planVersion}) is already running for quest ${questSlug}`,
			existingReview: existing,
		};
	}

	// 3. Final acceptance limit: at most 1 active final reviewer across session
	if (kind === "final_acceptance") {
		for (const rev of activeReviews.values()) {
			if (rev.kind === "final_acceptance" && (rev.status === "starting" || rev.status === "running")) {
				return {
					allowed: false,
					reason: `Final acceptance review ${rev.reviewId} is already active`,
					existingReview: rev,
				};
			}
		}
	}

	return { allowed: true };
}

export function registerActiveReview(
	reviewId: string,
	questSlug: string,
	parentSessionId: string,
	kind: CriticalReviewKind,
	snapshot: ReviewSnapshot,
	promise?: Promise<any>,
	triggerReason?: string,
): ActiveReview {
	const existing = findActiveReviewForQuest(questSlug);
	if (existing) {
		throw new Error(`Invariant violated: active review ${existing.reviewId} already running for quest ${questSlug} (attempt to register ${reviewId} kind=${kind})`);
	}
	const active: ActiveReview = {
		reviewId,
		parentSessionId,
		questId: snapshot.questId,
		questSlug,
		kind,
		triggerReason,
		snapshot,
		startedAt: Date.now(),
		activity: createEmptyActivityStats(),
		status: "starting",
		promise,
	};
	activeReviews.set(reviewId, active);
	return active;
}

export function updateReviewActivity(
	reviewId: string,
	eventData: any,
	ctx?: ExtensionContext,
): ReviewActivityStats | null {
	const rev = activeReviews.get(reviewId);
	if (!rev) return null;

	rev.status = "running";
	rev.activity.lastActivityAt = Date.now();

	const eventName = (eventData?.event || eventData?.type || "").toLowerCase();
	const toolName = (eventData?.toolName || eventData?.name || eventData?.tool || "").toLowerCase();
	const filePath = eventData?.path || eventData?.file || eventData?.input?.path || eventData?.args?.path;

	if (eventData?.childSessionId && !rev.childSessionId) {
		rev.childSessionId = eventData.childSessionId;
	}

	if (eventName.includes("turn") || eventName.includes("turn_start") || eventName.includes("turn_end")) {
		if (typeof eventData?.turnIndex === "number") {
			rev.activity.turns = Math.max(rev.activity.turns, eventData.turnIndex);
		} else {
			rev.activity.turns++;
		}
	}

	if (toolName || eventName.includes("tool")) {
		rev.activity.tools++;
		rev.activity.lastTool = toolName;

		if (toolName === "read" || toolName === "doc_to_md" || toolName === "grep") {
			rev.activity.reads++;
		} else if (toolName === "search_graph" || toolName === "search_code" || toolName === "web_search" || toolName === "find") {
			rev.activity.searches++;
		} else if (toolName === "bash") {
			rev.activity.commands++;
		} else if (toolName === "edit" || toolName === "write") {
			rev.activity.writes++;
		}

		if (filePath && typeof filePath === "string") {
			if (!rev.activity.observedFilePaths) rev.activity.observedFilePaths = [];
			if (!rev.activity.observedFilePaths.includes(filePath)) {
				rev.activity.observedFilePaths.push(filePath);
				rev.activity.files = rev.activity.observedFilePaths.length;
			}
		}
	}

	updateReviewerUIStatus(ctx);
	return rev.activity;
}

export function completeActiveReview(
	reviewId: string,
	verdict?: string,
	error?: string,
	ctx?: ExtensionContext,
): void {
	const rev = activeReviews.get(reviewId);
	if (rev) {
		if (error) {
			rev.status = "failed";
			rev.error = error;
			latestCompletedStatus = {
				text: "⚖ Critical: reviewer ! ERROR",
				updatedAt: Date.now(),
			};
		} else {
			rev.status = "completed";
			rev.verdict = verdict as any;
			const symbol = (verdict === "APPROVE" || verdict === "PASS") ? "✓" : (verdict === "REVISE" || verdict === "FAIL") ? "✗" : "?";
			latestCompletedStatus = {
				text: `⚖ Critical: reviewer ${symbol} ${verdict || "COMPLETE"}`,
				updatedAt: Date.now(),
			};
		}
		activeReviews.delete(reviewId);
	}
	updateReviewerUIStatus(ctx);
}

function shortBoundary(boundaryKey?: string | null): string | undefined {
	if (!boundaryKey) return undefined;
	if (boundaryKey.startsWith("draft:")) {
		const parts = boundaryKey.split(":");
		const hash = parts[2] || parts[1] || "";
		return `draft h${hash.slice(0, 5)}`;
	}
	return boundaryKey.slice(0, 12);
}

export function formatActiveReviewsUIStatus(): string | undefined {
	const running = Array.from(activeReviews.values()).filter((r) => r.status === "starting" || r.status === "running");
	if (running.length === 0) {
		if (pendingReviews.size > 0) {
			const pendingLabels = [...pendingReviews.values()].map((p) => `${p.kind}/${p.triggerReason || p.kind}`).join(", ");
			return `⚖ Critical: queued (${pendingLabels})`;
		}
		if (latestCompletedStatus && (Date.now() - latestCompletedStatus.updatedAt) < 15000) {
			return latestCompletedStatus.text;
		}
		return undefined;
	}

	if (running.length === 1) {
		const r = running[0];
		const triggerLabel = r.triggerReason ? `${r.kind}/${r.triggerReason}` : r.kind;
		if (r.activity.turns === 0 && r.activity.tools === 0) {
			// Preserve "reviewer ⟳ starting" substring for backward compat, append trigger detail
			return `⚖ Critical: reviewer ⟳ starting (${triggerLabel})`;
		}
		const parts: string[] = [];
		if (r.activity.turns > 0) parts.push(`${r.activity.turns} turns`);
		if (r.activity.tools > 0) parts.push(`${r.activity.tools} tools`);
		if (r.activity.files > 0) parts.push(`${r.activity.files} files`);
		else if (r.activity.reads > 0) parts.push(`${r.activity.reads} reads`);
		const detail = parts.length > 0 ? ` ⟳ ${parts.join(" · ")}` : " ⟳ running";
		return `⚖ Critical: reviewer ${triggerLabel}${detail}`;
	}

	// Multiple active reviews — list all kind/trigger
	const labels = running.map((r) => `${r.reviewId.slice(0, 7)}:${r.kind}/${r.triggerReason || r.kind}`).join(", ");
	return `⚖ Critical: ${running.length} active · ${labels}`;
}

export function updateReviewerUIStatus(ctx?: ExtensionContext, customText?: string | null): void {
	const c = getActiveContext(ctx);
	if (c?.hasUI && typeof c.ui?.setStatus === "function") {
		if (customText === null) {
			c.ui.setStatus("critical_review", undefined);
		} else if (customText !== undefined) {
			c.ui.setStatus("critical_review", customText);
		} else {
			const text = formatActiveReviewsUIStatus();
			c.ui.setStatus("critical_review", text);
		}
	}
}
