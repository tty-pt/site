import { existsSync } from "node:fs";
import { readFile, readdir, stat } from "node:fs/promises";
import { resolve } from "node:path";
import { createHash } from "node:crypto";
import { QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR } from "../../constants.ts";
import { extractParentFromQuest, extractSubQuestsFromQuest, parseMarkdownSections, parseQuestId } from "../../markdown.ts";
import { fileExists } from "../../paths.ts";
import { parseOriginalRequest } from "../../reconstruction.ts";
import { state } from "../../state.ts";
import { ActiveRunHierarchy, ParsedRunLog } from "../types.ts";
import { parseDetailedLogEvents, parseRunLogFile } from "./log-parser.ts";
import { collectCapturedSubQuests } from "./captured.ts";
import { QuestFileInfo, buildQuestMaps, computeDescendantMaps } from "./quest-maps.ts";

async function discoverRunLogs(
	projectRoot: string,
	selectedQuestId?: string,
): Promise<{ runLogs: ParsedRunLog[]; selectedRunLog: ParsedRunLog | null }> {
	const searchCurrentDir = resolve(projectRoot, QUEST_CURRENT_DIR);
	const runLogs: ParsedRunLog[] = [];

	try {
		if (existsSync(searchCurrentDir)) {
			const entries = await readdir(searchCurrentDir, { withFileTypes: true });
			for (const e of entries) {
				if (e.isDirectory()) {
					const nestedExec = resolve(searchCurrentDir, e.name, "execution.log");
					if (existsSync(nestedExec)) {
						const parsed = await parseRunLogFile(nestedExec);
						if (parsed) runLogs.push(parsed);
					}
				}
			}
		}
	} catch {}

	runLogs.sort((a, b) => {
		const timeA = a.endTime ? Date.parse(a.endTime) : a.mtime;
		const timeB = b.endTime ? Date.parse(b.endTime) : b.mtime;
		return timeB - timeA;
	});

	let selectedRunLog: ParsedRunLog | null = null;

	if (selectedQuestId) {
		selectedRunLog = runLogs.find((r) => r.questId === selectedQuestId) || null;
		if (!selectedRunLog) {
			const candidatePath = resolve(searchCurrentDir, selectedQuestId, "execution.log");
			if (existsSync(candidatePath)) {
				selectedRunLog = await parseRunLogFile(candidatePath);
			}
		}
	} else if (runLogs.length > 0) {
		selectedRunLog = runLogs[0];
	}

	return { runLogs, selectedRunLog };
}

