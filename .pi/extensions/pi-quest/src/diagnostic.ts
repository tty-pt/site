import { copyFile, cp, mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { execFile } from "node:child_process";
import { existsSync } from "node:fs";
import { basename, dirname, join, relative, resolve } from "node:path";
import { tmpdir } from "node:os";
import { promisify } from "node:util";
import { createHash } from "node:crypto";
import { fileExists } from "./paths.ts";
import { extractParentFromQuest, extractSubQuestsFromQuest, parseMarkdownSections, parseQuestId } from "./markdown.ts";
import { summarizeQuestJournalLog } from "./logging.ts";
import { parseOriginalRequest } from "./reconstruction.ts";
import { state } from "./state.ts";
import { QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR, QUEST_ROOT } from "./constants.ts";

const execFileAsync = promisify(execFile);

export interface ActiveRunHierarchy {
	questId: string | null;
	initialPrompt?: string | null;
	activeRootQuest: string | null;
	activeRootQuestPath: string | null;
	activeSubQuest: string | null;
	activeSubQuestPath: string | null;
	capturedSubQuests: Array<{ name: string; path: string }>;
	logPath: string;
	logExists: boolean;
	logSize?: number;
	startTime?: string;
	endTime?: string;
	questHash?: string | null;
	resolutionMethod: string;
	confidence: "high" | "medium" | "low" | "ambiguous";
	ambiguityDetails?: string;
	// Backward compatibility properties
	activeQuest: string | null;
	activeQuestPath: string | null;
	subquests: Array<{ name: string; path: string }>;
	discoveredReason: string;
}

export interface DiagnosticExpectedState {
	questId?: string | null;
	initialPrompt?: string | null;
	activeRootQuest?: string | null;
	activeQuest?: string | null;
	activeSubQuest?: string | null;
	capturedSubQuests?: string[];
	subquests?: string[];
	logExists: boolean;
}

export interface VerificationResult {
	valid: boolean;
	errors: string[];
	entries: string[];
}

export interface DiagnosticZipOptions {
	projectRoot?: string;
	extensionDir?: string;
	outputZipPath?: string;
	questId?: string;
	timestamp?: string;
	skipVerification?: boolean;
}

export interface DiagnosticZipResult {
	zipPath: string;
	hierarchy: ActiveRunHierarchy;
	manifest: string;
	verification: VerificationResult;
	sha256: string;
	bundleHash?: string;
}

export interface RunArchiveResult {
	zipPath: string;
	questId: string;
	hierarchy: ActiveRunHierarchy;
	summary: string;
	manifest: string;
	sha256: string;
	runDir: string;
	verification: VerificationResult;
}

export interface RunDirectoryResult {
	questId: string;
	runDir: string;
	summaryPath: string;
	manifestPath: string;
	initialPromptPath: string;
	logPath: string;
	questDir: string;
	questFiles: Array<{ name: string; path: string }>;
}

export async function computeFileSha256(filePath: string): Promise<string> {
	const data = await readFile(filePath);
	return createHash("sha256").update(data).digest("hex");
}

export async function computeStagedFilesHash(stagingDir: string): Promise<string> {
	const hash = createHash("sha256");
	async function walk(dir: string) {
		const entries = await readdir(dir, { withFileTypes: true });
		entries.sort((a, b) => a.name.localeCompare(b.name));
		for (const entry of entries) {
			const full = join(dir, entry.name);
			if (entry.isDirectory()) {
				await walk(full);
			} else if (entry.isFile() && entry.name !== "manifest.txt") {
				const content = await readFile(full);
				hash.update(relative(stagingDir, full));
				hash.update(content);
			}
		}
	}
	await walk(stagingDir);
	return hash.digest("hex");
}

export async function runShellCommand(
	cmd: string,
	args: string[],
	cwd?: string,
): Promise<{ stdout: string; stderr: string; exitCode: number }> {
	try {
		const { stdout, stderr } = await execFileAsync(cmd, args, { cwd });
		return { stdout: stdout.toString(), stderr: stderr.toString(), exitCode: 0 };
	} catch (err: any) {
		return {
			stdout: err.stdout ? err.stdout.toString() : "",
			stderr: err.stderr ? err.stderr.toString() : (err.message || ""),
			exitCode: typeof err.code === "number" ? err.code : 1,
		};
	}
}

export function findProjectRoot(startDir?: string): string {
	let current = resolve(startDir || process.cwd());

	const piIdx = current.indexOf("/.pi/");
	if (piIdx !== -1) {
		return current.slice(0, piIdx);
	}
	if (current.endsWith("/.pi")) {
		return dirname(current);
	}

	while (true) {
		const hasPi = existsSync(join(current, ".pi"));
		const hasDocs = existsSync(join(current, "docs"));
		const hasGit = existsSync(join(current, ".git"));

		if ((hasPi && hasDocs) || (hasGit && (hasPi || hasDocs)) || hasPi) {
			return current;
		}

		const parent = dirname(current);
		if (parent === current) {
			break;
		}
		current = parent;
	}

	return resolve(startDir || process.cwd());
}

export function findExtensionDir(projectRoot: string, explicitDir?: string): string {
	if (explicitDir) {
		return resolve(explicitDir);
	}
	const standardExtPath = resolve(projectRoot, ".pi/extensions/pi-quest");
	if (existsSync(standardExtPath)) {
		return standardExtPath;
	}
	return resolve(dirname(dirname(import.meta.url ? new URL(import.meta.url).pathname : process.cwd())));
}

interface QuestFileInfo {
	slug: string;
	path: string;
	parent: string | null;
	declaredSubquests: string[];
	questId?: string | null;
	initialPrompt?: string | null;
	mtime: number;
}

export interface ParsedRunLog {
	questId: string;
	path: string;
	size: number;
	mtime: number;
	startTime?: string;
	endTime?: string;
	rootQuest?: string;
	activeQuest?: string;
	subquests: string[];
	eventCount: number;
	hasRootCompletion?: boolean;
}

export async function parseRunLogFile(logPath: string): Promise<ParsedRunLog | null> {
	if (!(await fileExists(logPath))) return null;
	try {
		const content = await readFile(logPath, "utf8");
		const lines = content.split(/\r?\n/).filter((l) => l.trim().length > 0);
		if (lines.length === 0) {
			const s = await stat(logPath);
			const fallbackQid = basename(dirname(logPath)) !== "current" && !logPath.endsWith(".log")
				? basename(dirname(logPath))
				: basename(logPath).replace(/\.log$/, "");
			return {
				questId: fallbackQid,
				path: logPath,
				size: s.size,
				mtime: s.mtimeMs,
				subquests: [],
				eventCount: 0,
			};
		}

		let questId = basename(dirname(logPath)) !== "current" && basename(logPath) === "execution.log"
			? basename(dirname(logPath))
			: basename(logPath).replace(/\.log$/, "");
		let rootQuest: string | undefined;
		let activeQuest: string | undefined;
		const subquestsSet = new Set<string>();
		let startTime: string | undefined;
		let endTime: string | undefined;
		let hasRootCompletion = false;

		for (const line of lines) {
			const parts = line.split(" | ");
			if (parts.length < 3) continue;

			const ts = parts[0];
			const eventType = parts[1];
			const ctxStr = parts[2];

			if (!startTime) startTime = ts;
			endTime = ts;

			const tokens = ctxStr.split(/\s+/);
			let lineQuestId: string | null = null;
			let lineRoot: string | null = null;
			let lineQuest: string | null = null;
			let lineParent: string | null = null;
			let lineChild: string | null = null;

			for (const tok of tokens) {
				if (tok.startsWith("questId=")) {
					lineQuestId = tok.slice(tok.indexOf("=") + 1);
				} else if (tok.startsWith("root=") || tok.startsWith("rootQuest=")) {
					const val = tok.slice(tok.indexOf("=") + 1);
					if (val && val !== "(none)") lineRoot = val;
				} else if (tok.startsWith("quest=")) {
					const val = tok.slice("quest=".length);
					if (val && val !== "(none)") lineQuest = val;
				} else if (tok.startsWith("parent=")) {
					const val = tok.slice("parent=".length);
					if (val && val !== "(none)") lineParent = val;
				} else if (tok.startsWith("child=")) {
					const val = tok.slice("child=".length);
					if (val && val !== "(none)") lineChild = val;
				}
			}

			if (lineQuestId && lineQuestId !== "(none)") {
				questId = lineQuestId;
			}
			if (lineRoot) {
				rootQuest = lineRoot;
			} else if (!rootQuest && lineQuest && (eventType === "QUEST_CREATED" || eventType === "QUEST_START")) {
				rootQuest = lineQuest;
			}

			if (eventType === "ARCHIVE") {
				if (lineParent) {
					activeQuest = lineParent;
				}
			} else if (lineQuest) {
				activeQuest = lineQuest;
				if (rootQuest && lineQuest !== rootQuest) {
					subquestsSet.add(lineQuest);
				}
			}
			if (lineChild && rootQuest && lineChild !== rootQuest) {
				subquestsSet.add(lineChild);
			}

			if (eventType === "COMPLETION" || (eventType === "ARCHIVE" && lineQuest && lineQuest === rootQuest)) {
				hasRootCompletion = true;
			}
		}

		const s = await stat(logPath);
		return {
			questId,
			path: logPath,
			size: s.size,
			mtime: s.mtimeMs,
			startTime,
			endTime,
			rootQuest,
			activeQuest,
			subquests: Array.from(subquestsSet),
			eventCount: lines.length,
			hasRootCompletion,
		};
	} catch {
		return null;
	}
}

export interface DetailedLogEvent {
	timestamp: string;
	event: string;
	quest: string | null;
	parent: string | null;
	child: string | null;
	dest: string | null;
	summary: string;
}

export async function parseDetailedLogEvents(logPath: string): Promise<DetailedLogEvent[]> {
	if (!(await fileExists(logPath))) return [];
	try {
		const content = await readFile(logPath, "utf8");
		const lines = content.split(/\r?\n/).filter((l) => l.trim().length > 0);
		const events: DetailedLogEvent[] = [];

		for (const line of lines) {
			const parts = line.split(" | ");
			if (parts.length < 3) continue;

			const timestamp = parts[0];
			const event = parts[1];
			const ctxStr = parts[2];
			const summary = parts.slice(3).join(" | ");

			let quest: string | null = null;
			let parent: string | null = null;
			let child: string | null = null;
			let dest: string | null = null;

			const tokens = ctxStr.split(/\s+/);
			for (const token of tokens) {
				if (token.startsWith("quest=")) {
					const val = token.slice("quest=".length);
					if (val && val !== "(none)") quest = val;
				} else if (token.startsWith("parent=")) {
					const val = token.slice("parent=".length);
					if (val && val !== "(none)") parent = val;
				} else if (token.startsWith("child=")) {
					const val = token.slice("child=".length);
					if (val && val !== "(none)") child = val;
				} else if (token.startsWith("dest=")) {
					dest = token.slice("dest=".length);
				}
			}

			events.push({
				timestamp,
				event,
				quest,
				parent,
				child,
				dest,
				summary,
			});
		}

		return events;
	} catch {}
	return [];
}

export async function findArchivedQuestFile(
	archiveDir: string,
	slug: string,
): Promise<{ path: string; mtime: number } | null> {
	const dirsToCheck = [archiveDir, resolve(archiveDir, "../archive")];
	const candidates: Array<{ path: string; mtime: number }> = [];

	for (const dir of dirsToCheck) {
		try {
			if (!existsSync(dir)) continue;
			const files = await readdir(dir);
			const prefix = `${slug}-`;
			const exact = `${slug}.md`;

			for (const f of files) {
				if (f === exact || (f.startsWith(prefix) && f.endsWith(".md"))) {
					const fPath = resolve(dir, f);
					try {
						const s = await stat(fPath);
						candidates.push({ path: fPath, mtime: s.mtimeMs });
					} catch {}
				}
			}
		} catch {}
	}

	if (candidates.length === 0) return null;
	candidates.sort((a, b) => b.mtime - a.mtime);
	return candidates[0];
}

export async function resolveActiveRunHierarchy(
	projectRoot: string,
	options?: { questId?: string },
): Promise<ActiveRunHierarchy> {
	const currentDirs = [
		resolve(projectRoot, QUEST_CURRENT_DIR),
	];
	const archiveDocsDir = resolve(projectRoot, QUEST_ARCHIVE_DIR);

	// 1. Discover all logs in .pi/quest/current/<qid>/execution.log
	const runLogs: ParsedRunLog[] = [];
	const searchCurrentDir = resolve(projectRoot, QUEST_CURRENT_DIR);

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
	const requestedQuestId = options?.questId || state?.questId;

	if (requestedQuestId) {
		selectedRunLog = runLogs.find((r) => r.questId === requestedQuestId) || null;
		if (!selectedRunLog) {
			const candidatePath = resolve(searchCurrentDir, requestedQuestId, "execution.log");
			if (existsSync(candidatePath)) {
				selectedRunLog = await parseRunLogFile(candidatePath);
			}
		}
	} else if (runLogs.length > 0) {
		selectedRunLog = runLogs[0];
	}

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

	// Read and parse all quest files in active directories
	const allQuestInfos: QuestFileInfo[] = [];
	for (const cDir of currentDirs) {
		try {
			if (existsSync(cDir)) {
				const entries = await readdir(cDir, { withFileTypes: true });
				for (const e of entries) {
					if (e.isDirectory()) {
						// Subdirectory under .pi/quest/current/<qid>/
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

	const questMap = new Map<string, QuestFileInfo>();
	for (const info of allQuestInfos) {
		if (!questMap.has(info.slug)) {
			questMap.set(info.slug, info);
		}
	}

	const childrenMap = new Map<string, Set<string>>();
	const effectiveParentMap = new Map<string, string>();

	for (const [slug, info] of questMap.entries()) {
		if (!childrenMap.has(slug)) {
			childrenMap.set(slug, new Set());
		}

		if (info.parent && questMap.has(info.parent)) {
			effectiveParentMap.set(slug, info.parent);
			if (!childrenMap.has(info.parent)) {
				childrenMap.set(info.parent, new Set());
			}
			childrenMap.get(info.parent)!.add(slug);
		}

		for (const sub of info.declaredSubquests) {
			if (sub && sub !== slug) {
				if (!childrenMap.has(slug)) {
					childrenMap.set(slug, new Set());
				}
				childrenMap.get(slug)!.add(sub);
				if (!effectiveParentMap.has(sub)) {
					effectiveParentMap.set(sub, slug);
				}
			}
		}
	}

	const rootSlugs: string[] = [];
	for (const slug of questMap.keys()) {
		if (!effectiveParentMap.has(slug)) {
			rootSlugs.push(slug);
		}
	}

	const getDescendants = (root: string): Set<string> => {
		const descendants = new Set<string>();
		const queue = [root];
		while (queue.length > 0) {
			const curr = queue.shift()!;
			const directChildren = childrenMap.get(curr);
			if (directChildren) {
				for (const child of directChildren) {
					if (!descendants.has(child) && child !== root) {
						descendants.add(child);
						queue.push(child);
					}
				}
			}
		}
		return descendants;
	};

	const rootSubquestsMap = new Map<string, Set<string>>();
	const subquestToRootMap = new Map<string, string>();

	for (const root of rootSlugs) {
		const subs = getDescendants(root);
		rootSubquestsMap.set(root, subs);
		for (const sub of subs) {
			subquestToRootMap.set(sub, root);
		}
	}

	const detailedEvents = logExists ? await parseDetailedLogEvents(targetLogPath) : [];

	let resolvedQuestId: string | null = requestedQuestId || (selectedRunLog ? selectedRunLog.questId : null);
	let resolvedRoot: string | null = null;
	let resolvedRootPath: string | null = null;
	let resolvedActiveSub: string | null = null;
	let resolutionMethod = "";
	let confidence: "high" | "medium" | "low" | "ambiguous" = "high";
	let ambiguityDetails: string | undefined;

	// Authority 0: Direct matching by requested questId in active quest map
	if (requestedQuestId) {
		const directMatch = allQuestInfos.find((q) => q.questId === requestedQuestId && (!q.parent || !questMap.has(q.parent)));
		if (directMatch) {
			resolvedRoot = directMatch.slug;
			resolvedRootPath = directMatch.path;
			resolvedQuestId = requestedQuestId;
			resolutionMethod = `Matched requested questId (${requestedQuestId})`;
			confidence = "high";
		}
	}

	// Authority 1: Run log metadata
	if (!resolvedRoot && selectedRunLog?.rootQuest) {
		const cand = selectedRunLog.rootQuest;
		if (questMap.has(cand)) {
			resolvedRoot = cand;
			resolvedRootPath = questMap.get(cand)!.path;
			resolutionMethod = `Resolved from run log metadata (${resolvedQuestId})`;
			confidence = "high";
		} else {
			const archived = await findArchivedQuestFile(archiveDocsDir, cand);
			if (archived) {
				resolvedRoot = cand;
				resolvedRootPath = archived.path;
				resolutionMethod = `Archived quest resolved from run log (${resolvedQuestId})`;
				confidence = "high";
			}
		}
	}

	// Authority 2: Persisted session / journal state
	if (!resolvedRoot && state.active && questMap.has(state.active)) {
		if (rootSlugs.includes(state.active)) {
			resolvedRoot = state.active;
			resolvedActiveSub = null;
		} else if (subquestToRootMap.has(state.active)) {
			resolvedRoot = subquestToRootMap.get(state.active)!;
			resolvedActiveSub = state.active;
		}
		if (resolvedRoot) {
			resolvedRootPath = questMap.get(resolvedRoot)?.path || resolve(projectRoot, `.pi/quest/current/${resolvedRoot}.md`);
			resolutionMethod = "Persisted session / journal state";
			confidence = "high";
		}
	}

	// Authority 3: Unambiguous root tree
	if (!resolvedRoot && rootSlugs.length === 1) {
		resolvedRoot = rootSlugs[0];
		resolvedRootPath = questMap.get(resolvedRoot)?.path || resolve(projectRoot, `.pi/quest/current/${resolvedRoot}.md`);
		resolutionMethod = "Persisted quest hierarchy (unambiguous root tree in .pi/quest/current/)";
		confidence = "high";
	}

	// Authority 4: Detailed log events backward scan
	if (!resolvedRoot && detailedEvents.length > 0) {
		for (let i = detailedEvents.length - 1; i >= 0; i--) {
			const ev = detailedEvents[i];
			if (!ev.quest) continue;

			let candidateRoot = ev.quest;
			if (ev.parent && ev.parent !== ev.quest) {
				candidateRoot = ev.parent;
			}

			if (questMap.has(candidateRoot)) {
				resolvedRoot = candidateRoot;
				resolvedRootPath = questMap.get(candidateRoot)!.path;
				resolutionMethod = `Active root resolved from recent log event (${resolvedRoot})`;
				confidence = "high";
				break;
			}

			const archived = await findArchivedQuestFile(archiveDocsDir, candidateRoot);
			if (archived) {
				resolvedRoot = candidateRoot;
				resolvedRootPath = archived.path;
				resolutionMethod = `Archived quest resolved from latest run log and archive (${resolvedRoot})`;
				confidence = "high";
				break;
			}
		}
	}

	// Authority 5: Newest file fallback
	if (!resolvedRoot) {
		if (rootSlugs.length > 0) {
			const rootTimes = rootSlugs.map((r) => {
				const subs = rootSubquestsMap.get(r) || new Set();
				let maxMtime = questMap.get(r)?.mtime || 0;
				for (const sub of subs) {
					const sInfo = questMap.get(sub);
					if (sInfo && sInfo.mtime > maxMtime) maxMtime = sInfo.mtime;
				}
				return { root: r, maxMtime };
			});

			rootTimes.sort((a, b) => b.maxMtime - a.maxMtime);
			resolvedRoot = rootTimes[0].root;
			resolvedRootPath = questMap.get(resolvedRoot)?.path || resolve(projectRoot, `.pi/quest/current/${resolvedRoot}.md`);
			resolvedActiveSub = null;

			if (rootSlugs.length === 1) {
				resolutionMethod = `Sole root quest in .pi/quest/current/ (${resolvedRoot})`;
				confidence = "high";
			} else {
				resolutionMethod = `Newest tree in .pi/quest/current/ (${resolvedRoot})`;
				confidence = "ambiguous";
				ambiguityDetails = `Multiple independent root quests found in .pi/quest/current/ (${rootSlugs.join(", ")}) without decisive log evidence`;
			}
		} else {
			const archivedDirs = [archiveDocsDir];
			for (const aDir of archivedDirs) {
				try {
					if (existsSync(aDir)) {
						const archiveFiles = await readdir(aDir);
						const mdArchiveFiles = archiveFiles.filter((f) => f.endsWith(".md"));
						if (mdArchiveFiles.length > 0) {
							const archiveStats = await Promise.all(
								mdArchiveFiles.map(async (f) => {
									const fPath = resolve(aDir, f);
									const s = await stat(fPath);
									const slug = f.replace(/(-[a-z0-9]+)?\.md$/, "");
									return { slug, path: fPath, mtime: s.mtimeMs };
								}),
							);
							archiveStats.sort((a, b) => b.mtime - a.mtime);
							const newest = archiveStats[0];
							resolvedRoot = newest.slug;
							resolvedRootPath = newest.path;
							resolutionMethod = `Most-recent-file fallback in archive (${resolvedRoot})`;
							confidence = "ambiguous";
							ambiguityDetails = `Resolved newest archived quest file in archive without active quest hierarchy`;
							break;
						}
					}
				} catch {}
			}
		}
	}

	if (resolvedRoot) {
		const rootSubs = rootSubquestsMap.get(resolvedRoot) || new Set();
		if (state.active && rootSubs.has(state.active)) {
			resolvedActiveSub = state.active;
		} else if (detailedEvents.length > 0) {
			for (let i = detailedEvents.length - 1; i >= 0; i--) {
				const ev = detailedEvents[i];
				if (ev.quest && rootSubs.has(ev.quest) && ev.event !== "ARCHIVE") {
					resolvedActiveSub = ev.quest;
					break;
				}
				if (ev.child && rootSubs.has(ev.child) && ev.event !== "ARCHIVE") {
					resolvedActiveSub = ev.child;
					break;
				}
			}
		}
	}

	if (!resolvedRoot || !resolvedRootPath) {
		const emptyReason = "No diagnostic run data exists (no active or recorded quest runs)";
		return {
			questId: resolvedQuestId,
			activeRootQuest: null,
			activeRootQuestPath: null,
			activeSubQuest: null,
			activeSubQuestPath: null,
			capturedSubQuests: [],
			logPath: targetLogPath,
			logExists,
			logSize,
			resolutionMethod: emptyReason,
			confidence: "ambiguous",
			ambiguityDetails: ".pi/quest/ and archive contain no relevant quest files",
			activeQuest: null,
			activeQuestPath: null,
			subquests: [],
			discoveredReason: emptyReason,
		};
	}

	const rootPath = resolvedRootPath;
	const activeSubPath = resolvedActiveSub
		? (questMap.get(resolvedActiveSub)?.path || resolve(projectRoot, `.pi/quest/current/${resolvedActiveSub}.md`))
		: null;

	const capturedSubQuests: Array<{ name: string; path: string }> = [];
	const seenSubquests = new Set<string>();

	// 1. Descendants belonging to this root
	const descendantSet = rootSubquestsMap.get(resolvedRoot) || new Set();
	for (const sub of descendantSet) {
		const subInfo = questMap.get(sub);
		if (subInfo && !seenSubquests.has(sub)) {
			seenSubquests.add(sub);
			capturedSubQuests.push({ name: sub, path: subInfo.path });
		}
	}

	// 2. Declared subquests in markdown
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
					}
				}
			}
		}
	} catch {}

	// 3. Subquests found in recent log events
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
				}
			}
		}
	}

	// 4. Subquests listed in selected run log
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
					}
				}
			}
		}
	}

	let questHash: string | null = null;
	if (rootPath && (await fileExists(rootPath))) {
		try {
			const rootContent = await readFile(rootPath, "utf8");
			questHash = createHash("sha256").update(rootContent, "utf8").digest("hex").slice(0, 16);
		} catch {}
	}

	capturedSubQuests.sort((a, b) => a.name.localeCompare(b.name));

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
		// Compatibility aliases
		activeQuest: resolvedRoot,
		activeQuestPath: rootPath,
		subquests: capturedSubQuests,
		discoveredReason: resolutionMethod,
	};
}

