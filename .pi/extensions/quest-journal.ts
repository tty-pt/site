/**
 * Quest Journal & Context Awareness — unified extension for quest persistence,
 * session awareness, compaction gates, and workflow enforcement.
 *
 * Concepts
 * --------
 * - Each active quest has a markdown file at `docs/current/<quest>.md`.
 * - The active quest is tracked per-session via a `quest_journal` custom entry.
 * - Session awareness (timestamp, cwd, git branch, active quest freshness,
 *   project guidelines, standing notes) is auto-injected into the system prompt.
 * - Quest files are written by the model itself using its normal tools, so the
 *   content is naturally shaped for resuming. This extension only *prompts*,
 *   *enforces*, and *detects staleness* — it never fabricates plan content.
 * - `saveCount` / `compactCount` are persisted counters. Compaction is allowed
 *   only when there has been at least one quest-file save since the last
 *   compaction, guaranteeing the file is always up to date before context is
 *   lost, while letting compaction run once a fresh save exists (delaying it as
 *   late as the save allows).
 * - Original user prompts are captured verbatim (truncated) and injected into
 *   every save request so the `## Original request` section stays faithful.
 *
 * Commands
 * --------
 *   /quest <name>      – set active quest (creates docs/current/<name>.md if missing)
 *   /quest-save        – persist current state now
 *   /quest-refine      – add mid-workflow or post-implementation requirements
 *   /quest-del [name]  – archive (rename to docs/archive/) the current/named quest
 *   /quest-draft <name>– draft a future quest in docs/future/
 *   /quest-status      – show active quest and staleness
 *   /quests            – list docs/current/*.md and docs/future/*.md
 *
 * Auto-behaviour
 * --------------
 *   - `before_agent_start`: injects session awareness + active quest context +
 *     TDD & workflow rules; captures verbatim user prompt; and, if a save is
 *     pending and we haven't asked in a while, reminds the model to refresh the file.
 *   - `turn_end`: after each completed turn, ask the model to update the file;
 *     at >=85% context also instructs save + /compact (auto-compact fires at
 *     contextTokens > contextWindow - reserveTokens).
 *   - `session_before_compact`: block unless the file was saved since the last
 *     compaction — guarantees persistence before context is lost. Save request
 *     explicitly requires ## Original request to be faithful.
 *   - `session_compact`: record that a compaction happened (next save gate).
 *   - `tool_result` (edit/write on the active file): counts as a save.
 *   - `session_before_switch`: remind to persist before leaving so a parallel
 *     quest is also captured.
 */

import { readFileSync } from "node:fs";
import { mkdir, readFile, readdir, rename, unlink, writeFile } from "node:fs/promises";
import { basename } from "node:path";
import { Text } from "@earendil-works/pi-tui";
import type { ExtensionAPI, ExtensionContext } from "@earendil-works/pi-coding-agent";

const QUEST_DIR = "docs/current";
const NOTES_FILE = ".pi/context.md";
const CUSTOM_TYPE = "quest_journal";
const LEGACY_CUSTOM_TYPE = "task_journal";
const MIN_PROMPT_MS = 45_000; // never nag more often than this
const SAVE_PERCENT = 70; // context-usage % that escalates the reminder
const COMPACT_WARN_PERCENT = 85; // instruct save + compact when this close to auto-compact
const PROMPT_MAX_CHARS = 4000;
const PROMPT_MAX_COUNT = 10;

interface StoredState {
	active: string | null;
	saveCount: number;
	compactCount: number;
	prompts: string[];
}

let state: StoredState = { active: null, saveCount: 0, compactCount: 0, prompts: [] };
let lastPromptAt = Date.now();
let pickerCancelledThisSession = false;

// ---------------------------------------------------------------------------
// State persistence (custom entries survive reloads and branching)
// ---------------------------------------------------------------------------

function persist(pi: ExtensionAPI) {
	try {
		pi.appendEntry<StoredState>(CUSTOM_TYPE, state);
	} catch {
		// ephemeral / unsupported session: stay in-memory only
	}
}

/** Rebuild `state` from the latest `quest_journal` (or legacy `task_journal`) entry in the active branch. */
function reconstruct(ctx: ExtensionContext) {
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && (entry.customType === CUSTOM_TYPE || entry.customType === LEGACY_CUSTOM_TYPE) && entry.data) {
			latest = entry.data as unknown as StoredState;
		}
	}
	state = latest && latest.active
		? { active: latest.active, saveCount: latest.saveCount || 0, compactCount: latest.compactCount || 0, prompts: Array.isArray(latest.prompts) ? latest.prompts : [] }
		: { active: null, saveCount: 0, compactCount: 0, prompts: [] };
	lastPromptAt = Date.now();
}

// ---------------------------------------------------------------------------
// Helpers & Environment Inspection
// ---------------------------------------------------------------------------

