import { compactionReady, getEconomyThreshold } from "./compaction.ts";
import { NOTES_FILE } from "./constants.ts";
import { calculateCurrentTokens, gitBranch, projectGuidelines, standingNotes } from "./context.ts";
import { questPath } from "./paths.ts";
import { getActiveContext, state } from "./state.ts";
import { ExtensionContext } from "./types.ts";
import { formatQuestHierarchy, formatTokens } from "./utils.ts";

export function updateUIStatus(ctx?: ExtensionContext) {
	const c = getActiveContext(ctx);
	if (c?.hasUI) {
		const fresh = compactionReady();
		const hier = formatQuestHierarchy(state.active, state.stack);
		const threshold = getEconomyThreshold(c);

		let tokenInfo = "";
		const tokens = calculateCurrentTokens(c);

		if (tokens !== null && tokens > 0) {
			if (threshold > 0) {
				tokenInfo = ` [${formatTokens(tokens)}/${formatTokens(threshold)}]`;
			} else {
				tokenInfo = ` [${formatTokens(tokens)}]`;
			}
		} else {
			const usage = typeof c.getContextUsage === "function" ? c.getContextUsage() : undefined;
			if (typeof usage?.percent === "number" && usage.percent > 0) {
				tokenInfo = ` [${Math.round(usage.percent)}%]`;
			}
		}

		let stateTag = "";
		if (state.pendingRootQuest) {
			stateTag = " [PROVISIONAL RESEARCH]";
		} else if (!fresh) {
			stateTag = " (save pending)";
		} else if (threshold > 0 && tokens !== null && tokens >= threshold) {
			stateTag = " (compaction ready)";
		}

		const idTag = state.questId ? ` | id: ${state.questId}` : "";

		const text = state.active
			? `✨ quest: ${hier}${idTag}${tokenInfo}${stateTag}`
			: state.pendingRootQuest
			? `✨ quest: [provisional root]${idTag}${tokenInfo}${stateTag}`
			: undefined;
		if (typeof c.ui?.setStatus === "function") {
			c.ui.setStatus("quest", text);
		}
	}
}

export function buildSessionAwarenessBlock(ctx: ExtensionContext): string {
	const lines: string[] = [
		"# Session awareness (auto-injected)",
		"",
		`- Now: ${new Date().toISOString()}`,
		`- cwd: ${ctx.cwd}`,
	];
	const branch = gitBranch();
	if (branch) lines.push(`- Git branch: ${branch}`);
	if (state.questId) lines.push(`- Active quest ID: \`${state.questId}\``);

	if (state.active) {
		const fresh = compactionReady();
		const hier = formatQuestHierarchy(state.active, state.stack);
		const threshold = getEconomyThreshold(ctx);
		const tokens = calculateCurrentTokens(ctx);
		const tokenStr = tokens !== null ? ` | tokens: ${formatTokens(tokens)}${threshold > 0 ? `/${formatTokens(threshold)}` : ""}` : "";
		const stackInfo = state.stack && state.stack.length > 1 ? ` | LIFO stack: [${state.stack.join(" → ")}]` : "";
		lines.push(
			`- Active quest: \`${questPath(state.questId)}\` [${hier}] (${fresh ? "fresh" : "SAVE PENDING - update it before compaction"}${tokenStr}${stackInfo}); manage with /quest, /subquest, /quests, /quest-economy.`,
		);
	} else if (state.pendingRootQuest) {
		lines.push(
			`- Active quest: [PROVISIONAL ROOT INITIALIZATION] Research required to establish quest identity and plan. Use read/search/bash tools to investigate, determine a concise semantic quest name (e.g. 'persistent-agent-research', 'oauth-login-flow'), and call quest_update_state to initialize the durable quest with your research findings. Original user request is captured in session state.`,
		);
	} else {
		lines.push("- Active quest: none (substantive requests will automatically receive a persistent quest context in .pi/quest/current/<qid>/).");
	}

	const guidelines = projectGuidelines();
	if (guidelines) {
		lines.push("", guidelines);
	}

	const notes = standingNotes();
	if (notes) lines.push("", `## Standing project notes (\`${NOTES_FILE}\`)`, "", notes);

	return lines.join("\n");
}