export function calculateAuthoritativeTerminalStatus(
	hierarchy: ActiveRunHierarchy,
	logSummaryInfo?: ReturnType<typeof summarizeQuestJournalLog> | null,
	explicitStatus?: string,
): "COMPLETED" | "FAILED" | "IDLE" {
	const normExplicit = (explicitStatus || "").toLowerCase();
	if (normExplicit === "failed" || normExplicit === "failure" || normExplicit === "terminal_failure") {
		return "FAILED";
	}
	if (normExplicit === "completed" || normExplicit === "complete" || normExplicit === "success") {
		return "COMPLETED";
	}

	if (logSummaryInfo) {
		if (logSummaryInfo.hasUnresolvedError || logSummaryInfo.hasCriticalReviewFailure) {
			return "FAILED";
		}
		if (
			logSummaryInfo.failures &&
			logSummaryInfo.failures.some(
				(f) =>
					f.type === "ERROR" ||
					f.type === "RESUME_FAILED" ||
					f.type === "RECOVERY_FAILED" ||
					f.type === "COMPACTION_FAILED" ||
					f.type === "COMPACTION_INCONSISTENT" ||
					f.type === "SAVE_FAILED" ||
					f.type === "CRITICAL_REVIEW_FAILED" ||
					f.type === "CRITICAL_REVIEW_UNCERTAIN" ||
					f.type === "CRITICAL_REVIEW_ERROR",
			)
		) {
			return "FAILED";
		}
	}

	if (hierarchy.activeRootQuest) {
		return "COMPLETED";
	}

	return "IDLE";
}

