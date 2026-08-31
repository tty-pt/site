import { reportAgentError } from "../messaging.ts";
import { questPath } from "../paths.ts";
import { persist } from "../persistence.ts";
import { triggerReassessment } from "../research.ts";
import { state } from "../state.ts";
import { CompactionTransaction, ExtensionAPI, ExtensionContext, QuestErrorCode, StoredState } from "../types.ts";
import { formatTokens } from "../utils.ts";

export function compactionReady(expectedSlug?: string): boolean {
	const target = expectedSlug || state.active;
	if (!target) return false;
	if (
		state.activeTransaction &&
		(state.activeTransaction.phase === "inconsistent" || state.activeTransaction.phase === "failed")
	) {
		return false;
	}
	const p = questPath(state.questId);
	const hasValidGen = Boolean(state.saveGeneration && state.saveGeneration.path === p);
	const hasSaveSinceCompact = state.saveCount > state.compactCount;
	return hasValidGen && hasSaveSinceCompact && !state.dirty;
}

export function getCompactionInstructions(activeQuest: string, tokens: number | null, threshold: number): string {
	const isSubQuest = Array.isArray(state.stack) && state.stack.length > 1;
	const parentName = isSubQuest ? state.stack[state.stack.length - 2] : null;
	const tokenLabel = tokens !== null ? ` at ${formatTokens(tokens)} tokens (threshold: ${formatTokens(threshold)})` : "";
	const activePath = questPath(state.questId);

	if (isSubQuest && parentName) {
		const parentPath = questPath(state.questId);
		return `Compaction${tokenLabel} during sub-quest '${activeQuest}' (parent: '${parentName}'). Focus summary on active sub-quest progress, tested hypotheses, key architectural decisions, modified files, and immediate sub-quest next steps. Parent quest state is safely preserved on disk in ${parentPath}. Following compaction, autonomously read ${activePath}, validate current understanding against the recovered state, and proceed with the most justified next action.`;
	}

	return `Compaction${tokenLabel}. Focus summary on active quest '${activeQuest}', tested hypotheses, key architectural decisions, modified files, and immediate next steps. The latest durable quest state is persisted in ${activePath}. Following compaction, autonomously read ${activePath}, validate current understanding against the recovered state, and proceed with the most justified next action.`;
}

export function buildPeriodicCheckpointPrompt(
	activeQuest: string,
	opts: { turnsSinceCheckpoint: number; filesModified?: string[]; logTail?: string } = { turnsSinceCheckpoint: 0 },
): string {
	const qp = questPath(state.questId);
	const files = Array.isArray(opts.filesModified) && opts.filesModified.length > 0 ? opts.filesModified.slice(-6).join(", ") : "(none)";
	const tailBlock = opts.logTail ? `\n\nRecent execution log tail (last 10):\n\`\`\`\n${opts.logTail.slice(0, 1200)}\n\`\`\`` : "";
	if (!activeQuest || activeQuest === "provisional" || state.pendingRootQuest || state.activeDraft) {
		const draft = state.activeDraft ? `.pi/quest/future/${state.activeDraft}.md` : (qp || `.pi/quest/current/${state.questId || "<qid>"}/quest.md`);
		const idTag = state.questId ? ` (id: ${state.questId})` : "";
		const slug = activeQuest && activeQuest !== "provisional" ? activeQuest : (state.activeDraft || "provisional");
		return `💾 **Periodic Durable Checkpoint (Draft/Provisional${idTag})** — ${opts.turnsSinceCheckpoint} substantive turns since last save for '${slug}'. Persist \`${draft}\` + call \`quest_update_state\` (or \`quest_mark_saved\`).

Checklist: Current Understanding & Plan, Files Modified (${files}), Test / Build Status, EXACT NEXT ACTION (live pointer).

Original request — keep VERBATIM under ## Original request in \`${draft}\`.${tailBlock}`;
	}
	return `💾 **Periodic Durable Checkpoint** — ${opts.turnsSinceCheckpoint} substantive turns since last save for '${activeQuest}'. Update \`${qp}\` + \`quest_mark_saved\`.

Checklist: Current Understanding & Plan, Files Modified (${files}), Test / Build Status, EXACT NEXT ACTION (live pointer).

Original request — keep VERBATIM under ## Original request in \`${qp}\`.${tailBlock}`;
}