function slugify(name: string): string {
	if (!name || typeof name !== "string") return "";
	return (
		name
			.trim()
			.toLowerCase()
			.replace(/[^a-z0-9.\-_]+/g, "-")
			.replace(/-{2,}/g, "-")
			.replace(/^-+|-+$/g, "")
	);
}

const questPath = (slug: string | null) => (slug ? `${QUEST_DIR}/${slug}.md` : "");

async function fileExists(p: string): Promise<boolean> {
	try {
		await readFile(p);
		return true;
	} catch {
		return false;
	}
}

/** Clean up matching proposal draft in docs/future/ if it exists. */
async function cleanDraftIfExists(slug: string) {
	const futurePath = `docs/future/${slug}.md`;
	if (await fileExists(futurePath)) {
		try {
			await unlink(futurePath);
		} catch {
			// ignore cleanup errors
		}
	}
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

/** Context usage as a number 0..100, 0 if unknown. */
function usagePercent(ctx: ExtensionContext): number {
	const u = ctx.getContextUsage();
	if (u && typeof u.percent === "number" && Number.isFinite(u.percent)) return u.percent;
	return 0;
}

function withinCooldown(): boolean {
	return Date.now() - lastPromptAt < MIN_PROMPT_MS;
}

/** A fresh save of the quest file — increments the save gate so compaction may run. */
function markSaved(pi: ExtensionAPI) {
	state.saveCount += 1;
	lastPromptAt = Date.now();
	persist(pi);
}

/** True when compaction should be allowed: at least one save since the last compaction. */
function compactionReady(): boolean {
	return state.saveCount > state.compactCount;
}

function shouldCapturePrompt(text: string): boolean {
	const t = text.trim();
	if (!t) return false;
	if (t.startsWith("/quest") || t.startsWith("/task")) return false;
	if (t.startsWith("Quest-journal:") || t.startsWith("Task-journal:")) return false;
	if (t.length < 2) return false;
	return true;
}

function promptsBlock(): string {
	if (!state.prompts || state.prompts.length === 0) return "(none captured yet — this is the first substantive request; use the current user message)";
	return state.prompts.map((p, i) => `${i + 1}. ${p}`).join("\n\n---\n\n");
}

/** Queue a user message asking the model to update the quest file. */
function sendSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request(s) for this quest — keep VERBATIM (or very faithful if truncated) under ## Original request in the quest file. This section MUST be present and faithful; do not summarize away details:\n${promptsBlock()}`;
	pi.sendUserMessage([
		{
			type: "text",
			text: `${message}\n\n${promptReminder}\n\nActive quest file: \`${questPath(state.active)}\`\n\nFinish current work, then update that file with the latest state (goal, progress, decisions, files touched, findings, TDD & Quality checklist, remaining work, next step). The file MUST contain a ## Original request section with the verbatim/faithful user request(s) above. Ensure TDD (tests written first), build/run verification, clean code (no debug artifacts), and full test suite passing are checked off. Make it complete enough to resume without re-research, then reply with a one-line confirmation.`,
		},
	]);
}

function buildSessionAwarenessBlock(ctx: ExtensionContext): string {
	const lines: string[] = [
		"# Session awareness (auto-injected)",
		"",
		`- Now: ${new Date().toISOString()}`,
		`- cwd: ${ctx.cwd}`,
	];
	const branch = gitBranch();
	if (branch) lines.push(`- Git branch: ${branch}`);

	if (state.active) {
		const fresh = compactionReady();
		lines.push(
			`- Active quest: \`docs/current/${state.active}.md\` (${fresh ? "fresh" : "SAVE PENDING — update it before compaction"}); manage with /quest, /quest-save, /quests.`,
		);
	} else {
		lines.push("- Active quest: none (use /quest <name> before starting real work).");
	}

	const guidelines = projectGuidelines();
	if (guidelines) {
		lines.push("", guidelines);
	}

	const notes = standingNotes();
	if (notes) lines.push("", `## Standing project notes (\`${NOTES_FILE}\`)`, "", notes);

	return lines.join("\n");
}

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------

function installPromptCapture(pi: ExtensionAPI) {
	pi.on("before_agent_start", async (event) => {
		if (!state.active) return;
		const raw = (event as { prompt?: unknown }).prompt;
		if (typeof raw !== "string" || !shouldCapturePrompt(raw)) return;
		const trimmed = raw.trim().slice(0, PROMPT_MAX_CHARS);
		if (state.prompts.length > 0 && state.prompts[state.prompts.length - 1] === trimmed) return;
		state.prompts.push(trimmed);
		if (state.prompts.length > PROMPT_MAX_COUNT) state.prompts = state.prompts.slice(-PROMPT_MAX_COUNT);
		persist(pi);
	});
}

function installBeforeAgentStart(pi: ExtensionAPI) {
	pi.on("before_agent_start", async (_event, _ctx) => {
		if (!state.active) return;
		if (pickerCancelledThisSession) return; // user cancelled the boot picker — stay quiet until /quest
		if (compactionReady()) return; // file already freshly saved
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Quest-journal: before starting, refresh the active quest file with the latest state.");
		lastPromptAt = Date.now();
	});
}

