import { readFile } from "node:fs/promises";
import { compactionReady } from "../compaction.ts";
import { DEFAULT_CHECKPOINT_INTERVAL_TURNS, FUTURE_DIR } from "../constants.ts";
import { calculateCurrentTokens, usagePercent } from "../context.ts";
import { extractParentFromQuest, extractSubQuestsFromQuest } from "../markdown.ts";
import { logError } from "../messaging.ts";
import { fileExists, listActiveQuestRecords, listQuestFiles, questPath, resolveQuestRecordBySlug } from "../paths.ts";
import { state } from "../state.ts";
import { ExtensionContext } from "../types.ts";
import { formatQuestHierarchy, formatTokens } from "../utils.ts";
import { auditQuestConsistency } from "../validation.ts";

function buildParentChildMaps(records: any[]): { parentOf: Map<string, string>; childrenOf: Map<string, string[]> } {
	const parentOf = new Map<string, string>();
	const childrenOf = new Map<string, string[]>();
	for (const r of records) {
		const slug = r.name;
	}
	return { parentOf, childrenOf };
}

export async function handleQuestStatusCommand(_args: string, ctx: ExtensionContext): Promise<string> {
	if (state.pendingRootQuest) {
		const reqPreview = (state.pendingRootRequest || "").slice(0, 100);
		const line = `[PROVISIONAL ROOT INITIALIZATION] - Research required to establish quest identity and plan. Original request: "${reqPreview}..."`;
		if (ctx.hasUI) ctx.ui.notify(line, "info");
		return line;
	}
	if (!state.active) { if (ctx.hasUI) ctx.ui.notify("No active quest.", "info"); return "No active quest."; }
	const rec = await resolveQuestRecordBySlug(state.active);
	const path = rec ? rec.path : questPath(state.questId);
	const exists = Boolean(path && (await fileExists(path)));
	const fresh = compactionReady();
	const hier = formatQuestHierarchy(state.active, state.stack);
	const tokens = calculateCurrentTokens(ctx);
	const tokenStr = tokens !== null ? `${formatTokens(tokens)}` : `~${Math.round(usagePercent(ctx))}%`;
	const checkpointStr = `checkpoint every ${DEFAULT_CHECKPOINT_INTERVAL_TURNS} turns`;
	let parentInfo = "";
	const parentFromStack = state.stack && state.stack.length >= 2 ? state.stack[state.stack.length - 2] : null;
	if (parentFromStack) parentInfo = ` (parent: [[${parentFromStack}]])`;
	let subInfo = "";
	if (exists) {
		try {
			const content = await readFile(path, "utf8");
			const parent = extractParentFromQuest(content);
			if (parent) parentInfo = ` (parent: [[${parent}]])`;
			const subQuests = extractSubQuestsFromQuest(content);
			if (subQuests.length > 0) subInfo = ` | sub-quests: ${subQuests.join(", ")}`;
			const audit = auditQuestConsistency(content, { recentModifiedFiles: state.sessionModifiedFiles });
			if (!audit.consistent && audit.issues.length > 0) subInfo += ` | ⚠️ ${audit.issues.length} consistency issue(s)`;
		} catch (err: any) { logError(`Failed to read quest file for status at ${path}`, err, ctx); }
	}
	const qId = state.questId;
	const runInfo = qId ? ` | id: ${qId}` : "";
	const line = exists ? `${path}${parentInfo} [${hier}] - ${fresh ? "fresh" : "SAVE PENDING"}${runInfo}, tokens ${tokenStr} (${checkpointStr}), prompts ${state.prompts.length}${subInfo}` : `${path || state.active} - MISSING on disk!`;
	if (ctx.hasUI) ctx.ui.notify(`Active quest: ${line}`, fresh ? "info" : "warning");
	return line;
}

export async function handleQuestsCommand(_args: string, ctx: ExtensionContext): Promise<void> {
	const currentRecords = await listActiveQuestRecords();
	const future = await listQuestFiles(FUTURE_DIR);
	const parentOf = new Map<string, string>();
	const childrenOf = new Map<string, string[]>();
	for (const r of currentRecords) {
		const slug = r.name;
		try {
			const content = await readFile(r.path, "utf8");
			const p = extractParentFromQuest(content);
			if (p && currentRecords.some((cr) => cr.name === p || cr.qid === p)) {
				parentOf.set(slug, p);
				const list = childrenOf.get(p) || [];
				if (!list.includes(slug)) list.push(slug);
				childrenOf.set(p, list);
			}
			const subs = extractSubQuestsFromQuest(content);
			if (subs.length > 0) {
				const list = childrenOf.get(slug) || [];
				for (const s of subs) if (!list.includes(s)) list.push(s);
				childrenOf.set(slug, list);
			}
		} catch (err: any) { logError(`Failed to read quest file for parent linking at ${r.path}`, err, ctx); }
	}
	const renderedCurrent: string[] = [];
	for (const r of currentRecords) {
		const slug = r.name;
		if (parentOf.has(slug)) continue;
		const isActive = state.active === slug;
		renderedCurrent.push(`  ${slug}${isActive ? "  ◀ active" : ""}`);
		const subs = childrenOf.get(slug) || [];
		for (const sub of subs) {
			const isSubActive = state.active === sub;
			renderedCurrent.push(`    ↳ ${sub}${isSubActive ? "  ◀ active" : ""}`);
		}
	}
	const futureRows = future.length ? future.map((f) => `  ${f.replace(/\.md$/, "")}`) : ["  (none - use /quest-draft <name>)"];
	ctx.ui.setWidget("quest", [
		`Active: ${state.active ? (questPath(state.questId) || state.active) : "(none)"}`,
		"",
		"Current quests:",
		...(renderedCurrent.length ? renderedCurrent : ["  (none - use /quest <name>)"]),
		"",
		"Future / Backlog quests:",
		...futureRows,
	]);
}