// @deprecated pressure prompts retained as aliases for compat (unused)
export function buildWarningSavePrompt(activeQuest: string, _fraction: number, _tokens: number, _threshold: number): string {
	return buildPeriodicCheckpointPrompt(activeQuest, { turnsSinceCheckpoint: (state as any).substantiveTurnsSinceCheckpoint || 0 });
}
export function buildCriticalSavePrompt(activeQuest: string, _tokens: number, _threshold: number): string {
	return buildPeriodicCheckpointPrompt(activeQuest, { turnsSinceCheckpoint: (state as any).substantiveTurnsSinceCheckpoint || 0 });
}
export function buildCriticalCompactionReadyPrompt(activeQuest: string, _tokens: number, _threshold: number): string {
	return buildPeriodicCheckpointPrompt(activeQuest, { turnsSinceCheckpoint: (state as any).substantiveTurnsSinceCheckpoint || 0 });
}

export function validatePhasedPlan(content: string): boolean {
	const planMatch = content.match(/##\s*Plan\s*\n([\s\S]*?)(?=\n##\s+|$)/i);
	if (!planMatch) return false;
	const planBody = planMatch[1];
	const lines = planBody.split(/\r?\n/).filter((l) => l.trim().length > 0);
	const hasNumbered = lines.some((l) => /^\s*\d+\.\s+/.test(l) || /^\s*-\s*\[[ xX]\]/.test(l) || /^\s*-\s+/.test(l));
	const hasVerification = /verification|verify|test|check|gate/i.test(planBody);
	const hasSubquest = /\[\[.+?\]\]/.test(planBody);
	const exactIdx = content.search(/##\s*Exact Next Action/i);
	if (exactIdx === -1) return false;
	const exactBody = content.slice(exactIdx, exactIdx + 800);
	if (/>\s*Paste/i.test(exactBody) || exactBody.trim().length < 30) return false;
	return hasNumbered && (hasVerification || hasSubquest) && exactBody.length > 30;
}

export function validateCheckpointMatching(
	tx: CompactionTransaction | null | undefined,
	sessionState: StoredState,
	pi: ExtensionAPI,
	c?: ExtensionContext,
): boolean {
	const currentHash = sessionState.saveGeneration?.hash || sessionState.lastSavedHash || "";
	const currentQuest = sessionState.active || "";
	const currentQuestPath = questPath(sessionState.questId);

	const questMismatch = Boolean(tx && tx.activeQuest !== currentQuest);
	const pathMismatch = Boolean(tx && tx.questPath !== currentQuestPath);
	const hashMismatch = Boolean(tx && tx.checkpointHash !== currentHash);
	const countMismatch = Boolean(tx && tx.checkpointSaveCount !== sessionState.saveCount);

	if (questMismatch || pathMismatch || hashMismatch || countMismatch) {
		if (tx) {
			tx.phase = "inconsistent";
		}
		triggerReassessment(
			sessionState,
			"The completed compaction no longer matches the durable checkpoint that was prepared for this transaction.",
		);
		persist(pi, c);
		reportAgentError(
			pi,
			c,
			`[Quest Journal] RESUME_STATE_INCONSISTENT\n\nThe completed compaction no longer matches the durable checkpoint that was prepared for this transaction.\n\nDo not assume the pre-compaction state was safely preserved.\n\nRequired action:\nreconstruct the current durable quest state, reconcile the discrepancy, and continue only from the verified state.`,
			{
				code: QuestErrorCode.RESUME_STATE_INCONSISTENT,
				requiredNextAction: "reconstruct the current durable quest state, reconcile the discrepancy, and continue only from the verified state.",
				details: {
					CompactionId: tx?.id,
					TransactionQuest: tx?.activeQuest,
					AuthoritativeQuest: currentQuest,
					TransactionPath: tx?.questPath,
					AuthoritativePath: currentQuestPath,
					TransactionHash: tx?.checkpointHash,
					LiveHash: currentHash,
					TransactionGen: tx?.checkpointSaveCount,
					LiveGen: sessionState.saveCount,
				},
			},
		);
		return false;
	}
	return true;
}