function installTurnEnd(pi: ExtensionAPI) {
	pi.on("turn_end", async (_event, ctx) => {
		if (pickerCancelledThisSession) return; // user cancelled at boot — no routine save prompts
		if (!state.active) return;
		const pct = usagePercent(ctx);
		if (pct >= COMPACT_WARN_PERCENT) {
			sendSaveRequest(pi, `Quest-journal: context at ~${Math.round(pct)}% (near auto-compact: tokens > contextWindow - reserveTokens). Write a thorough state snapshot to the active quest file (must include faithful ## Original request), then run /compact immediately to free context.`);
			lastPromptAt = Date.now();
			return;
		}
		if (pct >= SAVE_PERCENT) {
			sendSaveRequest(pi, "Quest-journal: context is getting large. Write a thorough state snapshot to the active quest file.");
			lastPromptAt = Date.now();
			return;
		}
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Quest-journal: update the active quest file with the current state.");
		lastPromptAt = Date.now();
	});
}

function installBeforeCompact(pi: ExtensionAPI) {
	pi.on("session_before_compact", async (_event, ctx) => {
		if (!state.active) return; // nothing to protect
		if (compactionReady()) return; // file is fresh since last compaction — allow
		ctx.ui.notify(`Quest-journal: blocking compaction until '${questPath(state.active)}' is saved.`, "warning");
		sendSaveRequest(pi, "Quest-journal: save is required before compaction. Write the full quest state to disk now (must include faithful ## Original request), then allow compaction to proceed.");
		return { cancel: true };
	});
}

function installAfterCompact(pi: ExtensionAPI) {
	pi.on("session_compact", async (_event, _ctx) => {
		if (!state.active) return;
		state.compactCount = state.saveCount; // a compaction happened; next save re-arms the gate
		persist(pi);
	});
}

function installBeforeSwitch(pi: ExtensionAPI) {
	pi.on("session_before_switch", async (_event, _ctx) => {
		if (!state.active) return;
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Quest-journal: persisting before we switch sessions.");
	});
}

/** Prompt the user with an interactive selector to choose an existing quest, draft, or create a new quest. */
async function promptForQuestChoice(ctx: ExtensionContext, title = "Select quest:"): Promise<string | null> {
	if (!ctx.hasUI || ctx.mode !== "tui") return null;
	const current = await listQuestFiles(QUEST_DIR);
	const future = await listQuestFiles("docs/future");
	const choices: string[] = [];

	for (const f of current) {
		const name = f.replace(/\.md$/, "");
		choices.push(state.active === name ? `${name} (active)` : name);
	}
	for (const f of future) {
		const name = f.replace(/\.md$/, "");
		if (!current.includes(f)) {
			choices.push(`${name} (draft)`);
		}
	}
	choices.push("New quest…", "Cancel");

	const choice = await ctx.ui.select(title, choices);
	if (!choice || choice === "Cancel") return null;

	if (choice === "New quest…") {
		const inputName = await ctx.ui.input("New quest name (e.g. migrate-ftp):");
		return inputName && slugify(inputName) ? slugify(inputName) : null;
	}

	const clean = choice.replace(/ \(active\)$/, "").replace(/ \(draft\)$/, "");
	return slugify(clean);
}

/**
 * On interactive startup, ask the user what to work on: pick a current quest,
 * promote a draft from docs/future/, start a new quest, or skip.
 * Reuses the /quest command by queueing it as a user message (extension
 * commands are checked first in input processing).
 */
async function offerQuestChoiceOnBoot(pi: ExtensionAPI, ctx: ExtensionContext, reason: string) {
	if (reason !== "startup") return;
	if (!ctx.hasUI || ctx.mode !== "tui") return;
	const choice = await promptForQuestChoice(ctx, "What do you want to work on?");
	if (!choice) {
		pickerCancelledThisSession = true; // user opted out — suppress quest-journal prompts until /quest
		return;
	}
	pi.sendUserMessage(`/quest ${choice}`);
}

/** On quit, ask the model for a final snapshot of the active quest file. */
function installShutdownSave(pi: ExtensionAPI) {
	pi.on("session_shutdown", async (event, _ctx) => {
		if (event.reason !== "quit") return;
		if (!state.active) return;
		sendSaveRequest(pi, "Quest-journal: you are about to quit. Write a final state snapshot to the active quest file now (must include faithful ## Original request).");
	});
}

/** A write/edit to the active quest file counts as a save (lowers the compaction gate). */
function installFileWatch(pi: ExtensionAPI) {
	pi.on("tool_result", async (event) => {
		if (!state.active) return;
		if (event.toolName !== "write" && event.toolName !== "edit") return;
		const p = event.input.path as string | undefined;
		if (typeof p === "string" && normalizePath(p) === questPath(state.active)) {
			markSaved(pi);
		}
	});
}

