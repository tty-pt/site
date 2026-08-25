/**
 * Context-awareness — keep the model durably aware of environment/task facts.
 *
 * A model only knows what is inside its request context. This extension
 * appends a compact "Session awareness" block to the system prompt each turn:
 *
 *   - current date/time and cwd
 *   - git branch (read straight from .git/HEAD, no subprocess)
 *   - the active Task Journal task (read from task-journal.ts session entries)
 *     and whether its file is fresh or has a pending save
 *   - static project-specific notes from `.pi/context.md` (create/edit freely;
 *     missing file is fine)
 *
 * Purely additive: it never sends messages, never blocks, never mutates state,
 * and never throws into the agent loop.
 */

import { readFileSync } from "node:fs";
import type { ExtensionAPI, ExtensionContext } from "@earendil-works/pi-coding-agent";

const NOTES_FILE = ".pi/context.md";
const TASK_TYPE = "task_journal";

interface TaskJournalData {
	active?: string | null;
	saveCount?: number;
	compactCount?: number;
}

/** Reconstruct the latest task_journal state from session entries. */
function latestTaskState(ctx: ExtensionContext): TaskJournalData | null {
	let latest: TaskJournalData | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		const e = entry as unknown as { type?: string; customType?: string; data?: TaskJournalData };
		if (e.type === "custom" && e.customType === TASK_TYPE && e.data) latest = e.data;
	}
	return latest?.active ? latest : null;
}

function gitBranch(): string | null {
	try {
		const head = readFileSync(".git/HEAD", "utf8").trim();
		const m = head.match(/^ref:\s*refs\/heads\/(.+)$/);
		return m ? m[1] : head.slice(0, 40);
	} catch {
		return null;
	}
}

function standingNotes(): string | null {
	try {
		const t = readFileSync(NOTES_FILE, "utf8").trim();
		return t || null;
	} catch {
		return null;
	}
}

/** Auto-discover project guidelines from AGENTS.md, CLAUDE.md, or SYSTEM.md */
function projectGuidelines(): string | null {
	const candidates = ["AGENTS.md", "CLAUDE.md", "SYSTEM.md"];
	for (const file of candidates) {
		try {
			const content = readFileSync(file, "utf8").trim();
			if (!content) continue;

			// Look for a Guidelines / Rules / Invariants section
			const match = content.match(/(##\s+(?:Guidelines|Rules|Invariants|Mandatory Guidelines)[\s\S]*?)(?=\n##\s+|$)/i);
			if (match && match[1].trim()) {
				return `### Project Guidelines (auto-detected from \`${file}\`)\n\n${match[1].trim()}`;
			}

			// Fallback: if file is small (< 2500 chars), return full file content
			if (content.length <= 2500) {
				return `### Project Guidelines (auto-detected from \`${file}\`)\n\n${content}`;
			}
		} catch {
			// file unreadable or missing
		}
	}
	return null;
}

function buildAwarenessBlock(ctx: ExtensionContext): string {
	const lines: string[] = [
		"# Session awareness (auto-injected)",
		"",
		`- Now: ${new Date().toISOString()}`,
		`- cwd: ${ctx.cwd}`,
	];
	const branch = gitBranch();
	if (branch) lines.push(`- Git branch: ${branch}`);

	const task = latestTaskState(ctx);
	if (task?.active) {
		const fresh = (task.saveCount ?? 0) > (task.compactCount ?? 0);
		lines.push(
			`- Active task: \`docs/current/${task.active}.md\` (${fresh ? "fresh" : "SAVE PENDING — update it before compaction"}); manage with /task, /task-save, /tasks.`,
		);
	} else {
		lines.push("- Active task: none (use /task <name> before starting real work).");
	}

	const guidelines = projectGuidelines();
	if (guidelines) {
		lines.push("", guidelines);
	}

	const notes = standingNotes();
	if (notes) lines.push("", `## Standing project notes (\`${NOTES_FILE}\`)`, "", notes);

	return lines.join("\n");
}

export default function (pi: ExtensionAPI) {
	pi.on("before_agent_start", async (event, ctx) => {
		let block: string;
		try {
			block = buildAwarenessBlock(ctx);
		} catch {
			return; // never break a turn over awareness injection
		}
		if (!block.trim()) return;
		return { systemPrompt: `${event.systemPrompt}\n\n${block}` };
	});
}
