/**
 * Task Journal — persist per-task state to docs/current/*.md so work survives
 * across Pi sessions, compaction, and context resets without re-research.
 *
 * Concepts
 * --------
 * - Each active task has a markdown file at `docs/current/<task>.md`.
 * - The active task is tracked per-session via a `task_journal` custom entry.
 * - Task files are written by the model itself using its normal tools, so the
 *   content is naturally shaped for resuming. This extension only *prompts*,
 *   *enforces*, and *detects staleness* — it never fabricates plan content.
 * - `saveCount` / `compactCount` are persisted counters. Compaction is allowed
 *   only when there has been at least one task-file save since the last
 *   compaction, guaranteeing the file is always up to date before context is
 *   lost, while letting compaction run once a fresh save exists (delaying it as
 *   late as the save allows).
 * - Original user prompts are captured verbatim (truncated) and injected into
 *   every save request so the `## Original request` section stays faithful.
 *
 * Commands
 * --------
 *   /task <name>      – set active task (creates docs/current/<name>.md if missing)
 *   /task-save        – persist current state now
 *   /task-del [name]  – archive (rename to docs/future/) the current/named task
 *   /task-status      – show active task and staleness
 *   /tasks            – list docs/current/*.md
 *
 * Auto-behaviour
 * --------------
 *   - `before_agent_start`: capture the verbatim user prompt (for Original request)
 *     and, if a save is pending and we haven't asked in a while, lightly remind
 *     the model to refresh the file at the end of the turn.
 *   - `turn_end`: after each completed turn, ask the model to update the file;
 *     at >=85% context also instructs save + /compact (auto-compact fires at
 *     contextTokens > contextWindow - reserveTokens).
 *   - `session_before_compact`: block unless the file was saved since the last
 *     compaction — guarantees persistence before context is lost. Save request
 *     explicitly requires ## Original request to be faithful.
 *   - `session_compact`: record that a compaction happened (next save gate).
 *   - `tool_result` (edit/write on the active file): counts as a save.
 *   - `session_before_switch`: remind to persist before leaving so a parallel
 *     task is also captured.
 */

import { mkdir, readFile, readdir, rename, unlink, writeFile } from "node:fs/promises";
import { basename } from "node:path";
import { Text } from "@earendil-works/pi-tui";
import type { ExtensionAPI, ExtensionContext } from "@earendil-works/pi-coding-agent";

const TASK_DIR = "docs/current";
const CUSTOM_TYPE = "task_journal";
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

/** Rebuild `state` from the latest `task_journal` entry in the active branch. */
function reconstruct(ctx: ExtensionContext) {
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && entry.customType === CUSTOM_TYPE && entry.data) {
			latest = entry.data as unknown as StoredState;
		}
	}
	state = latest && latest.active
		? { active: latest.active, saveCount: latest.saveCount || 0, compactCount: latest.compactCount || 0, prompts: Array.isArray(latest.prompts) ? latest.prompts : [] }
		: { active: null, saveCount: 0, compactCount: 0, prompts: [] };
	lastPromptAt = Date.now();
}

// ---------------------------------------------------------------------------
// Helpers
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

const taskPath = (slug: string | null) => (slug ? `${TASK_DIR}/${slug}.md` : "");

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

/** Context usage as a number 0..100, 0 if unknown. */
function usagePercent(ctx: ExtensionContext): number {
	const u = ctx.getContextUsage();
	if (u && typeof u.percent === "number" && Number.isFinite(u.percent)) return u.percent;
	return 0;
}

function withinCooldown(): boolean {
	return Date.now() - lastPromptAt < MIN_PROMPT_MS;
}

/** A fresh save of the task file — increments the save gate so compaction may run. */
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
	if (t.startsWith("/task")) return false;
	if (t.startsWith("Task-journal:")) return false;
	if (t.length < 2) return false;
	return true;
}

function promptsBlock(): string {
	if (!state.prompts || state.prompts.length === 0) return "(none captured yet — this is the first substantive request; use the current user message)";
	return state.prompts.map((p, i) => `${i + 1}. ${p}`).join("\n\n---\n\n");
}