const normalizePath = (p: string) => p.replace(/^\.\//, "").replace(/\\/g, "/");

/** Helper to archive a quest file and update journal state */
async function archiveQuestFile(name: string, pi: ExtensionAPI): Promise<{ success: boolean; message: string; dest?: string }> {
	const path = questPath(name);
	if (!(await fileExists(path))) {
		return { success: false, message: `No quest file found at ${path}` };
	}
	await mkdir("docs/archive", { recursive: true });
	const dest = `docs/archive/${basename(path).replace(/\.md$/, "")}-${Date.now().toString(36)}.md`;
	await rename(path, dest);
	await cleanDraftIfExists(name);
	if (state.active === name) {
		state.active = null;
		state.prompts = [];
		persist(pi);
	}
	return { success: true, message: `Archived ${path} → ${dest}`, dest };
}

/** Tool allowing the model to explicitly archive the active (or named) quest and trigger auto-compaction. */
function installArchiveTool(pi: ExtensionAPI) {
	const archiveHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		const targetName = slugify(params.questName || params.taskName || state.active || "");
		if (!targetName) {
			return {
				content: [{ type: "text", text: "Error: No active quest to archive and no questName provided." }],
				details: { error: "no_quest" },
			};
		}

		const res = await archiveQuestFile(targetName, pi);
		if (!res.success) {
			return {
				content: [{ type: "text", text: res.message }],
				details: { error: "archive_failed" },
			};
		}

		const shouldCompact = params.compact !== false;
		if (shouldCompact) {
			ctx.compact({
				customInstructions: `Quest '${targetName}' completed and archived. Focus summary on key architecture decisions, completed work, and remaining roadmap.`,
				onComplete: () => {
					if (ctx.hasUI) ctx.ui.notify(`Compacted context following archive of '${targetName}'`, "info");
				},
				onError: (err) => {
					if (ctx.hasUI) ctx.ui.notify(`Post-archive compaction failed: ${err.message}`, "error");
				},
			});
		}

		if (ctx.hasUI) ctx.ui.notify(res.message, "info");
		return {
			content: [
				{
					type: "text",
					text: `${res.message}${shouldCompact ? " Context compaction triggered." : ""}`,
				},
			],
			details: { archived: targetName, dest: res.dest, compacted: shouldCompact },
		};
	};

	pi.registerTool({
		name: "quest_journal_archive",
		label: "Archive Quest Journal",
		description: "Archive the active (or specified) quest from docs/current/ to docs/archive/ and optionally trigger session context compaction.",
		parameters: {
			type: "object",
			properties: {
				questName: {
					type: "string",
					description: "Quest name to archive. Defaults to currently active quest.",
				},
				taskName: {
					type: "string",
					description: "Alias for questName.",
				},
				compact: {
					type: "boolean",
					description: "Whether to immediately trigger session context compaction after archiving (defaults to true).",
				},
			},
			additionalProperties: false,
		},
		execute: archiveHandler,
	});

	// Register task_journal_archive alias for backwards compatibility
	pi.registerTool({
		name: "task_journal_archive",
		label: "Archive Task Journal (legacy alias for quest_journal_archive)",
		description: "Archive the active (or specified) quest from docs/current/ to docs/archive/ and optionally trigger session context compaction.",
		parameters: {
			type: "object",
			properties: {
				questName: {
					type: "string",
					description: "Quest name to archive.",
				},
				taskName: {
					type: "string",
					description: "Alias for questName.",
				},
				compact: {
					type: "boolean",
					description: "Whether to immediately trigger session context compaction after archiving.",
				},
			},
			additionalProperties: false,
		},
		execute: archiveHandler,
	});
}