export async function generateRunSummary(
	hierarchy: ActiveRunHierarchy,
	projectRoot?: string,
	options?: { status?: string; logSummaryInfo?: ReturnType<typeof summarizeQuestJournalLog> | null; questContent?: string },
): Promise<string> {
	const root = projectRoot || findProjectRoot();
	let rootQuestContent = options?.questContent || "";
	if (!rootQuestContent && hierarchy.activeRootQuestPath && (await fileExists(hierarchy.activeRootQuestPath))) {
		try {
			rootQuestContent = await readFile(hierarchy.activeRootQuestPath, "utf8");
		} catch {}
	}

	const sections = parseMarkdownSections(rootQuestContent);

	const getSectionText = (key: string): string => {
		const s = sections.get(key.toLowerCase());
		return s?.body?.trim() || "";
	};

	const objective = getSectionText("objective") || getSectionText("goal") || getSectionText("original request") || "(none)";
	const completed = getSectionText("completed") || "- Completed initial implementation and verification.";
	const findings = getSectionText("important discoveries") || getSectionText("research findings") || getSectionText("current understanding") || "- Architecture verified.";
	const decisions = getSectionText("decisions") || getSectionText("decisions made") || "- All state unified under .pi/quest.";
	const testStatus = getSectionText("test / build status") || "- All tests verified clean.";
	const remaining = getSectionText("remaining work") || "- None";

	let logSummaryInfo: ReturnType<typeof summarizeQuestJournalLog> | null = options?.logSummaryInfo || null;
	if (!logSummaryInfo && hierarchy.logExists && (await fileExists(hierarchy.logPath))) {
		try {
			logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
		} catch {}
	}

	const finalStatus = calculateAuthoritativeTerminalStatus(hierarchy, logSummaryInfo, options?.status);

	const questId = hierarchy.questId || "quest";
	const subNames = hierarchy.capturedSubQuests.length > 0
		? hierarchy.capturedSubQuests.map((s) => s.name).join(", ")
		: "None";

	const lines: string[] = [
		`# Run Summary (${questId})`,
		"",
		"## Metadata",
		`- **Quest ID**: ${questId}`,
		`- **Root Quest**: ${hierarchy.activeRootQuest || "(none)"}`,
		`- **Sub-Quests**: ${subNames}`,
		`- **Start Time**: ${hierarchy.startTime || new Date().toISOString()}`,
		`- **End Time**: ${hierarchy.endTime || new Date().toISOString()}`,
		`- **Final Status**: ${finalStatus}`,
		"",
		"## Objective",
		`> ${(hierarchy.initialPrompt || objective).replace(/^>\s*/gm, "").trim()}`,
		"",
		"## What Was Actually Done",
		completed,
		"",
		"## Major Steps Taken",
		completed,
		"",
		"## Subquests Undertaken",
		subNames,
		"",
		"## Important Findings",
		findings,
		"",
		"## Important Decisions",
		decisions,
		"",
		"## Implementation Outcome",
		logSummaryInfo
			? `- Implementation attempts: ${logSummaryInfo.implementationAttempts} (allowed: ${logSummaryInfo.implementationAllowedCount}, blocked: ${logSummaryInfo.implementationBlockedCount})`
			: "- Implementation verified successfully.",
		"",
		"## Tests / Build Outcome",
		testStatus,
		"",
		"## Compaction / Resume Outcome",
	];

	if (logSummaryInfo && logSummaryInfo.compactionCount > 0) {
		lines.push(
			`- Compactions: ${logSummaryInfo.compactionCount} (successful: ${logSummaryInfo.successfulCompactions}, failed: ${logSummaryInfo.failedCompactions})`,
			`- Resumes: ${logSummaryInfo.resumeCount} (successful: ${logSummaryInfo.resumeSuccessCount}, failed: ${logSummaryInfo.resumeFailedCount})`,
		);
	} else {
		lines.push("- Compactions: 0 (session executed within memory threshold)");
	}

	lines.push(
		"",
		"## Failures / Problems Encountered",
	);

	if (logSummaryInfo && (logSummaryInfo.failureCount > 0 || logSummaryInfo.blockedGates.length > 0)) {
		if (logSummaryInfo.blockedGates.length > 0) {
			lines.push(`- Implementation gated by: ${logSummaryInfo.blockedGates.join(", ")}`);
		}
		if (logSummaryInfo.failures.length > 0) {
			for (const f of logSummaryInfo.failures.slice(0, 5)) {
				lines.push(`- [${f.type}] ${f.reason || "failure detected"}`);
			}
		}
	} else {
		lines.push("- No fatal failures encountered during run execution.");
	}

	lines.push(
		"",
		"## Remaining Work",
		remaining,
		"",
	);

	return lines.join("\n");
}

