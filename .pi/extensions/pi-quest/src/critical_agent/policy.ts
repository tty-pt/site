import { createHash } from "node:crypto";
import { existsSync, readFileSync } from "node:fs";
import { FUTURE_DIR } from "../constants.ts";
import { logCriticalReviewTransition } from "../logging.ts";
import { logError } from "../messaging.ts";
import { getActiveContext, getSessionId, getState, isRootQuest, state } from "../state.ts";
import {
	CriticalReviewKind,
	CriticalReviewState,
	CriticalReviewer,
	ExtensionAPI,
	ExtensionContext,
	ReviewSnapshot,
	StoredState,
} from "../types.ts";
import {
	getCustomSubagentRunner,
	isSubagentToolRegistered,
	PiSubagentReviewer,
} from "./pi_adapter.ts";
import { findActiveReviewForQuest, getActiveReviews, getPendingReview, registerActiveReview, setPendingReview } from "./tracker.ts";
import { checkAttemptLimit, checkLaunchGuard } from "./policy/launch_guard.ts";
import { dequeuePendingIfNeeded } from "./policy/pending_coalesce.ts";
import { executeReviewBackground } from "./policy/background.ts";
import { getGlobalReviewLockKey, getQuestLockKey, withQuestLock } from "../utils/mutex.ts";
import { GLOBAL_REVIEW_CAP } from "../constants.ts";

export { reconcileReviewResult } from "./policy/reconcile.ts";

export interface CriticalReviewOptions {
	kind: CriticalReviewKind;
	questSlug?: string;
	agent?: string;
	model?: string;
	force?: boolean;
	boundaryKey?: string | null;
	triggerReason?: string;
	rebuttal?: string;
	async?: boolean;
	timeoutMs?: number;
	subagentRunner?: (task: string, options?: any) => Promise<string | { text?: string; content?: any; isError?: boolean; error?: any; childSessionId?: string; transcriptRef?: string }>;
}

export interface CriticalReviewExecutionResult {
	success: boolean;
	available: boolean;
	skipped?: boolean;
	inProgress?: boolean;
	superseded?: boolean;
	review?: CriticalReviewState;
	error?: string;
	reviewPromise?: Promise<CriticalReviewExecutionResult>;
}

export function isDraftReviewValid(targetState?: StoredState): boolean {
	const s = targetState || state;
	if (!s.activeDraft) return false;
	const approval = s.lastPlanReviewApproval;
	if (!approval) return false;
	try {
		const slug = s.activeDraft;
		const path = `${FUTURE_DIR}/${slug}.md`;
		if (!existsSync(path)) return false;
		const content = readFileSync(path, "utf8");
		const hash = createHash("sha256").update(content).digest("hex").slice(0, 12);
		const expectedKey = `draft:${slug}:${hash}`;
		if (approval.boundaryKey) {
			return approval.boundaryKey === expectedKey || approval.boundaryKey === s.draftLastReviewKey;
		}
		return false;
	} catch {
		return false;
	}
}

export function isPlanReviewValidForState(targetState?: StoredState): boolean {
	const s = targetState || state;
	if ((s as any).activeDraft) return isDraftReviewValid(s);
	if (s.dirty) return false;
	const approval = s.lastPlanReviewApproval;
	if (!approval) return false;

	const currentPlanVersion = s.planVersion || 1;

	if (approval.planVersion !== currentPlanVersion) {
		return false;
	}

	if (approval.boundaryKey) {
		const currentBoundaryKey = s.lastPlanReviewBoundaryKey;
		if (currentBoundaryKey && approval.boundaryKey !== currentBoundaryKey) {
			return false;
		}
		return true;
	}

	const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : null);
	if (currentHash && approval.saveHash && approval.saveHash !== currentHash) {
		return false;
	}
	if (s.saveCount && approval.saveCount && approval.saveCount !== s.saveCount) {
		return false;
	}

	return true;
}

export function isCriticalReviewValidForCompletion(targetState?: StoredState): boolean {
	const s = targetState || state;
	if (s.dirty) return false;
	const rev = s.lastCriticalReview;
	if (!rev) return false;
	if (rev.kind !== "final_acceptance") return false;
	if (rev.verdict !== "PASS" && rev.verdict !== "APPROVE") return false;
	if (rev.superseded) return false;

	const currentPlanVersion = s.planVersion || 1;
	const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : null);

	if (rev.reviewedStateVersion.planVersion !== currentPlanVersion) {
		return false;
	}
	if (currentHash && rev.reviewedStateVersion.saveHash && rev.reviewedStateVersion.saveHash !== currentHash) {
		return false;
	}

	return true;
}