function installMarkTool(pi: ExtensionAPI) {
	const markHandler = async () => {
		markSaved(pi);
		return { content: [{ type: "text", text: "Quest file marked as saved in the journal." }], details: {} };
	};

	pi.registerTool({
		name: "quest_journal_mark_saved",
		label: "Mark Quest Saved",
		description: "Record that the active quest file has been written to disk. Call after updating the quest file.",
		parameters: { type: "object", properties: {}, additionalProperties: false },
		execute: markHandler,
	});

	// Register task_journal_mark_saved alias for backwards compatibility
	pi.registerTool({
		name: "task_journal_mark_saved",
		label: "Mark Task Saved (legacy alias for quest_journal_mark_saved)",
		description: "Record that the active quest file has been written to disk. Call after updating the quest file.",
		parameters: { type: "object", properties: {}, additionalProperties: false },
		execute: markHandler,
	});
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

async function listQuestFiles(dir = QUEST_DIR): Promise<string[]> {
	try {
		return (await readdir(dir)).filter((f) => f.endsWith(".md")).sort();
	} catch {
		return [];
	}
}

function installCommands(pi: ExtensionAPI) {
	const questHandler = async (args: string, ctx: ExtensionContext) => {
		let name = slugify(args);
		if (!name) {
			name = (await promptForQuestChoice(ctx, "Which quest do you want to work on?")) ?? "";
		}
		if (!name) {
			ctx.ui.notify("No quest selected.", "warning");
			return;
		}
		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(name);
		const futurePath = `docs/future/${name}.md`;
		
		let goal = "";
		if (!(await fileExists(path))) {
			if (await fileExists(futurePath)) {
				// Promote future quest to current
				await rename(futurePath, path);
				if (ctx.hasUI) ctx.ui.notify(`Promoted ${futurePath} → ${path}`, "info");
			} else {
				if (ctx.hasUI && ctx.mode === "tui") {
					goal = ((await ctx.ui.input(`Describe the goal for quest '${name}':`)) ?? "").trim();
				}
				await writeFile(path, QUEST_TEMPLATE(name, goal), "utf8");
			}
		}
		// Clean up any remaining draft file in docs/future/ so no duplicate stays behind
		await cleanDraftIfExists(name);
		// Switching quests: new quest starts with fresh prompt history.
		const switching = state.active !== name;
		pickerCancelledThisSession = false; // explicit /quest re-enables journal prompts
		state.active = name;
		if (switching) state.prompts = [];
		if (goal) {
			state.prompts.push(`Goal: ${goal}`);
		}
		state.saveCount += 1;
		persist(pi);
		// Initial turn: ask the model to fill the file before doing real work.
		const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
		pi.sendUserMessage([
			{
				type: "text",
				text: `Now working on quest **${name}**. Quest file: \`${path}\`.${goalText}

**Mandatory Upfront Research & Planning Protocol**:
1. First, discover how to build, run, and test the project (e.g. read AGENTS.md, Makefile, scripts).
2. Perform an in-depth codebase investigation: inspect relevant libraries, module boundaries, data flows, and root causes of complexity. Think ambitiously about clean abstractions.
3. Formulate a comprehensive, multi-stage execution plan where each phase is self-contained with exact function signatures, touched files, and targeted test files.
4. Fill \`${path}\` completely with the goal, decisions, analysis findings, multi-stage plan, acceptance checklist, and next recommended step.
5. In your very first turn, present the complete analysis, architectural trade-offs, and multi-stage plan clearly to the user for confirmation.

**Mandatory TDD & Quality Workflow**:
1. Develop targeted test(s) for each stage BEFORE feature code.
2. Develop feature -> build -> run -> verify targeted tests.
3. Support end-of-task user feedback loops and polish iterations until final confirmation.
4. Final Quality Gates: zero build errors/warnings, zero debug artifacts, and full test suite passing with zero errors.`,
			},
		]);
	};

	const questCompletions = async (prefix: string) => {
		const current = await listQuestFiles(QUEST_DIR);
		const future = await listQuestFiles("docs/future");
		const names = [...new Set([...current, ...future])].map((f) => f.replace(/\.md$/, ""));
		const filtered = names.filter((n) => n.startsWith(prefix));
		return filtered.length ? filtered.map((value) => ({ value, label: value })) : null;
	};

	pi.registerCommand("quest", {
		description: "Set the active quest (e.g. /quest cx). Promotes from docs/future/ if it exists, or creates docs/current/<name>.md.",
		getArgumentCompletions: questCompletions,
		handler: questHandler,
	});

	pi.registerCommand("task", {
		description: "Alias for /quest.",
		getArgumentCompletions: questCompletions,
		handler: questHandler,
	});

	const questSaveHandler = async (_args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			ctx.ui.notify("No active quest — use /quest <name> first.", "warning");
			return;
		}
		sendSaveRequest(pi, "Quest-journal: /quest-save — write a full state snapshot to the active quest file now.");
		lastPromptAt = Date.now();
	};

	pi.registerCommand("quest-save", {
		description: "Persist the active quest file now.",
		handler: questSaveHandler,
	});

	pi.registerCommand("task-save", {
		description: "Alias for /quest-save.",
		handler: questSaveHandler,
	});

	const questRefineHandler = async (args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			ctx.ui.notify("No active quest — use /quest <name> first.", "warning");
			return;
		}
		let refinement = args.trim();
		if (!refinement && ctx.mode === "tui") {
			refinement = (await ctx.ui.input("Enter quest refinement or new requirements:")) ?? "";
		}
		if (!refinement) {
			ctx.ui.notify("Usage: /quest-refine <instructions...>", "warning");
			return;
		}
		const entry = `Refinement: ${refinement}`;
		state.prompts.push(entry);
		persist(pi);

		sendSaveRequest(
			pi,
			`Quest-journal: /quest-refine — User quest refinement received:\n"${refinement}"\n\nUpdate \`docs/current/${state.active}.md\` now: expand ## Goal if needed, add entry under ## Quest Refinements & User Feedback Loops, update ## Remaining work and ## TDD & Quality Checklist, and record any new decisions.`
		);
		lastPromptAt = Date.now();
		if (ctx.hasUI) ctx.ui.notify(`Refinement queued for active quest '${state.active}'`, "info");
	};

	pi.registerCommand("quest-refine", {
		description: "Refine the active quest mid-workflow or add post-implementation requirements (e.g. /quest-refine Add edge case handling).",
		handler: questRefineHandler,
	});

	pi.registerCommand("task-refine", {
		description: "Alias for /quest-refine.",
		handler: questRefineHandler,
	});

	const questDelHandler = async (args: string, ctx: ExtensionContext) => {
		let name = slugify(args);
		if (!name) {
			name = (await promptForQuestChoice(ctx, "Select quest to archive:")) ?? "";
		}
		if (!name) {
			ctx.ui.notify("No quest selected for archiving.", "warning");
			return;
		}
		const res = await archiveQuestFile(name, pi);
		if (!res.success) {
			ctx.ui.notify(res.message, "warning");
			return;
		}
		if (ctx.hasUI) ctx.ui.notify(res.message, "info");
	};

	pi.registerCommand("quest-del", {
		description: "Archive (rename to docs/archive/) the current or named quest file.",
		handler: questDelHandler,
	});

	pi.registerCommand("task-del", {
		description: "Alias for /quest-del.",
		handler: questDelHandler,
	});

	const questDraftHandler = async (args: string, ctx: ExtensionContext) => {
		let name = slugify(args);
		if (!name && ctx.mode === "tui") {
			name = slugify((await ctx.ui.input("Future quest name (e.g. cx-ergonomics):")) ?? "");
		}
		if (!name) {
			ctx.ui.notify("Usage: /quest-draft <name>", "warning");
			return;
		}
		const currentPath = questPath(name);
		if (await fileExists(currentPath)) {
			ctx.ui.notify(`Quest '${name}' is already active/current in ${currentPath}. Cannot create a draft for an active quest.`, "warning");
			return;
		}
		await mkdir("docs/future", { recursive: true });
		const path = `docs/future/${name}.md`;
		if (!(await fileExists(path))) {
			await writeFile(path, FUTURE_QUEST_TEMPLATE(name), "utf8");
			if (ctx.hasUI) ctx.ui.notify(`Created draft proposal at ${path}`, "info");
		} else {
			if (ctx.hasUI) ctx.ui.notify(`Draft already exists at ${path}`, "warning");
		}
	};

	pi.registerCommand("quest-draft", {
		description: "Draft a future quest or proposal without making it active.",
		handler: questDraftHandler,
	});

	pi.registerCommand("task-draft", {
		description: "Alias for /quest-draft.",
		handler: questDraftHandler,
	});

	const questStatusHandler = async (_args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			if (ctx.hasUI) ctx.ui.notify("No active quest.", "info");
			return;
		}
		const path = questPath(state.active);
		const exists = await fileExists(path);
		const fresh = compactionReady();
		const pct = usagePercent(ctx);
		const line = exists
			? `${path} — ${fresh ? "fresh" : "SAVE PENDING"}, context ~${Math.round(pct)}%, prompts ${state.prompts.length}`
			: `${path} — MISSING on disk!`;
		if (ctx.hasUI) ctx.ui.notify(`Active quest: ${line}`, fresh ? "info" : "warning");
	};

	pi.registerCommand("quest-status", {
		description: "Show the active quest and whether its file is fresh.",
		handler: questStatusHandler,
	});

	pi.registerCommand("task-status", {
		description: "Alias for /quest-status.",
		handler: questStatusHandler,
	});

	const questsHandler = async (_args: string, ctx: ExtensionContext) => {
		const current = await listQuestFiles(QUEST_DIR);
		const future = await listQuestFiles("docs/future");
		const rows = current.length
			? current.map((f) => `  ${f.replace(/\.md$/, "")}${state.active === f.replace(/\.md$/, "") ? "  ◀ active" : ""}`)
			: ["  (none — use /quest <name>)"];
		const futureRows = future.length
			? future.map((f) => `  ${f.replace(/\.md$/, "")}`)
			: ["  (none — use /quest-draft <name>)"];
		
		ctx.ui.setWidget("quest-journal", [
			`Active: ${state.active ? questPath(state.active) : "(none)"}`, 
			"",
			"Current quests:",
			...rows,
			"",
			"Future / Backlog quests:",
			...futureRows
		]);
	};

	pi.registerCommand("quests", {
		description: "List current and future quests.",
		handler: questsHandler,
	});

	pi.registerCommand("tasks", {
		description: "Alias for /quests.",
		handler: questsHandler,
	});
}