async function collectQuestInfos(projectRoot: string, requestedQuestId: string | null): Promise<QuestFileInfo[]> {
	const currentDirs = [resolve(projectRoot, QUEST_CURRENT_DIR)];
	const allQuestInfos: QuestFileInfo[] = [];
	for (const cDir of currentDirs) {
		try {
			if (existsSync(cDir)) {
				const entries = await readdir(cDir, { withFileTypes: true });
				for (const e of entries) {
					if (e.isDirectory()) {
						const nestedDir = resolve(cDir, e.name);
						try {
							const subEntries = await readdir(nestedDir, { withFileTypes: true });
							for (const subFile of subEntries) {
								if (subFile.isFile() && subFile.name.endsWith(".md") && subFile.name !== "summary.md") {
									const nestedQuest = resolve(nestedDir, subFile.name);
									try {
										const content = await readFile(nestedQuest, "utf8");
										const parent = extractParentFromQuest(content);
										const declaredSubquests = extractSubQuestsFromQuest(content);
										const qId = parseQuestId(content) || e.name;
										const sections = parseMarkdownSections(content);
										const origReq = parseOriginalRequest(sections);
										const s = await stat(nestedQuest);
										const headerMatch = content.match(/^#\s+Quest:\s*([^\r\n]+)/m);
										const slug = headerMatch ? headerMatch[1].trim() : (subFile.name === "quest.md" ? e.name : subFile.name.replace(/\.md$/, ""));
										allQuestInfos.push({
											slug,
											path: nestedQuest,
											parent,
											declaredSubquests,
											questId: qId,
											initialPrompt: origReq,
											mtime: s.mtimeMs,
										});
									} catch {}
								}
							}
						} catch {}
					}
				}
			}
		} catch {}
	}

	allQuestInfos.sort((a, b) => {
		if (requestedQuestId) {
			if (a.questId === requestedQuestId && b.questId !== requestedQuestId) return -1;
			if (b.questId === requestedQuestId && a.questId !== requestedQuestId) return 1;
		}
		return b.mtime - a.mtime;
	});

	return allQuestInfos;
}

async function resolveRoot(
	selectedRunLog: ParsedRunLog | null,
	questMap: Map<string, QuestFileInfo>,
	rootSlugs: string[],
	rootSubquestsMap: Map<string, Set<string>>,
	subquestToRootMap: Map<string, string>,
	targetLogPath: string,
): Promise<{ resolvedRoot: string | null; resolvedActiveSub: string | null; resolvedQuestId: string | null; resolutionMethod: string; confidence: "high" | "medium" | "low" | "ambiguous"; ambiguityDetails?: string }> {
	let resolvedRoot: string | null = null;
	let resolvedActiveSub: string | null = null;
	let resolvedQuestId: string | null = null;
	let resolutionMethod = "none";
	let confidence: "high" | "medium" | "low" | "ambiguous" = "low";
	let ambiguityDetails: string | undefined;

	const detailedEvents = await parseDetailedLogEvents(targetLogPath);

	if (selectedRunLog?.rootQuest && questMap.has(selectedRunLog.rootQuest)) {
		resolvedRoot = selectedRunLog.rootQuest;
		resolutionMethod = "run_log_root";
		confidence = "high";
	}

	if (!resolvedRoot && (state?.active || state?.stack?.length)) {
		const activeCandidate = state?.active || (state.stack && state.stack.length > 0 ? state.stack[0] : null);
		if (activeCandidate) {
			if (rootSubquestsMap.has(activeCandidate)) {
				resolvedRoot = activeCandidate;
				resolutionMethod = "in_memory_state_root";
				confidence = "high";
			} else if (subquestToRootMap.has(activeCandidate)) {
				resolvedRoot = subquestToRootMap.get(activeCandidate)!;
				resolvedActiveSub = activeCandidate;
				resolutionMethod = "in_memory_state_subquest";
				confidence = "high";
			}
		}
	}

	if (!resolvedRoot && rootSlugs.length > 0) {
		resolvedRoot = rootSlugs[0];
		resolutionMethod = rootSlugs.length === 1 ? "single_unparented_quest" : "most_recent_unparented_quest";
		confidence = rootSlugs.length === 1 ? "high" : "medium";
		if (rootSlugs.length > 1) {
			ambiguityDetails = `Multiple potential root quests found: ${rootSlugs.join(", ")}. Selected most recent: ${resolvedRoot}.`;
		}
	}

	if (resolvedRoot) {
		if (state?.active && state.active !== resolvedRoot && questMap.has(state.active)) {
			resolvedActiveSub = state.active;
		} else if (selectedRunLog?.activeQuest && selectedRunLog.activeQuest !== resolvedRoot && questMap.has(selectedRunLog.activeQuest)) {
			resolvedActiveSub = selectedRunLog.activeQuest;
		} else {
			for (let i = detailedEvents.length - 1; i >= 0; i--) {
				const ev = detailedEvents[i];
				if (ev.quest && ev.quest !== resolvedRoot && questMap.has(ev.quest)) {
					resolvedActiveSub = ev.quest;
					break;
				}
			}
		}
	}

	return { resolvedRoot, resolvedActiveSub, resolvedQuestId, resolutionMethod, confidence, ambiguityDetails };
}

export async function resolveActiveRunHierarchy(
	projectRoot: string,
	options?: { questId?: string },
): Promise<ActiveRunHierarchy> {
	const archiveDocsDir = resolve(projectRoot, QUEST_ARCHIVE_DIR);
	const searchCurrentDir = resolve(projectRoot, QUEST_CURRENT_DIR);
	const requestedQuestId = options?.questId || state?.questId || null;

	const { selectedRunLog } = await discoverRunLogs(projectRoot, requestedQuestId || undefined);

	let targetLogPath: string;
	let logExists = false;
	let logSize: number | undefined;

	if (selectedRunLog) {
		targetLogPath = selectedRunLog.path;
		logExists = true;
		logSize = selectedRunLog.size;
	} else {
		targetLogPath = resolve(searchCurrentDir, requestedQuestId || "default", "execution.log");
		logExists = existsSync(targetLogPath);
		if (logExists) {
			try {
				const s = await stat(targetLogPath);
				logSize = s.size;
			} catch {}
		}
	}

	const allQuestInfos = await collectQuestInfos(projectRoot, requestedQuestId);
	const { questMap, childrenMap, effectiveParentMap } = buildQuestMaps(allQuestInfos);

	const rootSlugs: string[] = [];
	for (const slug of questMap.keys()) {
		if (!effectiveParentMap.has(slug)) {
			rootSlugs.push(slug);
		}
	}

	const { rootSubquestsMap, subquestToRootMap } = computeDescendantMaps(childrenMap, rootSlugs);

	const rootResolution = await resolveRoot(selectedRunLog, questMap, rootSlugs, rootSubquestsMap, subquestToRootMap, targetLogPath);
	let { resolvedRoot, resolvedActiveSub, resolutionMethod, confidence, ambiguityDetails } = rootResolution;
	let resolvedQuestId: string | null = requestedQuestId;

	const detailedEvents = await parseDetailedLogEvents(targetLogPath);

	if (resolvedRoot) {
		if (state?.active && state.active !== resolvedRoot && questMap.has(state.active)) {
			resolvedActiveSub = state.active;
		} else if (selectedRunLog?.activeQuest && selectedRunLog.activeQuest !== resolvedRoot && questMap.has(selectedRunLog.activeQuest)) {
			resolvedActiveSub = selectedRunLog.activeQuest;
		}
	}

	const rootPath = resolvedRoot && questMap.has(resolvedRoot) ? questMap.get(resolvedRoot)!.path : null;
	const activeSubPath = resolvedActiveSub && questMap.has(resolvedActiveSub)
		? questMap.get(resolvedActiveSub)!.path
		: null;

	const capturedSubQuests = await collectCapturedSubQuests(
		resolvedRoot,
		questMap,
		rootPath,
		rootSubquestsMap,
		detailedEvents,
		selectedRunLog,
		projectRoot,
		archiveDocsDir,
	);

	let questHash: string | null = null;
	if (rootPath && (await fileExists(rootPath))) {
		try {
			const rootContent = await readFile(rootPath, "utf8");
			questHash = createHash("sha256").update(rootContent, "utf8").digest("hex").slice(0, 16);
		} catch {}
	}

	const rootInfo = resolvedRoot ? questMap.get(resolvedRoot) : null;
	if (!resolvedQuestId && rootInfo?.questId) {
		resolvedQuestId = rootInfo.questId;
	}
	const initialPrompt = rootInfo?.initialPrompt || (state?.prompts && state.prompts.length > 0 ? state.prompts[0] : null);

	return {
		questId: resolvedQuestId,
		initialPrompt,
		activeRootQuest: resolvedRoot,
		activeRootQuestPath: rootPath,
		activeSubQuest: resolvedActiveSub,
		activeSubQuestPath: activeSubPath,
		capturedSubQuests,
		logPath: targetLogPath,
		logExists,
		logSize,
		startTime: selectedRunLog?.startTime,
		endTime: selectedRunLog?.endTime,
		questHash,
		resolutionMethod,
		confidence,
		ambiguityDetails,
		activeQuest: resolvedRoot,
		activeQuestPath: rootPath,
		subquests: capturedSubQuests,
		discoveredReason: resolutionMethod,
	};
}