/** Queue a user message asking the model to update the task file. */
function sendSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request(s) for this task — keep VERBATIM (or very faithful if truncated) under ## Original request in the task file. This section MUST be present and faithful; do not summarize away details:\n${promptsBlock()}`;
	pi.sendUserMessage([
		{
			type: "text",
			text: `${message}\n\n${promptReminder}\n\nActive task file: \`${taskPath(state.active)}\`\n\nFinish current work, then update that file with the latest state (goal, progress, decisions, files touched, findings, TDD & Quality checklist, remaining work, next step). The file MUST contain a ## Original request section with the verbatim/faithful user request(s) above. Ensure TDD (tests written first), build/run verification, clean code (no debug artifacts), and full test suite passing are checked off. Make it complete enough to resume without re-research, then reply with a one-line confirmation.`,
		},
	]);
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
	pi.on("before_agent_start", async (_event, ctx) => {
		if (!state.active) return;
		if (pickerCancelledThisSession) return; // user cancelled the boot picker — stay quiet until /task
		if (compactionReady()) return; // file already freshly saved
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Task-journal: before starting, refresh the active task file with the latest state.");
		lastPromptAt = Date.now();
	});
}

function installTurnEnd(pi: ExtensionAPI) {
	pi.on("turn_end", async (_event, ctx) => {
		if (pickerCancelledThisSession) return; // user cancelled at boot — no routine save prompts
		if (!state.active) return;
		const pct = usagePercent(ctx);
		if (pct >= COMPACT_WARN_PERCENT) {
			sendSaveRequest(pi, `Task-journal: context at ~${Math.round(pct)}% (near auto-compact: tokens > contextWindow - reserveTokens). Write a thorough state snapshot to the active task file (must include faithful ## Original request), then run /compact immediately to free context.`);
			lastPromptAt = Date.now();
			return;
		}
		if (pct >= SAVE_PERCENT) {
			sendSaveRequest(pi, "Task-journal: context is getting large. Write a thorough state snapshot to the active task file.");
			lastPromptAt = Date.now();
			return;
		}
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Task-journal: update the active task file with the current state.");
		lastPromptAt = Date.now();
	});
}

function installBeforeCompact(pi: ExtensionAPI) {
	pi.on("session_before_compact", async (event, ctx) => {
		if (!state.active) return; // nothing to protect
		if (compactionReady()) return; // file is fresh since last compaction — allow
		ctx.ui.notify(`Task-journal: blocking compaction until '${taskPath(state.active)}' is saved.`, "warning");
		sendSaveRequest(pi, "Task-journal: save is required before compaction. Write the full task state to disk now (must include faithful ## Original request), then allow compaction to proceed.");
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
		sendSaveRequest(pi, "Task-journal: persisting before we switch sessions.");
	});
}

/** Prompt the user with an interactive selector to choose an existing task, draft, or create a new task. */
async function promptForTaskChoice(ctx: ExtensionContext, title = "Select task:"): Promise<string | null> {
	if (!ctx.hasUI || ctx.mode !== "tui") return null;
	const current = await listTaskFiles(TASK_DIR);
	const future = await listTaskFiles("docs/future");
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
	choices.push("New task…", "Cancel");

	const choice = await ctx.ui.select(title, choices);
	if (!choice || choice === "Cancel") return null;

	if (choice === "New task…") {
		const inputName = await ctx.ui.input("New task name (e.g. migrate-ftp):");
		return inputName && slugify(inputName) ? slugify(inputName) : null;
	}

	const clean = choice.replace(/ \(active\)$/, "").replace(/ \(draft\)$/, "");
	return slugify(clean);
}

/**
 * On interactive startup, ask the user what to work on: pick a current task,
 * promote a draft from docs/future/, start a new task, or skip.
 * Reuses the /task command by queueing it as a user message (extension
 * commands are checked first in input processing).
 */