function FUTURE_QUEST_TEMPLATE(name: string): string {
	return [
		`# Proposal / Future Quest: ${name}`,
		``,
		`Status: **proposal**`,
		``,
		`## Goals & Scope`,
		`> What are we proposing to change and why?`,
		``,
		`## Requirements`,
		`- `,
		``,
		`## Implementation Plan`,
		`1. `,
		``,
		`## Out of scope`,
		`- `,
		``
	].join("\n");
}

function QUEST_TEMPLATE(name: string, goal = ""): string {
	return [
		`# Quest: ${name}`,
		``,
		`## Goal`,
		goal ? goal : `> What we are trying to accomplish.`,
		``,
		`## Original request`,
		goal ? `> Goal: ${goal}` : `> Paste the verbatim user prompt here (or very faithful summary if truncated). This section MUST stay faithful — it is enforced by the extension.`,
		`>`,
		``,
		`## Current Status`,
		`- [ ] not started · in progress · blocked · done`,
		``,
		`## Build & Run Commands`,
		`> Commands to build, run, and test the project (discovered BEFORE modifying feature code).`,
		`- Build: `,
		`- Run: `,
		`- Test: `,
		``,
		`## TDD & Quality Checklist`,
		`- [ ] **1. Discovery**: Discovered how to build and run the project.`,
		`- [ ] **2. Write Tests First**: Developed test(s) for the quest BEFORE feature code.`,
		`- [ ] **3. Feature Implementation**: Developed feature to satisfy tests.`,
		`- [ ] **4. Build & Run**: Built and ran project with zero build errors. Restart server/process to verify clean boot.`,
		`- [ ] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.`,
		`- [ ] **6. Server Restart & Full Test Suite**: Restarted fresh server instance (e.g. \`make restart\` / kill and restart daemon) and executed FULL test suite with zero errors.`,
		``,
		`## In-Depth Analysis & Findings`,
		`> Root cause analysis, architectural friction, abstraction opportunities.`,
		`- `,
		``,
		`## Detailed Multi-Stage Execution Plan`,
		`> Each stage must be self-contained as if it were a single quest, with exact signatures, touched files, and targeted tests.`,
		`### Stage 1: `,
		`- **Target**: `,
		`- **Tasks**: `,
		`- **Targeted Tests**: `,
		``,
		`## Acceptance Criteria & Polish Checklist`,
		`- [ ] `,
		``,
		`## Quest Refinements & User Feedback Loops`,
		`> Mid-workflow refinements, post-implementation iterations, and user adjustments.`,
		`- `,
		``,
		`## Why this matters`,
		`> Context, motivation, stakeholders.`,
		``,
		`## Decisions made`,
		`- `,
		``,
		`## Constraints & Rules`,
		`- `,
		``,
		`## Files touched`,
		`- `,
		``,
		`## Remaining work`,
		`- [ ] `,
		``,
		`## Open questions / risks`,
		`- `,
		``,
		`## Next recommended step`,
		`1. `,
		``,
		`## Resume prompt`,
		`> A paragraph an agent should read before resuming.`,
		``,
	].join("\n");
}