export async function runCriticalReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	options: CriticalReviewOptions,
): Promise<CriticalReviewExecutionResult> {
	const c = getActiveContext(ctx);
	const targetState = getState(c);
	const slug = options.questSlug || targetState.active || "quest";
	const questId = targetState.questId || slug;
	const sessionId = getSessionId(c);
	const correlationId = `rev_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;

	const reviewer: CriticalReviewer = new PiSubagentReviewer(pi, ctx, options.subagentRunner);

	const registered = Boolean(options.subagentRunner) || Boolean(getCustomSubagentRunner()) || isSubagentToolRegistered(pi, ctx);
	if (!registered) {
		return { success: true, available: false, skipped: true };
	}

	if (!reviewer.isAvailable()) {
		logCriticalReviewTransition("CRITICAL_REVIEW_UNAVAILABLE", "critical review unavailable: subagent tool not executable", {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			parentSessionId: sessionId,
			reviewKind: options.kind,
			reason: "subagent_tool_not_executable",
		});
		return {
			success: false,
			available: false,
			skipped: false,
			error: "subagent_tool_not_executable",
		};
	}

	const currentPlanVersion = targetState.planVersion || 1;
	const currentHash = targetState.lastSavedHash || (targetState.saveGeneration ? targetState.saveGeneration.hash : null);
	const currentSaveCount = targetState.saveCount || 0;

	const questLockKey = getQuestLockKey(slug, sessionId);
	const globalLockKey = getGlobalReviewLockKey(sessionId);
	let provisionalSnapshot!: ReviewSnapshot;
	let executionPromise!: Promise<CriticalReviewExecutionResult>;
	let resolveExecution!: (res: CriticalReviewExecutionResult) => void;
	let lockResult: { blocked?: boolean; limited?: boolean; response?: any } | null = null;

	await withQuestLock(globalLockKey, async () =>
		await withQuestLock(questLockKey, async () => {
			// Global burst cap check inside hierarchical lock (global → per-quest)
			const globalActive = [...getActiveReviews().values()].filter((r) => r.status === "starting" || r.status === "running").length;
			if (globalActive >= GLOBAL_REVIEW_CAP) {
				setPendingReview(slug, {
					questSlug: slug,
					kind: options.kind,
					triggerReason: options.triggerReason,
					planVersion: currentPlanVersion,
					stateHash: currentHash,
					boundaryKey: options.boundaryKey || targetState.lastPlanReviewBoundaryKey || null,
					saveCount: currentSaveCount,
					requestedAt: Date.now(),
					rebuttal: options.rebuttal,
					model: options.model,
					timeoutMs: options.timeoutMs,
					force: options.force,
				} as any);
				logCriticalReviewTransition("GLOBAL_REVIEW_CAP_HIT", `global review cap hit (active=${globalActive}, cap=${GLOBAL_REVIEW_CAP})`, {
					quest: slug,
					questId,
					sessionId,
					reviewId: correlationId,
					parentSessionId: sessionId,
					reviewKind: options.kind,
					triggerReason: options.triggerReason || options.kind,
					boundaryKey: options.boundaryKey || undefined,
					activeCount: globalActive,
					cap: GLOBAL_REVIEW_CAP,
				});
				lockResult = { blocked: true, response: { success: true, available: true, inProgress: true, skipped: true, error: "global_review_cap" } };
				return;
			}

			const launchGuard = checkLaunchGuard(slug, options.kind, targetState, options, currentPlanVersion, currentHash, currentSaveCount);
			if (launchGuard.blocked) {
				logCriticalReviewTransition("CRITICAL_REVIEW_SUPPRESSED_DUPLICATE", `review suppressed duplicate: ${launchGuard.response?.error || "active review exists"}`, {
					quest: slug,
					questId,
					sessionId,
					reviewId: correlationId,
					parentSessionId: sessionId,
					reviewKind: options.kind,
					triggerReason: options.triggerReason || options.kind,
					reason: launchGuard.response?.error,
				});
				// Log coalesced when pending was set
				logCriticalReviewTransition("CRITICAL_REVIEW_COALESCED", `review coalesced: pending ${slug}:${options.kind}`, {
					quest: slug,
					questId,
					sessionId,
					reviewId: correlationId,
					parentSessionId: sessionId,
					reviewKind: options.kind,
					triggerReason: options.triggerReason || options.kind,
					boundaryKey: options.boundaryKey || undefined,
				});
				lockResult = { blocked: true, response: launchGuard.response };
				return;
			}
			const attemptCheck = checkAttemptLimit(slug, options.kind, targetState, options, correlationId, sessionId, questId, currentPlanVersion, currentHash, currentSaveCount, pi);
			if (attemptCheck.limited) {
				lockResult = { limited: true, response: attemptCheck.response };
				return;
			}
			if (options.force) {
				logCriticalReviewTransition("CRITICAL_REVIEW_FORCED" as any, `critical review forced (correlationId=${correlationId})`, {
					quest: slug,
					questId,
					sessionId,
					reviewId: correlationId,
					parentSessionId: sessionId,
					reviewKind: options.kind,
					triggerReason: options.triggerReason || options.kind,
				});
			}

			provisionalSnapshot = {
				questId,
				sessionId,
				reviewId: correlationId,
				reviewKind: options.kind,
				planVersion: currentPlanVersion,
				saveGeneration: currentSaveCount,
				stateHash: currentHash,
				originalUserRequest: targetState.prompts && targetState.prompts.length > 0 ? targetState.prompts[0] : "",
				currentUnderstanding: "",
				assumptions: "",
				plan: "",
				planRevisions: "",
				findings: "",
				filesChanged: "",
				relevantDiff: "",
				testStatus: "",
				nextAction: "",
				createdAt: Date.now(),
			};

			executionPromise = new Promise<CriticalReviewExecutionResult>((res) => {
				resolveExecution = res;
			});

			registerActiveReview(correlationId, slug, sessionId, options.kind, provisionalSnapshot, executionPromise, options.triggerReason);
			// Phase 20/21: scalar awaitingReview gate (A: plan_review + final_acceptance only)
			if (options.kind === "plan_review" || options.kind === "final_acceptance") {
				(targetState as any).awaitingReview = { kind: options.kind, reviewId: correlationId, triggerReason: options.triggerReason, since: Date.now() };
			}

			const isPlanReviewKind = options.kind === "plan_review";
			const requestEventType = isPlanReviewKind ? "PLAN_REVIEW_REQUESTED" : "CRITICAL_REVIEW_REQUESTED";
			const startEventType = isPlanReviewKind ? "PLAN_REVIEW_STARTED" : "CRITICAL_REVIEW_STARTED";

			logCriticalReviewTransition(requestEventType, `critical ${options.kind} review requested`, {
				quest: slug,
				questId,
				sessionId,
				reviewId: correlationId,
				parentSessionId: sessionId,
				reviewKind: options.kind,
				triggerReason: options.triggerReason || options.kind,
				boundaryKey: options.boundaryKey || undefined,
				reviewedVersion: currentPlanVersion,
			});

			logCriticalReviewTransition(startEventType, `critical ${options.kind} review started`, {
				quest: slug,
				questId,
				sessionId,
				reviewId: correlationId,
				parentSessionId: sessionId,
				reviewKind: options.kind,
				triggerReason: options.triggerReason || options.kind,
				boundaryKey: options.boundaryKey || undefined,
				reviewedVersion: currentPlanVersion,
			});
		})
	);

	if ((lockResult as any)?.blocked || (lockResult as any)?.limited) return (lockResult as any).response;

	(async () => {
		await executeReviewBackground(pi, ctx, {
			slug,
			questId,
			sessionId,
			correlationId,
			targetState,
			currentPlanVersion,
			currentHash,
			currentSaveCount,
			provisionalSnapshot,
			kind: options.kind,
			options,
			reviewer,
			resolveExecution,
			onPending: async (snapshot) => {
				// Phase 10: atomic dequeue under hierarchical lock, no setTimeout gap
				const globalKey = getGlobalReviewLockKey(sessionId);
				const questKey = getQuestLockKey(slug, sessionId);
				let pendingToRun: any = null;
				await withQuestLock(globalKey, async () =>
					await withQuestLock(questKey, async () => {
						const pending = dequeuePendingIfNeeded(slug, currentPlanVersion, currentHash, snapshot, targetState, options.kind);
						if (pending) pendingToRun = pending;
					})
				);
				if (pendingToRun) {
					runCriticalReview(pi, ctx, {
						kind: pendingToRun.kind,
						questSlug: slug,
						triggerReason: pendingToRun.triggerReason,
						boundaryKey: pendingToRun.boundaryKey,
						rebuttal: pendingToRun.rebuttal,
						model: pendingToRun.model,
						timeoutMs: pendingToRun.timeoutMs,
						force: pendingToRun.force,
					}).catch((e) => {
						logError("Failed to run queued critical review follow-up", e, ctx);
					});
				}
			},
		});
	})();

	return await executionPromise;
}

export async function requestPlanReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	questSlug?: string,
	options?: Partial<CriticalReviewOptions>,
): Promise<CriticalReviewExecutionResult> {
	return runCriticalReview(pi, ctx, { kind: "plan_review", questSlug, ...options });
}

export async function checkAndTriggerPlanReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	triggerReason?: string,
): Promise<CriticalReviewExecutionResult | null> {
	const c = getActiveContext(ctx);
	const s = getState(c);
	if (s.activeDraft) {
		if (isDraftReviewValid(s)) return null;
		const registered = isSubagentToolRegistered(pi, ctx) || Boolean(getCustomSubagentRunner());
		if (!registered) return null;
		const draftSlug = s.activeDraft;
		const path = `${FUTURE_DIR}/${draftSlug}.md`;
		let hash = "clean";
		try { const content = readFileSync(path, "utf8"); hash = createHash("sha256").update(content).digest("hex").slice(0, 12); } catch {}
		const key = `draft_review:${draftSlug}:h${hash}`;
		if ((s as any).lastDraftReviewRequestKey === key) return null;
		const result = await runCriticalReview(pi, ctx, { kind: "plan_review", questSlug: draftSlug, triggerReason: triggerReason || "draft", boundaryKey: `draft:${draftSlug}:${hash}` } as any);
		if (result?.review?.verdict) {
			(s as any).lastDraftReviewRequestKey = key;
			(s as any).__lastDraftReviewKey = key;
			if (result.review.verdict === "APPROVE" || result.review.verdict === "PASS") {
				s.draftLastReviewKey = `draft:${draftSlug}:${hash}`;
				// Notify agent to present approved draft plan to user for final "go"
				try {
					const { sendInternalAgentMessage } = await import("../messaging.ts");
					sendInternalAgentMessage(pi, `✅ **Draft '${draftSlug}' reviewer APPROVED** (compliance check vs ${s.draftPrompts?.length || 0} requirements). Present the finalized plan to the user now and await explicit "go" / confirmation before promoting to current quest.`, "steer");
				} catch {}
			} else {
				try {
					const { sendInternalAgentMessage } = await import("../messaging.ts");
					sendInternalAgentMessage(pi, `⚠️ **Draft '${draftSlug}' reviewer ${result.review.verdict}**: ${result.review.requiredActions?.join("; ") || result.review.findings?.map((f:any)=>f.issue).join("; ") || "needs revision"} — update .pi/quest/future/${draftSlug}.md and re-submit for review before presenting to user.`, "steer");
				} catch {}
			}
		}
		return result;
	}
	if (!s.active || !isRootQuest(s)) return null;
	if (s.reassessmentRequired) return null;

	const registered = isSubagentToolRegistered(pi, ctx) || Boolean(getCustomSubagentRunner());
	if (!registered) return null;

	const currentPlanVersion = s.planVersion || 1;
	const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : "clean");
	const currentSaveCount = s.saveCount || 0;
	const key = `plan_review:${s.active}:v${currentPlanVersion}:h${currentHash}:s${currentSaveCount}`;

	if (isPlanReviewValidForState(s) && (s as any).lastPlanReviewRequestKey === key) {
		return null;
	}

	const result = await runCriticalReview(pi, ctx, { kind: "plan_review", questSlug: s.active, triggerReason: triggerReason || "root" });
	if (result?.review?.verdict) {
		(s as any).lastPlanReviewRequestKey = key;
		(s as any).__lastPlanReviewKey = key;
	}
	return result;
}



export async function requestDirectionReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	triggerReason: string,
): Promise<CriticalReviewExecutionResult | null> {
	return checkAndTriggerDirectionReview(pi, ctx, triggerReason);
}

export async function requestFinalReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	questSlug?: string,
): Promise<CriticalReviewExecutionResult> {
	return runCriticalReview(pi, ctx, { kind: "final_acceptance", questSlug });
}

export async function checkAndTriggerDirectionReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	triggerReason?: string,
): Promise<CriticalReviewExecutionResult | null> {
	const c = getActiveContext(ctx);
	const s = getState(c);
	if (!s.active || !isRootQuest(s)) return null;
	if (s.reassessmentRequired || s.researchRequired || s.awaitingUserConfirmation) return null;

	const registered = isSubagentToolRegistered(pi, ctx) || Boolean(getCustomSubagentRunner());
	if (!registered) return null;

	// Throttle no_progress during cooldown / pending / plan-block
	if (triggerReason === "no_progress") {
		// Plan-block: suppress direction while plan_review active or pending
		const hasActivePlan =
			(findActiveReviewForQuest(s.active!)?.kind === "plan_review") ||
			[...getActiveReviews().values()].some((r) => r.kind === "plan_review" && (r.status === "starting" || r.status === "running"));
		const hasPendingPlan = !!getPendingReview(s.active!, "plan_review");
		if (hasActivePlan || hasPendingPlan) {
			const { logEvent } = await import("../logging.ts");
			logEvent("DIRECTION_REVIEW_THROTTLED", `direction review throttled (plan_review active/pending)`, {
				quest: s.active,
				triggerReason,
				reason: hasActivePlan ? "plan_review_active" : "plan_review_pending",
			});
			s.substantiveTurnsSinceCheckpoint = 0;
			return null;
		}
		const { DIRECTION_REVIEW_COOLDOWN_MS } = await import("../constants.ts");
		if ((s as any).lastDirectionReviewAt && Date.now() - (s as any).lastDirectionReviewAt < DIRECTION_REVIEW_COOLDOWN_MS) {
			const { logEvent } = await import("../logging.ts");
			logEvent("DIRECTION_REVIEW_THROTTLED", `direction review throttled (cooldown ${DIRECTION_REVIEW_COOLDOWN_MS}ms)`, {
				quest: s.active,
				triggerReason,
				cooldownMs: DIRECTION_REVIEW_COOLDOWN_MS,
				lastAt: (s as any).lastDirectionReviewAt,
			});
			s.substantiveTurnsSinceCheckpoint = 0;
			return null;
		}
		if (getPendingReview(s.active!, "direction")) {
			s.substantiveTurnsSinceCheckpoint = 0;
			return null;
		}
	}

	const currentPlanVersion = s.planVersion || 1;
	const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : "clean");
	const currentSaveCount = s.saveCount || 0;
	const key = triggerReason === "no_progress"
		? `dir:${s.active}:v${currentPlanVersion}:h${currentHash}:s${currentSaveCount}:no_progress`
		: `dir:${s.active}:v${currentPlanVersion}:h${currentHash}:s${currentSaveCount}`;

	if (s.lastCriticalReview?.kind === "direction" && (s as any).lastDirectionReviewKey === key) {
		return null;
	}

	const result = await runCriticalReview(pi, ctx, { kind: "direction", questSlug: s.active, triggerReason: triggerReason || "direction" });
	if (result?.review?.verdict) {
		(s as any).lastDirectionReviewKey = key;
		(s as any).__lastDirectionReviewKey = key;
		(s as any).lastDirectionReviewAt = Date.now();
	}
	// Reset counter after successful trigger to prevent 5-10-15 storm
	if (triggerReason === "no_progress" && result) {
		s.substantiveTurnsSinceCheckpoint = 0;
	}
	return result;
}

export async function submitReviewRebuttal(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	rebuttalText: string,
	options?: { questSlug?: string; kind?: CriticalReviewKind; subagentRunner?: any },
): Promise<CriticalReviewExecutionResult> {
	const c = getActiveContext(ctx);
	const s = getState(c);
	const slug = options?.questSlug || s.active || "quest";
	const kind = options?.kind || (s.lastCriticalReview?.kind || "direction");

	return await runCriticalReview(pi, ctx, {
		kind,
		questSlug: slug,
		force: true,
		rebuttal: rebuttalText,
		subagentRunner: options?.subagentRunner,
	});
}