async function offerTaskChoiceOnBoot(pi: ExtensionAPI, ctx: ExtensionContext, reason: string) {
	if (reason !== "startup") return;
	if (!ctx.hasUI || ctx.mode !== "tui") return;
	const choice = await promptForTaskChoice(ctx, "What do you want to work on?");
	if (!choice) {
		pickerCancelledThisSession = true; // user opted out — suppress task-journal prompts until /task
		return;
	}
	pi.sendUserMessage(`/task ${choice}`);
}

/** On quit, ask the model for a final snapshot of the active task file. */
function installShutdownSave(pi: ExtensionAPI) {
	pi.on("session_shutdown", async (event, _ctx) => {
		if (event.reason !== "quit") return;
		if (!state.active) return;
		sendSaveRequest(pi, "Task-journal: you are about to quit. Write a final state snapshot to the active task file now (must include faithful ## Original request).");
	});
}

/** A write/edit to the active task file counts as a save (lowers the compaction gate). */
function installFileWatch(pi: ExtensionAPI) {
	pi.on("tool_result", async (event) => {
		if (!state.active) return;
		if (event.toolName !== "write" && event.toolName !== "edit") return;
		const p = event.input.path as string | undefined;
		if (typeof p === "string" && normalizePath(p) === taskPath(state.active)) {
			markSaved(pi);
		}
	});
}

