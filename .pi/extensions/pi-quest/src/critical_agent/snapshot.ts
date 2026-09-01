import { execSync } from "node:child_process";
import { readFile } from "node:fs/promises";
import { parseMarkdownSections } from "../markdown.ts";
import { fileExists, questPath, resolveQuestRecordBySlug } from "../paths.ts";
import { parseOriginalRequest, parseRefinements } from "../reconstruction.ts";
import { state } from "../state.ts";
import { logEvent } from "../logging.ts";
import { CriticalReviewKind, ReviewSnapshot, StoredState } from "../types.ts";
import { extractQuestReviewContext } from "./prompt.ts";
import { computePlanReviewBoundaryKey } from "./tracker.ts";

export async function createReviewSnapshot(
	slugOrQid: string,
	reviewId: string,
	kind: CriticalReviewKind,
	sessionId: string,
	s?: StoredState,
	versionOverride?: { planVersion?: number; saveGeneration?: number; stateHash?: string | null },
): Promise<ReviewSnapshot> {
	const activeState = s || state;
	const planVersion = versionOverride?.planVersion !== undefined ? versionOverride.planVersion : (activeState.planVersion || 1);
	const saveGeneration = versionOverride?.saveGeneration !== undefined ? versionOverride.saveGeneration : (activeState.saveCount || 0);
	const stateHash = versionOverride?.stateHash !== undefined
		? versionOverride.stateHash
		: (activeState.lastSavedHash || (activeState.saveGeneration ? activeState.saveGeneration.hash : null));

	const context = await extractQuestReviewContext(slugOrQid, activeState);

	let relevantDiff = "";
	try {
		relevantDiff = execSync("git diff --no-color -U2", {
			encoding: "utf8",
			timeout: 2000,
			stdio: ["ignore", "pipe", "ignore"],
		});
	} catch {
		logEvent("SNAPSHOT_FALLBACK", `snapshot fallback: git diff failed, using filesModified`, {
			quest: slugOrQid,
			reviewId,
			reviewKind: kind,
			reason: "git_diff_failed",
		});
		relevantDiff = context.filesModified || "";
	}

	let boundaryKey: string | null = null;
	if (kind === "plan_review") {
		if (activeState.activeDraft === slugOrQid) {
			try {
				const { createHash } = await import("node:crypto");
				const { readFile } = await import("node:fs/promises");
				const { FUTURE_DIR } = await import("../constants.ts");
				const fPath = `${FUTURE_DIR}/${slugOrQid}.md`;
				const fContent = await readFile(fPath, "utf8");
				boundaryKey = `draft:${slugOrQid}:${createHash("sha256").update(fContent).digest("hex").slice(0, 12)}`;
			} catch {
				logEvent("SNAPSHOT_FALLBACK", `snapshot fallback: draft boundary compute failed`, {
					quest: slugOrQid,
					reviewId,
					reviewKind: kind,
					reason: "draft_boundary_fallback",
					boundaryKey: activeState.lastPlanReviewBoundaryKey || undefined,
				});
				boundaryKey = activeState.lastPlanReviewBoundaryKey || computePlanReviewBoundaryKey(
					activeState.questId || slugOrQid, planVersion, context.plan || "", context.planRevisions || "",
				);
			}
		} else {
			boundaryKey = activeState.lastPlanReviewBoundaryKey || computePlanReviewBoundaryKey(
				activeState.questId || slugOrQid, planVersion, context.plan || "", context.planRevisions || "",
			);
		}
	}

	return {
		questId: activeState.questId || slugOrQid,
		sessionId,
		reviewId,
		reviewKind: kind,
		planVersion,
		boundaryKey,
		saveGeneration,
		stateHash,
		originalUserRequest: context.originalRequest,
		currentUnderstanding: context.currentUnderstanding,
		assumptions: context.keyAssumptions,
		plan: context.plan,
		planRevisions: context.planRevisions,
		findings: context.findings,
		filesChanged: context.filesModified,
		relevantDiff,
		testStatus: context.testStatus,
		nextAction: context.exactNextAction,
		createdAt: Date.now(),
		refinements: context.refinements,
		executionSnapshot: context.executionSnapshot,
		remainingWork: context.remainingWork,
		status: context.status,
	};
}

export function isReviewSnapshotCurrent(
	snapshot: ReviewSnapshot,
	currentState: StoredState,
): { current: boolean; reason?: string } {
	const currentPlanVersion = currentState.planVersion || 1;
	if (snapshot.planVersion !== currentPlanVersion) {
		return {
			current: false,
			reason: `Plan version advanced from v${snapshot.planVersion} to v${currentPlanVersion}`,
		};
	}

	if (snapshot.reviewKind === "plan_review") {
		if (snapshot.boundaryKey && currentState.lastPlanReviewBoundaryKey && snapshot.boundaryKey !== currentState.lastPlanReviewBoundaryKey) {
			return {
				current: false,
				reason: `Plan content boundary changed from ${snapshot.boundaryKey} to ${currentState.lastPlanReviewBoundaryKey}`,
			};
		}
	}

	const currentHash = currentState.lastSavedHash || (currentState.saveGeneration ? currentState.saveGeneration.hash : null);
	// ZERO SLACK: any hash divergence with a newer save generation supersedes
	if (snapshot.stateHash && currentHash && snapshot.stateHash !== currentHash && (currentState.saveCount || 0) > snapshot.saveGeneration) {
		return {
			current: false,
			reason: `State hash changed from ${snapshot.stateHash.slice(0, 8)} to ${currentHash.slice(0, 8)}`,
		};
	}

	if (snapshot.reviewKind === "final_acceptance") {
		if (currentState.dirty) {
			return {
				current: false,
				reason: "State became dirty during review execution",
			};
		}
		if (currentHash && snapshot.stateHash && currentHash !== snapshot.stateHash) {
			return {
				current: false,
				reason: `Final state hash mismatch (${snapshot.stateHash.slice(0, 8)} vs current ${currentHash.slice(0, 8)})`,
			};
		}
	}

	return { current: true };
}
