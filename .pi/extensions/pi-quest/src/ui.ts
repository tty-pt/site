import { compactionReady } from "./compaction.ts";
import { NOTES_FILE } from "./constants.ts";
import { getGuidelinesFingerprint, gitBranch, projectGuidelines, standingNotes } from "./context.ts";
import { questPath } from "./paths.ts";
import { generateQuestId, getActiveContext, state } from "./state.ts";
import { ExtensionContext } from "./types.ts";
import { formatQuestHierarchy, formatQuestShort } from "./utils.ts";
import { updateReviewerUIStatus } from "./critical_agent/tracker.ts";
import { logEvent } from "./logging.ts";

let lastAwarenessKey = "";
let lastAwarenessValue = "";

export { updateReviewerUIStatus };

export function updateUIStatus(ctx?: ExtensionContext) {
	const c = getActiveContext(ctx);
	if (c?.hasUI) {
		const fresh = compactionReady();

		// invariant: never null during quest — synthesize if needed (covers same-turn race)
		if ((state.active || state.activeDraft || state.pendingRootQuest) && !state.questId) {
			try { state.questId = generateQuestId(); } catch {}
		}

		const text = (state.active || state.activeDraft || state.pendingRootQuest)
			? formatQuestShort(state, fresh)
			: undefined;
		try { logEvent("UI_STATUS", text || "(none)", { quest: state.active || state.activeDraft || "", text: text || "(none)", fresh }); } catch {}
		if (typeof c.ui?.setStatus === "function") {
			c.ui.setStatus("quest", text);
		}
	}
}

export function buildSessionAwarenessBlock(ctx: ExtensionContext): string {
	const branch = gitBranch();
	const guidelineFp = getGuidelinesFingerprint();
	const fresh = state.active ? compactionReady() : true;
	const activeKey = state.active || state.pendingRootQuest || "none";
	const stackKey = state.stack ? state.stack.join(",") : "";
	const awarenessKey = `${ctx.cwd}|${branch || ""}|${state.questId || ""}|${activeKey}|${stackKey}|${fresh ? "fresh" : "dirty"}|${guidelineFp}`;
	const isSteadyAwareness = !state.pendingRootQuest && !state.researchRequired && !state.reassessmentRequired && (() => { try { return compactionReady(); } catch { return false; } })() && (state.researchRound || 1) > 1;

	if (isSteadyAwareness && lastAwarenessKey === awarenessKey && lastAwarenessValue) {
		return lastAwarenessValue;
	}

	const lines: string[] = [
		"# Session awareness (auto-injected)",
		"",
		`- Now: ${new Date().toISOString()}`,
		`- cwd: ${ctx.cwd}`,
	];
	if (branch) lines.push(`- Git branch: ${branch}`);
	if (state.questId) lines.push(`- Active quest ID: \`${state.questId}\``);
	if (state.activeDraft) lines.push(`- Draft: \`.pi/quest/future/${state.activeDraft}.md\` (id: ${state.questId || "provisional"})`);

	if (state.active) {
		const hier = formatQuestHierarchy(state.active, state.stack);
		const stackInfo = state.stack && state.stack.length > 1 ? ` | LIFO stack: [${state.stack.join(" → ")}]` : "";
		lines.push(
			`- Active quest: \`${questPath(state.questId)}\` [${hier}] (${fresh ? "fresh" : "SAVE PENDING - update it before compaction"}${stackInfo}); manage with /quest, /subquest, /quests, /quest-economy.`,
		);
	} else if (state.pendingRootQuest || state.activeDraft) {
		const draftPath = state.activeDraft ? `.pi/quest/future/${state.activeDraft}.md` : `.pi/quest/future/<draft>.md`;
		lines.push(
			`- Active quest: [PROVISIONAL ROOT INITIALIZATION] Research required to establish quest identity and plan. Use read/search/bash tools to investigate, determine a concise semantic quest name (e.g. 'persistent-agent-research', 'oauth-login-flow'), and call quest_update_state to initialize the durable quest with your research findings. Original user request is captured in session state. Draft: \`${draftPath}\` id: \`${state.questId || "provisional"}\`.`,
		);
	} else {
		lines.push("- Active quest: none (substantive requests will automatically receive a persistent quest context in .pi/quest/current/<qid>/).");
	}

	if (isSteadyAwareness) {
		lines.push("", `Guidelines: see AGENTS.md (${guidelineFp ? "cached" : "none"}) — full invariants already injected via system prompt; compact mode.`);
	} else {
		const guidelines = projectGuidelines();
		if (guidelines) {
			lines.push("", guidelines);
		}
	}

	const notes = standingNotes();
	if (notes) lines.push("", `## Standing project notes (\`${NOTES_FILE}\`)`, "", notes);

	const out = lines.join("\n");
	lastAwarenessKey = awarenessKey;
	lastAwarenessValue = out;
	return out;
}