const normalizePath = (p: string) => p.replace(/^\.\//, "").replace(/\\/g, "/");

/** Optional tool the model can call to mark the file saved explicitly. */
function installMarkTool(pi: ExtensionAPI) {
	pi.registerTool({
		name: "task_journal_mark_saved",
		label: "Mark Task Saved",
		description: "Record that the active task file has been written to disk. Call after updating the task file.",
		parameters: { type: "object", properties: {}, additionalProperties: false },
		async execute() {
			markSaved(pi);
			return { content: [{ type: "text", text: "Task file marked as saved in the journal." }], details: {} };
		},
	});
}

// ---------------------------------------------------------------------------
// Commands
// ---------------------------------------------------------------------------

async function listTaskFiles(dir = TASK_DIR): Promise<string[]> {
	try {
		return (await readdir(dir)).filter((f) => f.endsWith(".md")).sort();
	} catch {
		return [];
	}
}

function installCommands(pi: ExtensionAPI) {
	pi.registerCommand("task", {
		description: "Set the active task (e.g. /task cx). Promotes from docs/future/ if it exists, or creates docs/current/<name>.md.",
		getArgumentCompletions: async (prefix) => {
			const current = await listTaskFiles(TASK_DIR);
			const future = await listTaskFiles("docs/future");
			const names = [...new Set([...current, ...future])].map((f) => f.replace(/\.md$/, ""));
			const filtered = names.filter((n) => n.startsWith(prefix));
			return filtered.length ? filtered.map((value) => ({ value, label: value })) : null;
		},
		handler: async (args, ctx) => {
			let name = slugify(args);
			if (!name) {
				name = (await promptForTaskChoice(ctx, "Which task do you want to work on?")) ?? "";
			}
			if (!name) {
				ctx.ui.notify("No task selected.", "warning");
				return;
			}
			await mkdir(TASK_DIR, { recursive: true });
			const path = taskPath(name);
			const futurePath = `docs/future/${name}.md`;
			
			let goal = "";
			if (!(await fileExists(path))) {
				if (await fileExists(futurePath)) {
					// Promote future task to current
					await rename(futurePath, path);
					if (ctx.hasUI) ctx.ui.notify(`Promoted ${futurePath} → ${path}`, "info");
				} else {
					if (ctx.hasUI && ctx.mode === "tui") {
						goal = ((await ctx.ui.input(`Describe the goal for task '${name}':`)) ?? "").trim();
					}
					await writeFile(path, TASK_TEMPLATE(name, goal), "utf8");
				}
			}
			// Clean up any remaining draft file in docs/future/ so no duplicate stays behind
			await cleanDraftIfExists(name);
			// Switching tasks: new task starts with fresh prompt history.
			const switching = state.active !== name;
			pickerCancelledThisSession = false; // explicit /task re-enables journal prompts
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
					text: `Now working on task **${name}**. Task file: \`${path}\`.${goalText}

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
		},
	});

	pi.registerCommand("task-save", {
		description: "Persist the active task file now.",
		handler: async (_args, ctx) => {
			if (!state.active) {
				ctx.ui.notify("No active task — use /task <name> first.", "warning");
				return;
			}
			sendSaveRequest(pi, "Task-journal: /task-save — write a full state snapshot to the active task file now.");
			lastPromptAt = Date.now();
		},
	});

	pi.registerCommand("task-refine", {
		description: "Refine the active task mid-workflow or add post-implementation requirements (e.g. /task-refine Add edge case handling).",
		handler: async (args, ctx) => {
			if (!state.active) {
				ctx.ui.notify("No active task — use /task <name> first.", "warning");
				return;
			}
			let refinement = args.trim();
			if (!refinement && ctx.mode === "tui") {
				refinement = (await ctx.ui.input("Enter task refinement or new requirements:")) ?? "";
			}
			if (!refinement) {
				ctx.ui.notify("Usage: /task-refine <instructions...>", "warning");
				return;
			}
			const entry = `Refinement: ${refinement}`;
			state.prompts.push(entry);
			persist(pi);

			sendSaveRequest(
				pi,
				`Task-journal: /task-refine — User task refinement received:\n"${refinement}"\n\nUpdate \`docs/current/${state.active}.md\` now: expand ## Goal if needed, add entry under ## Task Refinements & Iterations, update ## Remaining work and ## TDD & Quality Checklist, and record any new decisions.`
			);
			lastPromptAt = Date.now();
			if (ctx.hasUI) ctx.ui.notify(`Refinement queued for active task '${state.active}'`, "info");
		},
	});

	pi.registerCommand("task-del", {
		description: "Archive (rename to docs/archive/) the current or named task file.",
		handler: async (args, ctx) => {
			let name = slugify(args);
			if (!name) {
				name = (await promptForTaskChoice(ctx, "Select task to archive:")) ?? "";
			}
			if (!name) {
				ctx.ui.notify("No task selected for archiving.", "warning");
				return;
			}
			const path = taskPath(name);
			if (!(await fileExists(path))) {
				ctx.ui.notify(`No file at ${path}`, "warning");
				return;
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
			if (ctx.hasUI) ctx.ui.notify(`Archived ${path} → ${dest}`, "info");
		},
	});

	pi.registerCommand("task-draft", {
		description: "Draft a future task or proposal without making it active.",
		handler: async (args, ctx) => {
			let name = slugify(args);
			if (!name && ctx.mode === "tui") {
				name = slugify((await ctx.ui.input("Future task name (e.g. cx-ergonomics):")) ?? "");
			}
			if (!name) {
				ctx.ui.notify("Usage: /task-draft <name>", "warning");
				return;
			}
			const currentPath = taskPath(name);
			if (await fileExists(currentPath)) {
				ctx.ui.notify(`Task '${name}' is already active/current in ${currentPath}. Cannot create a draft for an active task.`, "warning");
				return;
			}
			await mkdir("docs/future", { recursive: true });
			const path = `docs/future/${name}.md`;
			if (!(await fileExists(path))) {
				await writeFile(path, FUTURE_TASK_TEMPLATE(name), "utf8");
				if (ctx.hasUI) ctx.ui.notify(`Created draft proposal at ${path}`, "info");
			} else {
				if (ctx.hasUI) ctx.ui.notify(`Draft already exists at ${path}`, "warning");
			}
		},
	});

	pi.registerCommand("task-status", {
		description: "Show the active task and whether its file is fresh.",
		handler: async (_args, ctx) => {
			if (!state.active) {
				if (ctx.hasUI) ctx.ui.notify("No active task.", "info");
				return;
			}
			const path = taskPath(state.active);
			const exists = await fileExists(path);
			const fresh = compactionReady();
			const pct = usagePercent(ctx);
			const line = exists
				? `${path} — ${fresh ? "fresh" : "SAVE PENDING"}, context ~${Math.round(pct)}%, prompts ${state.prompts.length}`
				: `${path} — MISSING on disk!`;
			if (ctx.hasUI) ctx.ui.notify(`Active task: ${line}`, fresh ? "info" : "warning");
		},
	});

	pi.registerCommand("tasks", {
		description: "List current and future tasks.",
		handler: async (_args, ctx) => {
			const current = await listTaskFiles(TASK_DIR);
			const future = await listTaskFiles("docs/future");
			const rows = current.length
				? current.map((f) => `  ${f.replace(/\.md$/, "")}${state.active === f.replace(/\.md$/, "") ? "  ◀ active" : ""}`)
				: ["  (none — use /task <name>)"];
			const futureRows = future.length
				? future.map((f) => `  ${f.replace(/\.md$/, "")}`)
				: ["  (none — use /task-draft <name>)"];
			
			ctx.ui.setWidget("task-journal", [
				`Active: ${state.active ? taskPath(state.active) : "(none)"}`, 
				"",
				"Current tasks:",
				...rows,
				"",
				"Future / Backlog tasks:",
				...futureRows
			]);
		},
	});
}

function FUTURE_TASK_TEMPLATE(name: string): string {
	return [
		`# Proposal / Future Task: ${name}`,
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

function TASK_TEMPLATE(name: string, goal = ""): string {
	return [
		`# Task: ${name}`,
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
		`- [ ] **2. Write Tests First**: Developed test(s) for the task BEFORE feature code.`,
		`- [ ] **3. Feature Implementation**: Developed feature to satisfy tests.`,
		`- [ ] **4. Build & Run**: Built and ran project with zero build errors.`,
		`- [ ] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.`,
		`- [ ] **6. Full Test Suite**: Executed FULL test suite with zero errors.`,
		``,
		`## In-Depth Analysis & Findings`,
		`> Root cause analysis, architectural friction, abstraction opportunities.`,
		`- `,
		``,
		`## Detailed Multi-Stage Execution Plan`,
		`> Each stage must be self-contained as if it were a single task, with exact signatures, touched files, and targeted tests.`,
		`### Stage 1: `,
		`- **Target**: `,
		`- **Tasks**: `,
		`- **Targeted Tests**: `,
		``,
		`## Acceptance Criteria & Polish Checklist`,
		`- [ ] `,
		``,
		`## Task Refinements & User Feedback Loops`,
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

async function loadActiveTaskResumeContext(): Promise<string> {
	if (!state.active) return "";
	const path = taskPath(state.active);
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
		return `\n\n# Active Task Resume Context (from \`${path}\`)\n${extracted.join("\n\n")}`;
	} catch {
		return "";
	}
}

function installWorkflowSystemPrompt(pi: ExtensionAPI) {
	pi.on("before_agent_start", async (event) => {
		const resumeContext = await loadActiveTaskResumeContext();
		const workflowInstructions = `\n\n# Mandatory Task Workflow Rules (TDD & Quality Gates)
When working on tasks:
1. **Upfront Deep Research & Planning**:
   - Before writing feature code, conduct a thorough architectural audit of the relevant codebase, understand constraints, and design an ambitious, multi-stage plan where each phase is self-contained.
   - Present findings, trade-offs, and the plan in turn 1 for user confirmation.
2. **Build & Run Discovery**: Discovered how to build and run the project before editing code.
3. **Develop Tests First (TDD)**: Develop targeted test(s) BEFORE developing each feature phase.
4. **Iterative Build, Run & Test**: Feature implementation -> build -> run -> verify targeted tests.
5. **Post-Implementation & User Feedback Loops**:
   - Expect and support user polish iterations at the end of a task.
   - When the user provides feedback or refinements, log them under \`## Task Refinements & User Feedback Loops\`, update acceptance checklists, execute the changes, and verify with tests until the user confirms satisfaction.
6. **Task Completion & Wrap-Up Flow**:
   - When a task is completed (all features, acceptance criteria, and quality gates pass), ALWAYS prompt the user via \`ask_questions\` with the following structured options:
     1. **Refine anything**: Keep active task open to make further adjustments or address feedback.
     2. **Archive task and auto-compact**: Archive the task (\`/task-del\` / move to \`docs/archive/\`), clear the active task state, and trigger session compaction (\`/compact\`).
     3. **Archive task without auto-compact**: Archive the task to \`docs/archive/\` and keep the current session context intact without compaction.
     4. **Change to manual mode**: Switch workflow / compaction to manual mode.
   - Execute the exact action corresponding to the user's choice.
7. **Final Verification & Quality Gates**:
   - Zero compiler errors or warnings.
   - Zero debug artifacts (no leftover console.logs, prints, or scratch code).
   - Full test suite (\`make test\`) must pass with zero errors.

# Task Management & Verbal Requests
You maintain long-lived task state on disk in \`docs/current/<task>.md\`.
When users ask in natural language to manage tasks (verbally refining, switching, drafting, archiving, or listing), apply these rules directly:

1. **Refine Active Task / User Feedback** (e.g. "refine task", "add requirement X", "tweak Y", "feedback: Z"):
   - Read and update \`docs/current/<active-task>.md\` immediately using \`edit\` or \`write\`.
   - Record the user feedback under \`## Task Refinements & User Feedback Loops\`.
   - Update \`## Remaining work\`, \`## Acceptance Criteria & Polish Checklist\`, \`## Decisions made\`, and \`## Next recommended step\`.
   - Call \`task_journal_mark_saved\` after updating to keep journal staleness in sync.

2. **Start / Switch Task** (e.g. "switch to task X", "start task Y"):
   - Maintain/create \`docs/current/<task>.md\`. If a proposal exists at \`docs/future/<task>.md\`, promote it by moving it to \`docs/current/<task>.md\`.
   - Fill initial Goal, Current Status, and TDD checklist before feature work.

3. **Draft Future Task** (e.g. "draft task X", "propose task Y for later"):
   - Create \`docs/future/<task>.md\` using proposal format (Goals & Scope, Requirements, Implementation Plan). Do not make it active.

4. **Archive Task** (e.g. "archive task X", "finish task Y"):
   - Move \`docs/current/<task>.md\` to \`docs/archive/<task>-<timestamp>.md\` and remove any matching draft in \`docs/future/\`.

5. **List / Status** (e.g. "what are my tasks?", "show task status"):
   - Inspect files in \`docs/current/\` and \`docs/future/\` to report task status.${resumeContext}`;

		return { systemPrompt: `${event.systemPrompt}${workflowInstructions}` };
	});
}

function registerTaskJournalCRBHook() {
	if (typeof globalThis !== "undefined") {
		const g = globalThis as any;
		if (!g.__pi_crb_providers) {
			g.__pi_crb_providers = [];
		}
		g.__pi_crb_providers.push((_ctx: ExtensionContext, tools: string[]) => {
			const set = new Set(tools.map((t) => t.toLowerCase()));
			if (set.has("task_journal_mark_saved") || state.active) {
				return [
					"Call `task_journal_mark_saved` after updating active task files.",
					"When completing a task, prompt via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
				];
			}
			return [];
		});
	}
}

export default function (pi: ExtensionAPI) {
	registerTaskJournalCRBHook();
	pi.on("session_start", async (event, ctx) => {
		reconstruct(ctx);
		await offerTaskChoiceOnBoot(pi, ctx, event.reason);
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
	installCommands(pi);
	installShutdownSave(pi);

	// Durable in-session marker, not sent to the LLM.
	pi.registerEntryRenderer<StoredState>(CUSTOM_TYPE, (entry, _o, theme) => {
		const data = entry.data ?? ({} as StoredState);
		const fresh = data.saveCount > data.compactCount;
		return new Text(
			`${theme.fg("accent", "🗂 ")}${theme.fg("muted", "task:")} ${data.active ?? "(none)"}${
				fresh ? "" : theme.fg("warning", " (save pending)")
			}`,
			0,
			0,
		);
	});
}