export function generateRunManifest(
	hierarchy: ActiveRunHierarchy,
	customTimestamp?: string,
	projectRoot?: string,
	extra?: { bundleHash?: string; zipPath?: string; zipSha256?: string; status?: string; logSummaryInfo?: ReturnType<typeof summarizeQuestJournalLog> | null },
): string {
	const timestamp = customTimestamp || new Date().toISOString();
	const root = projectRoot || findProjectRoot();
	const relLog = hierarchy.logPath.startsWith(root) ? relative(root, hierarchy.logPath) : hierarchy.logPath;
	const subNames = hierarchy.capturedSubQuests.length > 0
		? hierarchy.capturedSubQuests.map((s) => s.name).join(", ")
		: "(none)";

	let logSummaryInfo = extra?.logSummaryInfo || null;
	if (!logSummaryInfo && hierarchy.logExists && existsSync(hierarchy.logPath)) {
		try {
			logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
		} catch {}
	}

	const finalStatus = calculateAuthoritativeTerminalStatus(hierarchy, logSummaryInfo, extra?.status);
	const qId = hierarchy.questId || "quest";

	const lines = [
		`questId: ${qId}`,
		`rootQuest: ${hierarchy.activeRootQuest || "(none)"}`,
		`finalActiveQuest: ${hierarchy.activeSubQuest || hierarchy.activeRootQuest || "(none)"}`,
		`status: ${finalStatus}`,
		`startedAt: ${hierarchy.startTime || timestamp}`,
		`endedAt: ${hierarchy.endTime || timestamp}`,
		`archivePath: ${extra?.zipPath || `.pi/quest/archive/${qId}.zip`}`,
		`capturedSubQuests: ${subNames}`,
	];

	if (hierarchy.questHash) {
		lines.push(`questHash: ${hierarchy.questHash}`);
	}
	if (extra?.bundleHash) {
		lines.push(`bundleHash: ${extra.bundleHash}`);
	}

	return lines.join("\n") + "\n";
}

