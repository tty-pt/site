import { existsSync } from "node:fs";
import { stat, unlink, readdir, readFile } from "node:fs/promises";
import { join } from "node:path";
import { FUTURE_DIR, QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR } from "./constants.ts";
import { getQuestId } from "./state.ts";
import { ExtensionContext } from "./types.ts";

export interface QuestRecord {
	qid: string;
	name: string;
	path: string;
	dir: string;
	logPath: string;
	parent?: string | null;
	goal?: string;
	originalRequest?: string;
	mtime?: number;
}

export function questDirPath(qid?: string | null, baseDir?: string): string {
	const targetQid = qid || getQuestId();
	if (!targetQid) return "";
	return baseDir ? join(baseDir, targetQid) : `${QUEST_CURRENT_DIR}/${targetQid}`;
}

export function questPath(qid?: string | null, baseDir?: string): string {
	const dir = questDirPath(qid, baseDir);
	return dir ? join(dir, "quest.md") : "";
}

export function questWorkingFilePath(qid?: string | null, baseDir?: string): string {
	return questPath(qid, baseDir);
}

export function questLogPath(qid?: string | null, baseDir?: string): string {
	const dir = questDirPath(qid, baseDir);
	return dir ? join(dir, "execution.log") : "";
}

export function questMetaPath(qid?: string | null, baseDir?: string): string {
	const dir = questDirPath(qid, baseDir);
	return dir ? join(dir, "meta.json") : "";
}

export function questPlanPath(qid?: string | null, baseDir?: string): string {
	const dir = questDirPath(qid, baseDir);
	return dir ? join(dir, "plan.md") : "";
}

export function questResearchPath(qid?: string | null, baseDir?: string): string {
	const dir = questDirPath(qid, baseDir);
	return dir ? join(dir, "research.md") : "";
}

export function questExecutionPath(qid?: string | null, baseDir?: string): string {
	const dir = questDirPath(qid, baseDir);
	return dir ? join(dir, "execution.md") : "";
}

export function listShardPaths(qid?: string | null, baseDir?: string): string[] {
	return [
		questPath(qid, baseDir),
		questPlanPath(qid, baseDir),
		questResearchPath(qid, baseDir),
		questExecutionPath(qid, baseDir),
		questMetaPath(qid, baseDir),
	].filter(Boolean);
}

export function questArchivePath(qid: string, projectRoot?: string): string {
	const root = projectRoot || "";
	return root ? join(root, QUEST_ARCHIVE_DIR, `${qid}.zip`) : `${QUEST_ARCHIVE_DIR}/${qid}.zip`;
}

export async function fileExists(p: string): Promise<boolean> {
	try {
		await readFile(p);
		return true;
	} catch {
		return false;
	}
}

export function slugify(name: string, maxLen = 80): string {
	if (!name || typeof name !== "string") return "";
	let slug = name
		.trim()
		.toLowerCase()
		.replace(/[^a-z0-9.\-_]+/g, "-")
		.replace(/-{2,}/g, "-")
		.replace(/^-+|-+$/g, "");
	if (slug.length > maxLen) {
		const cut = slug.slice(0, maxLen);
		const lastHyphen = cut.lastIndexOf("-");
		slug = lastHyphen > 20 ? cut.slice(0, lastHyphen) : cut;
		slug = slug.replace(/-+$/, "");
	}
	return slug;
}

