import { originalRequestText, refinementsBlock, reportAgentError } from "../messaging.ts";
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
		return `Economy auto-compaction${tokenLabel} during sub-quest '${activeQuest}' (parent: '${parentName}'). Focus summary on active sub-quest progress, tested hypotheses, key architectural decisions, modified files, and immediate sub-quest next steps. Parent quest state is safely preserved on disk in ${parentPath}. Following compaction, autonomously read ${activePath}, validate current understanding against the recovered state, and proceed with the most justified next action.`;
	}

	return `Economy auto-compaction${tokenLabel}. Focus summary on active quest '${activeQuest}', tested hypotheses, key architectural decisions, modified files, and immediate next steps. The latest durable quest state is persisted in ${activePath}. Following compaction, autonomously read ${activePath}, validate current understanding against the recovered state, and proceed with the most justified next action.`;
}

export function buildWarningSavePrompt(
	activeQuest: string,
	fraction: number,
	tokens: number,
	threshold: number,
): string {
	const isClose = fraction >= 0.5;
	const escalationLabel = isClose ? "Close to Threshold" : "Approaching Threshold";
	const escalationAdvice = isClose
		? "Context is close to the compaction threshold. Prioritize an exhaustive durable checkpoint now. Avoid unnecessary further work."
		: "Context is approaching the compaction threshold. Keep the quest file current and prepare an exhaustive checkpoint.";

	const promptReminder = `Original user request -- keep VERBATIM under ## Original request in the quest file:\n"${originalRequestText()}"${
		state.refinements && state.refinements.length > 0 ? `\n\nUser refinements -- list under ## Quest Refinements & User Feedback Loops:\n${refinementsBlock()}` : ""
	}`;

	return `⚡ **Context Compaction Warning (${escalationLabel}: ${formatTokens(tokens)} / ${formatTokens(threshold)} tokens)**:
Context compaction is imminent. ${escalationAdvice}

Before auto-compaction occurs and resets conversation working memory, perform an EXHAUSTIVE DURABLE STATE SAVE in:
\`${questPath(state.questId)}\`

${promptReminder}

**Epistemic & Execution State Checklist**:
- Current Status
- Discoveries & Learnings (architectural facts, verified invariants)
- Tested Assumptions (validated / invalidated / uncertain)
- Contradictions & Plan Revisions (if any)
- Files Touched / Examined
- Test / Build Status
- Remaining Work
- EXACT NEXT ACTION (concrete and immediate)

Update the quest file and call \`quest_mark_saved\` to ensure your state is verified before compaction.`;
}

export function buildCriticalSavePrompt(
	activeQuest: string,
	tokens: number,
	threshold: number,
): string {
	const promptReminder = `Original user request -- keep VERBATIM under ## Original request in the quest file:\n"${originalRequestText()}"${
		state.refinements && state.refinements.length > 0 ? `\n\nUser refinements -- list under ## Quest Refinements & User Feedback Loops:\n${refinementsBlock()}` : ""
	}`;

	return `🚨 **CRITICAL QUEST JOURNAL EXECUTION DIRECTIVE** 🚨

Context usage (${formatTokens(tokens)} tokens) has reached or exceeded the configured compaction threshold (${formatTokens(threshold)} tokens).

This directive supersedes your current implementation plan for the next action.

STOP ordinary implementation.
Do NOT defer the checkpoint. STOP treating checkpointing as optional.

Your next action MUST be the durable checkpoint procedure:
1. Reconstruct the current state of the work.
2. Update the active quest file exhaustively in \`${questPath(state.questId)}\`.
3. Preserve discoveries, assumptions, contradictions, decisions, rejected approaches, files touched, verification status, remaining work, and the EXACT NEXT ACTION.
4. Call quest_mark_saved.
5. Ensure the save is verified.
6. Prepare for immediate compaction.

Do not start another implementation action first.
Do not defer checkpointing.

${promptReminder}

ONLY AFTER THE DURABLE SAVE IS VERIFIED may you continue ordinary work.`;
}

export function buildCriticalCompactionReadyPrompt(
	activeQuest: string,
	tokens: number,
	threshold: number,
): string {
	return `⚡ **CRITICAL QUEST JOURNAL EXECUTION DIRECTIVE (DURABLE STATE SAVED)** ⚡

Context usage (${formatTokens(tokens)} tokens) remains at or above the configured compaction threshold (${formatTokens(threshold)} tokens).

This directive supersedes your current implementation plan for the next action.
Your durable quest checkpoint in \`${questPath(state.questId)}\` is VERIFIED and ready.

Context auto-compaction is now required. Stand by for auto-compaction across the turn boundary.
Do NOT begin large new implementation streams before compaction resets working memory.
Ensure your exact next action is fully documented so execution can resume cleanly post-compaction.`;
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
