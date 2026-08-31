import { readFile } from "node:fs/promises";
import { resolve } from "node:path";
import { QUEST_CURRENT_DIR } from "../../constants.ts";
import { extractSubQuestsFromQuest } from "../../markdown.ts";
import { ParsedRunLog } from "../types.ts";
import { findArchivedQuestFile } from "./archive-lookup.ts";
import { DetailedLogEvent } from "./log-parser.ts";
import { QuestFileInfo } from "./quest-maps.ts";

export async function collectCapturedSubQuests(
	resolvedRoot: string | null,
	questMap: Map<string, QuestFileInfo>,
	rootPath: string | null,
	rootSubquestsMap: Map<string, Set<string>>,
	detailedEvents: DetailedLogEvent[],
	selectedRunLog: ParsedRunLog | null,
	projectRoot: string,
	archiveDocsDir: string,
): Promise<Array<{ name: string; path: string }>> {
	const capturedSubQuests: Array<{ name: string; path: string }> = [];
	const seenSubquests = new Set<string>();

	const descendantSet = rootSubquestsMap.get(resolvedRoot || "") || new Set();
	for (const sub of descendantSet) {
		const subInfo = questMap.get(sub);
		if (subInfo && !seenSubquests.has(sub)) {
			seenSubquests.add(sub);
			capturedSubQuests.push({ name: sub, path: subInfo.path });
		}
	}

	if (rootPath) {
		try {
			const rootContent = await readFile(rootPath, "utf8");
			const declared = extractSubQuestsFromQuest(rootContent);
			for (const sub of declared) {
				if (sub !== resolvedRoot && !seenSubquests.has(sub)) {
					if (questMap.has(sub)) {
						seenSubquests.add(sub);
						capturedSubQuests.push({ name: sub, path: questMap.get(sub)!.path });
					} else {
						const archSub = await findArchivedQuestFile(archiveDocsDir, sub);
						if (archSub) {
							seenSubquests.add(sub);
							capturedSubQuests.push({ name: sub, path: archSub.path });
						} else {
							seenSubquests.add(sub);
							capturedSubQuests.push({ name: sub, path: resolve(projectRoot, QUEST_CURRENT_DIR, sub, "quest.md") });
						}
					}
				}
			}
		} catch {}
	}

	for (const ev of detailedEvents) {
		if (ev.parent === resolvedRoot && ev.child && !seenSubquests.has(ev.child)) {
			const sub = ev.child;
			if (questMap.has(sub)) {
				seenSubquests.add(sub);
				capturedSubQuests.push({ name: sub, path: questMap.get(sub)!.path });
			} else {
				const archSub = await findArchivedQuestFile(archiveDocsDir, sub);
				if (archSub) {
					seenSubquests.add(sub);
					capturedSubQuests.push({ name: sub, path: archSub.path });
				} else {
					seenSubquests.add(sub);
					capturedSubQuests.push({ name: sub, path: resolve(projectRoot, QUEST_CURRENT_DIR, sub, "quest.md") });
				}
			}
		}
	}

	if (selectedRunLog) {
		for (const sub of selectedRunLog.subquests) {
			if (!seenSubquests.has(sub) && sub !== resolvedRoot) {
				if (questMap.has(sub)) {
					seenSubquests.add(sub);
					capturedSubQuests.push({ name: sub, path: questMap.get(sub)!.path });
				} else {
					const archSub = await findArchivedQuestFile(archiveDocsDir, sub);
					if (archSub) {
						seenSubquests.add(sub);
						capturedSubQuests.push({ name: sub, path: archSub.path });
					} else {
						seenSubquests.add(sub);
						capturedSubQuests.push({ name: sub, path: resolve(projectRoot, QUEST_CURRENT_DIR, sub, "quest.md") });
					}
				}
			}
		}
	}

	capturedSubQuests.sort((a, b) => a.name.localeCompare(b.name));
	return capturedSubQuests;
}