export function generateSlugFromPrompt(prompt: string, maxLen = 45): string {
	if (!prompt || typeof prompt !== "string") return "quest";

	// Strip code blocks and backticks
	let text = prompt.replace(/```[\s\S]*?```/g, "").replace(/`[^`]+`/g, "");

	// Strip polite/preamble prefixes
	text = text.replace(/^(please\s+|can\s+you\s+|could\s+you\s+|help\s+me\s+|i\s+want\s+to\s+|i\s+need\s+to\s+|we\s+need\s+to\s+|let'?s\s+)+/i, "");

	const stopWords = new Set([
		"a", "an", "the", "in", "on", "at", "to", "for", "of", "with", "by", "from",
		"about", "into", "through", "during", "before", "after", "above", "below",
		"and", "or", "but", "so", "as", "is", "are", "was", "were", "be", "been",
		"being", "have", "has", "had", "do", "does", "did", "can", "could", "should",
		"would", "will", "shall", "may", "might", "must", "that", "this", "these",
		"those", "my", "your", "our", "their", "it", "its"
	]);

	const rawWords = text
		.toLowerCase()
		.replace(/[^a-z0-9\s-_]/g, " ")
		.split(/\s+/)
		.filter((w) => w.length > 1 && !stopWords.has(w));

	const selectedWords = rawWords.slice(0, 6);
	let slug = selectedWords.join("-");

	if (!slug || slug.length < 3) {
		slug = slugify(prompt, maxLen);
	}
	if (!slug) {
		slug = `quest-${Date.now().toString(36)}`;
	}

	return slugify(slug, maxLen);
}

export function shouldStartPersistentQuest(prompt: string): boolean {
	if (!prompt || typeof prompt !== "string") return false;
	const trimmed = prompt.trim();
	if (trimmed.length < 10) return false;
	if (trimmed.startsWith("/")) return false;

	const lower = trimmed.toLowerCase();

	// Pure conversational greetings / acknowledgments / short remarks
	const conversationalOnly = /^(hi|hello|hey|greetings|thanks|thank you|thx|ok|okay|k|yes|no|yep|nope|sure|sounds good|looks good|lgtm|continue|go ahead|proceed|approved|got it|cool|nice|great|good|fine|done|quit|exit)[.!?\s]*$/i;
	if (conversationalOnly.test(lower)) return false;

	// Simple non-action factual / informational queries
	const isInformationalQuery =
		/^(what is|what are|what does|where is|where are|who is|how do i|how does|how can i|can you explain|explain to me|tell me about|is there a|are there any|why is|why does|why do)\b/i.test(lower);

	if (isInformationalQuery && trimmed.length < 150) {
		const hasActionKeywords = /\b(implement|build|refactor|fix|debug|rewrite|migrate|redesign|create feature|add feature)\b/i.test(lower);
		if (!hasActionKeywords) return false;
	}

	// Syntax questions, quick one-liners
	if (/^(is this valid|is this correct|check this syntax|how to write|how do i write)\b/i.test(lower) && trimmed.length < 120) {
		return false;
	}

	// Explicit action keywords that indicate substantive work
	const actionKeywords = [
		"implement", "build", "refactor", "fix", "investigate", "debug",
		"migrate", "redesign", "analyze", "create", "add", "update",
		"modify", "rewrite", "optimize", "integrate", "support",
		"resolve", "patch", "architecture", "benchmark", "cleanup",
		"clean up", "develop", "repertoire", "feature", "test suite",
		"e2e", "passkey", "auth", "picker", "endpoint", "handler"
	];

	for (const kw of actionKeywords) {
		const regex = new RegExp(`\\b${kw.replace(/\s+/g, "\\s+")}\\b`, "i");
		if (regex.test(lower)) return true;
	}

	// Substantial multi-word prompt describing requirements or tasks
	const words = trimmed.split(/\s+/).filter(Boolean);
	if (trimmed.length >= 50 && words.length >= 6) {
		return true;
	}

	return false;
}

export async function listActiveQuestRecords(baseDir: string = QUEST_CURRENT_DIR): Promise<QuestRecord[]> {
	const records: QuestRecord[] = [];
	try {
		const entries = await readdir(baseDir, { withFileTypes: true });
		for (const entry of entries) {
			if (entry.isDirectory()) {
				const qid = entry.name;
				const qDirPath = join(baseDir, qid);
				try {
					const qFiles = await readdir(qDirPath, { withFileTypes: true });
					for (const qf of qFiles) {
						if (qf.isFile() && qf.name.endsWith(".md") && qf.name !== "summary.md") {
							const filePath = join(qDirPath, qf.name);
							try {
								const content = await readFile(filePath, "utf8");
								const headerMatch = content.match(/^#\s+Quest:\s*([^\r\n]+)/m);
								const name = headerMatch ? headerMatch[1].trim() : (qf.name === "quest.md" ? qid : qf.name.replace(/\.md$/, ""));
								const parentMatch = content.match(/## Parent Quest\s*(?:>.*?\r?\n)?\[\[([^\]]+)\]\]/i);
								const parent = parentMatch ? parentMatch[1].trim() : null;
								const s = await stat(filePath);
								records.push({
									qid,
									name,
									path: filePath,
									dir: qDirPath,
									logPath: join(qDirPath, "execution.log"),
									parent,
									mtime: s.mtimeMs,
								});
							} catch {}
						}
					}
				} catch {}
			}
		}
	} catch {}
	return records.sort((a, b) => (b.mtime || 0) - (a.mtime || 0));
}

export async function resolveQuestRecordBySlug(slug: string, baseDir: string = QUEST_CURRENT_DIR): Promise<QuestRecord | null> {
	if (!slug) return null;
	const records = await listActiveQuestRecords(baseDir);
	const target = slug.trim().toLowerCase();
	const targetSlug = slugify(slug);
	const exact = records.find(
		(r) =>
			r.name.toLowerCase() === target ||
			r.qid.toLowerCase() === target ||
			slugify(r.name) === targetSlug,
	);
	if (exact) return exact;

	for (const r of records) {
		if (r.path.endsWith("quest.md")) {
			try {
				const content = await readFile(r.path, "utf8");
				const subQuestsMatch = content.match(/##\s+Sub-Quests\s*([\s\S]*?)(?=\n##\s+|$)/i);
				if (subQuestsMatch && subQuestsMatch[1].toLowerCase().includes(`[[${target}]]`)) {
					const childPath = join(r.dir, `${slugify(slug)}.md`);
					return {
						qid: r.qid,
						name: slug.trim(),
						path: childPath,
						dir: r.dir,
						logPath: r.logPath,
						parent: r.name,
					};
				}
			} catch {}
		}
	}
	return null;
}

export async function listQuestFiles(dir: string = QUEST_CURRENT_DIR): Promise<string[]> {
	const files = new Set<string>();
	try {
		const entries = await readdir(dir, { withFileTypes: true });
		for (const e of entries) {
			if (e.isFile() && e.name.endsWith(".md")) {
				files.add(e.name);
			} else if (e.isDirectory()) {
				const qPath = join(dir, e.name, "quest.md");
				if (await fileExists(qPath)) {
					files.add(e.name);
				}
			}
		}
	} catch {}
	return Array.from(files).sort();
}

export function futureDraftPath(slug: string): string {
	return `${FUTURE_DIR}/${slug}.md`;
}

export async function appendToFutureDraft(slug: string, promptText: string): Promise<boolean> {
	try {
		const path = futureDraftPath(slug);
		if (!(await fileExists(path))) return false;
		let content = await readFile(path, "utf8");
		const trimmed = promptText.trim().slice(0, 4000);
		if (!trimmed) return false;
		if (content.includes(trimmed.slice(0, 80))) return false;
		// Append to ## Requirements section
		if (content.includes("## Requirements")) {
			content = content.replace(
				/(## Requirements[^\n]*\n)([\s\S]*?)(\n## |\n$)/,
				(_m: string, header: string, body: string, next: string) => {
					let newBody = body.trimEnd();
					if (!newBody.endsWith("\n")) newBody += "\n";
					// Convert placeholder "- " alone to real content
					if (newBody.trim() === "-" || newBody.trim() === "- ") newBody = "";
					else if (newBody && !newBody.endsWith("\n")) newBody += "\n";
					newBody += `- ${trimmed}\n`;
					return header + newBody + next;
				},
			);
			// Fallback if regex didn't match (body until next section)
			if (!content.includes(trimmed.slice(0, 30))) {
				content = content.replace("## Requirements", `## Requirements\n- ${trimmed}`);
			}
		} else {
			content += `\n## Requirements\n- ${trimmed}\n`;
		}
		const { writeFile } = await import("node:fs/promises");
		await writeFile(path, content, "utf8");
		return true;
	} catch {
		return false;
	}
}

export async function createFutureDraftFromPrompt(slug: string, promptText: string): Promise<string> {
	const { mkdir, writeFile } = await import("node:fs/promises");
	const { FUTURE_QUEST_TEMPLATE } = await import("./markdown.ts");
	await mkdir(FUTURE_DIR, { recursive: true });
	const path = futureDraftPath(slug);
	if (await fileExists(path)) {
		await appendToFutureDraft(slug, promptText);
		return path;
	}
	await writeFile(path, FUTURE_QUEST_TEMPLATE(slug, promptText), "utf8");
	return path;
}

export async function cleanDraftIfExists(slug: string, ctx?: ExtensionContext) {
	try {
		const futurePath = futureDraftPath(slug);
		if (await fileExists(futurePath)) {
			await unlink(futurePath);
		}
	} catch (err) {
		// Non-fatal draft cleanup error
	}
}
