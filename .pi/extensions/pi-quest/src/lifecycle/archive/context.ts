import { readFile } from "node:fs/promises";
import { extractParentFromQuest } from "../../markdown.ts";
import { logError } from "../../messaging.ts";
import { fileExists, questPath, resolveQuestRecordBySlug } from "../../paths.ts";
import { extractChildResultSummary } from "../../reconstruction.ts";
import { getState } from "../../state.ts";
import { QuestErrorCode } from "../../constants.ts";

export interface ArchiveContext {
	targetQid: string;
	path: string;
	questName: string;
	parentSlug: string | null;
	questContent: string;
	childSummary: string;
	isRoot: boolean;
}

export async function resolveArchiveContext(name: string, ctx?: any): Promise<ArchiveContext & { error?: string }> {
	const s = getState(ctx);
	let targetQid = s.questId || name;
	let path = questPath(targetQid);
	let questName = name;

	const record = await resolveQuestRecordBySlug(name);
	if (record) {
		targetQid = record.qid;
		path = record.path;
		questName = record.name;
	} else if (!(await fileExists(path))) {
		return { targetQid, path, questName, parentSlug: null, questContent: "", childSummary: "", isRoot: true, error: `No quest file found at ${path}` };
	}

	let parentSlug: string | null = null;
	const stack = Array.isArray(s.stack) ? [...s.stack] : (s.active ? [s.active] : []);
	const idx = stack.lastIndexOf(questName);
	if (idx > 0) parentSlug = stack[idx - 1];
	if (!parentSlug) {
		const targetRec = await resolveQuestRecordBySlug(questName);
		if (targetRec && (targetRec as any).parent) parentSlug = (targetRec as any).parent;
	}

	let childSummary = "";
	let questContent = "";
	try {
		questContent = await readFile(path, "utf8");
		if (!parentSlug) parentSlug = extractParentFromQuest(questContent);
		childSummary = extractChildResultSummary(questContent, questName);
	} catch (err: any) {
		logError(`Failed to read quest file for parent extraction at ${path}`, err, ctx, QuestErrorCode.STATE_RECONSTRUCTION_FAILURE);
	}

	return { targetQid, path, questName, parentSlug, questContent, childSummary, isRoot: !parentSlug };
}