// ---------------------------------------------------------------------------

async function loadActiveQuestResumeContext(): Promise<string> {
	if (!state.active) return "";
	const path = questPath(state.active);
	try {
		const content = await readFile(path, "utf8");
		if (!content) return "";
		
		// Extract key sections: Goal, Current Status, Remaining work, Next recommended step, Resume prompt
		const sections = ["Goal", "Current Status", "Remaining work", "Next recommended step", "Resume prompt"];
		const extracted: string[] = [];
		for (const sec of sections) {
			const regex = new RegExp(`## ${sec}\\n([\\s\\S]*?)(?=\\n## |$)`, "i");
			const match = content.match(regex);
			if (match && match[1].trim()) {
				extracted.push(`### ${sec}\n${match[1].trim()}`);
			}
		}
		if (extracted.length === 0) return "";
		return `\n\n# Active Quest Resume Context (from \`${path}\`)\n${extracted.join("\n\n")}`;
	} catch {
		return "";
	}
}

function installWorkflowSystemPrompt(pi: ExtensionAPI) {
	pi.on("before_agent_start", async (event, ctx) => {
		try {
			const awarenessBlock = buildSessionAwarenessBlock(ctx);
			const resumeContext = await loadActiveQuestResumeContext();
			const workflowInstructions = `\n\n# Mandatory Quest Workflow Rules (TDD & Quality Gates)
When working on quests:
1. **Upfront Deep Research & Planning**:
   - Before writing feature code, conduct a thorough architectural audit of the relevant codebase, understand constraints, and design an ambitious, multi-stage plan where each phase is self-contained.
   - Present findings, trade-offs, and the plan in turn 1 for user confirmation.
2. **Build & Run Discovery**: Discovered how to build and run the project before editing code.
3. **Develop Tests First (TDD)**: Develop targeted test(s) BEFORE developing each feature phase.
4. **Iterative Build, Run & Test**: Feature implementation -> build -> run -> verify targeted tests.
5. **Post-Implementation & User Feedback Loops**:
   - Expect and support user polish iterations at the end of a quest.
   - When the user provides feedback or refinements, log them under \`## Quest Refinements & User Feedback Loops\`, update acceptance checklists, execute the changes, and verify with tests until the user confirms satisfaction.
6. **Quest Completion & Wrap-Up Flow**:
   - When a quest is completed (all features, acceptance criteria, and quality gates pass), ALWAYS prompt the user via \`ask_questions\` with the following structured options:
     1. **Refine anything**: Keep active quest open to make further adjustments or address feedback.
     2. **Archive quest and auto-compact**: Call \`quest_journal_archive({ compact: true })\` to archive the quest and automatically compact session context.
     3. **Archive quest without auto-compact**: Call \`quest_journal_archive({ compact: false })\` to archive the quest while keeping the current session context intact.
     4. **Change to manual mode**: Switch workflow / compaction to manual mode.
   - Execute the exact action corresponding to the user's choice.
7. **Final Verification & Quality Gates**:
   - Zero compiler errors or warnings.
   - Zero debug artifacts (no leftover console.logs, prints, or scratch code).
   - **Server / Daemon Restart**: Always ensure a fresh instance of the server / test daemon is running (e.g. restart background servers or run clean boot test) before running final tests, so tests never run against stale in-memory state.
   - Full test suite (\`make test\`) must pass with zero errors.

# Quest Management & Verbal Requests
You maintain long-lived quest state on disk in \`docs/current/<quest>.md\`.
When users ask in natural language to manage quests (verbally refining, switching, drafting, archiving, or listing), apply these rules directly:

1. **Refine Active Quest / User Feedback** (e.g. "refine quest", "add requirement X", "tweak Y", "feedback: Z"):
   - Read and update \`docs/current/<active-quest>.md\` immediately using \`edit\` or \`write\`.
   - Record the user feedback under \`## Quest Refinements & User Feedback Loops\`.
   - Update \`## Remaining work\`, \`## Acceptance Criteria & Polish Checklist\`, \`## Decisions made\`, and \`## Next recommended step\`.
   - Call \`quest_journal_mark_saved\` after updating to keep journal staleness in sync.

2. **Start / Switch Quest** (e.g. "switch to quest X", "start quest Y"):
   - Maintain/create \`docs/current/<quest>.md\`. If a proposal exists at \`docs/future/<quest>.md\`, promote it by moving it to \`docs/current/<quest>.md\`.
   - Fill initial Goal, Current Status, and TDD checklist before feature work.

3. **Draft Future Quest** (e.g. "draft quest X", "propose quest Y for later"):
   - Create \`docs/future/<quest>.md\` using proposal format (Goals & Scope, Requirements, Implementation Plan). Do not make it active.

4. **Archive Quest** (e.g. "archive quest X", "finish quest Y"):
   - Use tool \`quest_journal_archive({ questName: "...", compact: boolean })\` or move \`docs/current/<quest>.md\` to \`docs/archive/<quest>-<timestamp>.md\` and remove any matching draft in \`docs/future/\`.

5. **List / Status** (e.g. "what are my quests?", "show quest status"):
   - Inspect files in \`docs/current/\` and \`docs/future/\` to report quest status.${resumeContext}`;

			return { systemPrompt: `${event.systemPrompt}\n\n${awarenessBlock}${workflowInstructions}` };
		} catch {
			return; // never break turn on prompt injection failure
		}
	});
}

