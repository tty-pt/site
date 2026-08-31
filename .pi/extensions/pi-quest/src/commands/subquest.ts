import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { QUEST_TEMPLATE } from "../markdown.ts";
import { sendInternalUserMessage } from "../messaging.ts";
import { fileExists, questDirPath } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadExistingQuestEpistemicState } from "../reconstruction.ts";
import { generateQuestId, state } from "../state.ts";
import { applyLoadedSubquestState, buildSubquestProtocolInstructions, linkSubQuestInParent, pushSubquestToStack } from "../subquest.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";
import { updateUIStatus } from "../ui.ts";
import { slugify } from "../paths.ts";

export async function handleSubquestCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	let raw = args.trim();
	if (!raw && ctx.mode === "tui") raw = ((await ctx.ui.input("Describe the sub-quest (e.g. handle auth edge cases):")) ?? "").trim();
	if (!raw) { ctx.ui.notify("Usage: /subquest [--plan|-p] <description...>", "warning"); return; }
	let switchNow = true;
	if (raw.startsWith("--plan ") || raw.startsWith("-p ") || raw.startsWith("--no-switch ")) { switchNow = false; raw = raw.replace(/^(--plan|-p|--no-switch)\s+/, "").trim(); }
	const goal = raw;
	const name = slugify(raw);
	const qid = state.questId || generateQuestId();
	state.questId = qid;
	await mkdir(questDirPath(qid), { recursive: true });
	const path = join(questDirPath(qid), `${name}.md`);
	const parentName = state.active || "";
	const isExisting = await fileExists(path);
	if (!isExisting) await writeFile(path, QUEST_TEMPLATE(name, goal, parentName, "", [], qid), "utf8");
	if (parentName) { await linkSubQuestInParent(parentName, name, goal, ctx); await verifyAndMarkSaved(pi, ctx, parentName); }
	if (!switchNow) { const msg = `Planned sub-quest **${name}** at \`${path}\`${parentName ? ` linked in parent **${parentName}**` : ""}. Kept active quest **${state.active}**.`; if (ctx.hasUI) ctx.ui.notify(msg, "info"); return; }
	pushSubquestToStack(state, parentName, name);
	const subLoaded = await loadExistingQuestEpistemicState(qid);
	applyLoadedSubquestState(state, goal, isExisting, subLoaded);
	await verifyAndMarkSaved(pi, ctx, name);
	persist(pi, ctx);
	updateUIStatus(ctx);
	const subquestMsg = buildSubquestProtocolInstructions(name, goal, parentName, path);
	sendInternalUserMessage(pi, subquestMsg);
}