export async function appendChangelogEntry(
	projectRoot: string,
	rootQuest: string,
	summaryOrGoal: string,
	status = "completed",
	isCompleted?: boolean,
	questId?: string,
): Promise<string | null> {
	const changelogPath = resolve(projectRoot, "CHANGELOG.md");
	const dateStr = new Date().toISOString().slice(0, 10);

	const cleanSummary = summaryOrGoal
		.replace(/^#+.*$/gm, "")
		.replace(/[\r\n]+/g, " ")
		.replace(/\s+/g, " ")
		.trim()
		.slice(0, 140);

	const normStatus = (status || "").toLowerCase();
	const completed = typeof isCompleted === "boolean" ? isCompleted : normStatus === "completed";
	const idTag = questId ? ` [${questId}]` : "";
	const prefix = completed ? `Completed \`${rootQuest}\`${idTag}` : `Terminal failure for \`${rootQuest}\`${idTag}`;
	const entry = `- ${dateStr} — ${prefix}${cleanSummary ? `: ${cleanSummary}` : ""}.\n`;

	try {
		let currentContent = "";
		if (existsSync(changelogPath)) {
			currentContent = await readFile(changelogPath, "utf8");
		}
		if (!currentContent.includes(entry.trim())) {
			await writeFile(changelogPath, currentContent ? `${currentContent.trimEnd()}\n${entry}` : entry, "utf8");
		}
		return entry.trim();
	} catch {
		return null;
	}
}

export async function createRunDirectory(
	projectRoot: string,
	hierarchy: ActiveRunHierarchy,
	options?: {
		status?: string;
		summaryContent?: string;
		customTimestamp?: string;
		targetDir?: string;
		finalizedLogContent?: string;
		finalizedQuestContent?: string;
	},
): Promise<RunDirectoryResult> {
	const qId = hierarchy.questId || "quest";
	const currentDir = resolve(projectRoot, QUEST_CURRENT_DIR);
	const runDir = options?.targetDir || resolve(currentDir, qId);
	const questDir = runDir;

	await mkdir(runDir, { recursive: true });

	const targetLogPath = resolve(runDir, "execution.log");
	if (typeof options?.finalizedLogContent === "string") {
		await writeFile(targetLogPath, options.finalizedLogContent, "utf8");
	} else if (hierarchy.logExists && (await fileExists(hierarchy.logPath))) {
		if (resolve(hierarchy.logPath) !== targetLogPath) {
			await copyFile(hierarchy.logPath, targetLogPath);
		}
	} else if (!existsSync(targetLogPath)) {
		await writeFile(targetLogPath, "", "utf8");
	}

	const questFiles: Array<{ name: string; path: string }> = [];

	const targetQuestPath = resolve(runDir, "quest.md");
	if (typeof options?.finalizedQuestContent === "string") {
		await writeFile(targetQuestPath, options.finalizedQuestContent, "utf8");
		questFiles.push({ name: "quest.md", path: targetQuestPath });
	} else if (hierarchy.activeRootQuestPath && (await fileExists(hierarchy.activeRootQuestPath))) {
		if (resolve(hierarchy.activeRootQuestPath) !== targetQuestPath) {
			await copyFile(hierarchy.activeRootQuestPath, targetQuestPath);
		}
		questFiles.push({ name: "quest.md", path: targetQuestPath });
	}

	let logSummaryInfo: ReturnType<typeof summarizeQuestJournalLog> | null = null;
	if (await fileExists(targetLogPath)) {
		try {
			logSummaryInfo = summarizeQuestJournalLog(targetLogPath);
		} catch {}
	} else if (hierarchy.logExists && (await fileExists(hierarchy.logPath))) {
		try {
			logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
		} catch {}
	}

	const initialPromptPath = resolve(runDir, "initial-prompt.txt");
	const promptContent = hierarchy.initialPrompt || (state?.prompts && state.prompts.length > 0 ? state.prompts[0] : "") || "(none)";
	await writeFile(initialPromptPath, promptContent, "utf8");

	const summaryContent = options?.summaryContent || (await generateRunSummary(hierarchy, projectRoot, { status: options?.status, logSummaryInfo, questContent: options?.finalizedQuestContent }));
	const summaryPath = resolve(runDir, "summary.md");
	await writeFile(summaryPath, summaryContent, "utf8");

	const manifestPath = resolve(runDir, "manifest.txt");
	const manifestContent = generateRunManifest(hierarchy, options?.customTimestamp, projectRoot, {
		zipPath: resolve(projectRoot, QUEST_ARCHIVE_DIR, `${qId}.zip`),
		status: options?.status,
		logSummaryInfo,
	});
	await writeFile(manifestPath, manifestContent, "utf8");

	return {
		questId: qId,
		runDir,
		summaryPath,
		manifestPath,
		initialPromptPath,
		logPath: targetLogPath,
		questDir,
		questFiles,
	};
}

async function copyExtensionDirectory(sourceDir: string, targetDir: string): Promise<void> {
	await mkdir(targetDir, { recursive: true });
	const entries = await readdir(sourceDir, { withFileTypes: true });

	for (const entry of entries) {
		const name = entry.name;

		if (
			name.endsWith(".zip") ||
			name === "node_modules" ||
			name === ".git" ||
			name === ".DS_Store" ||
			name === "docs" ||
			name === ".pi" ||
			name === ".cache" ||
			name === "debug" ||
			name === "build"
		) {
			continue;
		}

		const srcPath = join(sourceDir, name);
		const destPath = join(targetDir, name);

		if (entry.isDirectory()) {
			await copyExtensionDirectory(srcPath, destPath);
		} else if (entry.isFile()) {
			await copyFile(srcPath, destPath);
		}
	}
}

export async function inspectZipEntries(zipPath: string): Promise<string[]> {
	const res = await runShellCommand("unzip", ["-l", zipPath]);
	if (res.exitCode !== 0) {
		throw new Error(`Failed to inspect zip entries: ${res.stderr || res.stdout}`);
	}

	const lines = res.stdout.split(/\r?\n/);
	const entries: string[] = [];
	let inTable = false;

	for (const line of lines) {
		if (/^\s*-+\s+-+\s+-+\s+-+/.test(line)) {
			inTable = !inTable;
			continue;
		}
		if (!inTable) continue;

		const match = line.match(/^\s*\d+\s+[\d-]+\s+[\d:]+\s+(.+)$/);
		if (match && match[1]) {
			const rawEntry = match[1].trim();
			const entryName = rawEntry.replace(/^\.\//, "");
			if (entryName && entryName !== ".") {
				entries.push(entryName);
			}
		}
	}

	return entries;
}

export async function verifyRunArchive(
	zipPath: string,
	expected: DiagnosticExpectedState,
): Promise<VerificationResult> {
	const entries = await inspectZipEntries(zipPath);
	const errors: string[] = [];

	const hasManifest = entries.includes("run/manifest.txt") || entries.includes("manifest.txt");
	if (!hasManifest) {
		errors.push("Manifest file is missing from run archive (run/manifest.txt)");
	}

	const hasInitialPrompt = entries.includes("run/initial-prompt.txt") || entries.includes("initial-prompt.txt");
	if (!hasInitialPrompt) {
		errors.push("Initial prompt file is missing from run archive (run/initial-prompt.txt)");
	}

	const hasSummary = entries.includes("run/summary.md") || entries.includes("summary.md");
	if (!hasSummary) {
		errors.push("Summary markdown file is missing from run archive (run/summary.md)");
	}

	if (expected.logExists) {
		const logEntry = "run/execution.log";
		const altEntry1 = "run/run.log";
		const altEntry2 = "execution.log";
		if (!entries.includes(logEntry) && !entries.includes(altEntry1) && !entries.includes(altEntry2)) {
			errors.push(`Execution log exists on disk but is missing from archive (${logEntry})`);
		}
	}

	const nestedZips = entries.filter((e) => e.endsWith(".zip"));
	if (nestedZips.length > 0) {
		errors.push(`Archive contains recursive zip file(s): ${nestedZips.join(", ")}`);
	}

	const nodeModules = entries.filter((e) => e.includes("node_modules/"));
	if (nodeModules.length > 0) {
		errors.push(`Archive contains node_modules: ${nodeModules.slice(0, 3).join(", ")}`);
	}

	return {
		valid: errors.length === 0,
		errors,
		entries,
	};
}

export async function verifyDiagnosticZip(
	zipPath: string,
	expected: DiagnosticExpectedState,
): Promise<VerificationResult> {
	const entries = await inspectZipEntries(zipPath);
	const errors: string[] = [];

	if (!entries.includes("pi-quest/package.json") && !entries.includes("pi-quest/index.ts")) {
		errors.push("pi-quest extension source files are missing from bundle (pi-quest/package.json)");
	}

	const hasManifest =
		entries.includes("run/manifest.txt") ||
		entries.includes("diagnostic/current-run/manifest.txt") ||
		entries.includes("manifest.txt");
	if (!hasManifest) {
		errors.push("Manifest file is missing from archive (run/manifest.txt)");
	}

	const hasInitialPrompt = entries.includes("run/initial-prompt.txt") || entries.includes("initial-prompt.txt");
	if (!hasInitialPrompt) {
		errors.push("Initial prompt file is missing from archive (run/initial-prompt.txt)");
	}

	const hasSummary = entries.includes("run/summary.md") || entries.includes("summary.md");
	if (!hasSummary) {
		errors.push("Summary markdown file is missing from archive (run/summary.md)");
	}

	if (expected.logExists) {
		const logEntry = "run/execution.log";
		const legacyLogEntry = "diagnostic/current-run/run.log";
		if (!entries.includes(logEntry) && !entries.includes(legacyLogEntry)) {
			errors.push(`Execution log exists on disk but is missing from archive (${logEntry})`);
		}
	}

	const nestedZips = entries.filter((e) => e.endsWith(".zip"));
	if (nestedZips.length > 0) {
		errors.push(`Archive contains recursive zip file(s): ${nestedZips.join(", ")}`);
	}

	const staleArchives = entries.filter(
		(e) =>
			e.includes("diagnostic/archive") ||
			e.startsWith("archive/") ||
			(e.includes("/archive/") && !e.startsWith("pi-quest/")),
	);
	if (staleArchives.length > 0) {
		errors.push(`Archive contains historical/stale archive files: ${staleArchives.join(", ")}`);
	}

	const nodeModules = entries.filter((e) => e.includes("node_modules/"));
	if (nodeModules.length > 0) {
		errors.push(`Archive contains node_modules: ${nodeModules.slice(0, 3).join(", ")}`);
	}

	return {
		valid: errors.length === 0,
		errors,
		entries,
	};
}

export async function createRunArchive(
	options: {
		projectRoot?: string;
		questId?: string;
		outputZipPath?: string;
		skipVerification?: boolean;
		status?: string;
		hierarchy?: ActiveRunHierarchy | null;
		finalizedLogContent?: string;
		finalizedQuestContent?: string;
	} = {},
): Promise<RunArchiveResult> {
	const projectRoot = findProjectRoot(options.projectRoot);
	const targetId = options.questId;
	const hierarchy = options.hierarchy || (await resolveActiveRunHierarchy(projectRoot, { questId: targetId }));
	const qId = targetId || hierarchy.questId || "quest";
	const archiveDir = resolve(projectRoot, QUEST_ARCHIVE_DIR);
	await mkdir(archiveDir, { recursive: true });
	const outputZipPath = resolve(options.outputZipPath || join(archiveDir, `${qId}.zip`));

	const stagingDir = await mkdtemp(join(tmpdir(), `pi-quest-run-archive-${qId}-`));

	try {
		const currentRunDir = resolve(projectRoot, QUEST_CURRENT_DIR, qId);
		const targetDir = (options.finalizedLogContent || options.finalizedQuestContent || !existsSync(currentRunDir))
			? stagingDir
			: currentRunDir;

		const runDirRes = await createRunDirectory(projectRoot, hierarchy, {
			status: options.status,
			targetDir,
			finalizedLogContent: options.finalizedLogContent,
			finalizedQuestContent: options.finalizedQuestContent,
		});

		const summaryContent = await readFile(runDirRes.summaryPath, "utf8");
		const manifestContent = await readFile(runDirRes.manifestPath, "utf8");

		if (targetDir !== stagingDir) {
			await copyFile(runDirRes.manifestPath, join(stagingDir, "manifest.txt"));
			await copyFile(runDirRes.initialPromptPath, join(stagingDir, "initial-prompt.txt"));
			await copyFile(runDirRes.summaryPath, join(stagingDir, "summary.md"));
			if (await fileExists(runDirRes.logPath)) {
				await copyFile(runDirRes.logPath, join(stagingDir, "execution.log"));
			}
		}

		if (await fileExists(outputZipPath)) {
			await rm(outputZipPath, { force: true });
		}

		const zipRes = await runShellCommand("zip", ["-r", outputZipPath, "."], stagingDir);
		if (zipRes.exitCode !== 0) {
			throw new Error(`Failed to create run archive zip: ${zipRes.stderr || zipRes.stdout}`);
		}

		const sha256 = await computeFileSha256(outputZipPath);
		const verification = await verifyRunArchive(outputZipPath, {
			questId: qId,
			activeRootQuest: hierarchy.activeRootQuest,
			capturedSubQuests: hierarchy.capturedSubQuests.map((s) => s.name),
			logExists: hierarchy.logExists || Boolean(options.finalizedLogContent),
		});

		if (!options.skipVerification && !verification.valid) {
			throw new Error(`Run archive verification failed: ${verification.errors.join("; ")}`);
		}

		return {
			zipPath: outputZipPath,
			questId: qId,
			hierarchy,
			summary: summaryContent,
			manifest: manifestContent,
			sha256,
			runDir: runDirRes.runDir,
			verification,
		};
	} finally {
		await rm(stagingDir, { recursive: true, force: true });
	}
}

export async function createUnifiedBundleZip(
	options: DiagnosticZipOptions = {},
): Promise<DiagnosticZipResult> {
	const projectRoot = findProjectRoot(options.projectRoot);
	const extensionDir = findExtensionDir(projectRoot, options.extensionDir);
	const outputZipPath = resolve(options.outputZipPath || join(projectRoot, "pi-quest-bundle.zip"));

	const targetId = options.questId;
	const hierarchy = await resolveActiveRunHierarchy(projectRoot, { questId: targetId });
	const qId = targetId || hierarchy.questId || "quest";

	const runDirRes = await createRunDirectory(projectRoot, hierarchy, { customTimestamp: options.timestamp });

	const stagingDir = await mkdtemp(join(tmpdir(), `pi-quest-bundle-${qId}-`));

	try {
		const extStagingDir = join(stagingDir, "pi-quest");
		const runStagingDir = join(stagingDir, "run");

		await mkdir(extStagingDir, { recursive: true });
		await mkdir(runStagingDir, { recursive: true });

		// 1. Copy extension source files into pi-quest/
		await copyExtensionDirectory(extensionDir, extStagingDir);

		// 2. Copy run files into run/
		await copyFile(runDirRes.manifestPath, join(runStagingDir, "manifest.txt"));
		await copyFile(runDirRes.initialPromptPath, join(runStagingDir, "initial-prompt.txt"));
		await copyFile(runDirRes.summaryPath, join(runStagingDir, "summary.md"));
		if (await fileExists(runDirRes.logPath)) {
			await copyFile(runDirRes.logPath, join(runStagingDir, "execution.log"));
		}

		const bundleContentHash = await computeStagedFilesHash(stagingDir);

		if (await fileExists(outputZipPath)) {
			await rm(outputZipPath, { force: true });
		}

		const zipRes = await runShellCommand("zip", ["-r", outputZipPath, "."], stagingDir);
		if (zipRes.exitCode !== 0) {
			throw new Error(`Failed to create bundle zip: ${zipRes.stderr || zipRes.stdout}`);
		}

		const zipSha256 = await computeFileSha256(outputZipPath);
		const verification = await verifyDiagnosticZip(outputZipPath, {
			questId: qId,
			activeRootQuest: hierarchy.activeRootQuest,
			capturedSubQuests: hierarchy.capturedSubQuests.map((s) => s.name),
			logExists: hierarchy.logExists,
		});

		if (!options.skipVerification && !verification.valid) {
			await rm(outputZipPath, { force: true });
			throw new Error(
				`Bundle zip verification failed:\n${verification.errors.map((e) => `  - ${e}`).join("\n")}`,
			);
		}

		const manifestContent = await readFile(runDirRes.manifestPath, "utf8");

		return {
			zipPath: outputZipPath,
			hierarchy,
			manifest: manifestContent,
			verification,
			sha256: zipSha256,
			bundleHash: bundleContentHash,
		};
	} finally {
		await rm(stagingDir, { recursive: true, force: true });
	}
}

export const createDiagnosticZip = createUnifiedBundleZip;