function registerQuestJournalCRBHook() {
	if (typeof globalThis !== "undefined") {
		const g = globalThis as any;
		if (!g.__pi_crb_providers) {
			g.__pi_crb_providers = [];
		}
		g.__pi_crb_providers.push((_ctx: ExtensionContext, tools: string[]) => {
			const set = new Set(tools.map((t) => t.toLowerCase()));
			if (set.has("quest_journal_mark_saved") || set.has("task_journal_mark_saved") || state.active) {
				return [
					"Call `quest_journal_mark_saved` after updating active quest files.",
					"When completing a quest, prompt via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
				];
			}
			return [];
		});
	}
}

export default function (pi: ExtensionAPI) {
	registerQuestJournalCRBHook();
	pi.on("session_start", async (event, ctx) => {
		reconstruct(ctx);
		await offerQuestChoiceOnBoot(pi, ctx, event.reason);
	});
	pi.on("session_tree", async (_event, ctx) => reconstruct(ctx));

	installWorkflowSystemPrompt(pi);
	installPromptCapture(pi);
	installBeforeAgentStart(pi);
	installTurnEnd(pi);
	installBeforeCompact(pi);
	installAfterCompact(pi);
	installBeforeSwitch(pi);
	installFileWatch(pi);
	installMarkTool(pi);
	installArchiveTool(pi);
	installCommands(pi);
	installShutdownSave(pi);

	// Durable in-session marker, not sent to the LLM.
	const renderEntry = (entry: any, _o: any, theme: any) => {
		const data = entry.data ?? ({} as StoredState);
		const fresh = data.saveCount > data.compactCount;
		return new Text(
			`${theme.fg("accent", "✨ ")}${theme.fg("muted", "quest:")} ${data.active ?? "(none)"}${
				fresh ? "" : theme.fg("warning", " (save pending)")
			}`,
			0,
			0,
		);
	};
	pi.registerEntryRenderer<StoredState>(CUSTOM_TYPE, renderEntry);
	pi.registerEntryRenderer<StoredState>(LEGACY_CUSTOM_TYPE, renderEntry);
}
