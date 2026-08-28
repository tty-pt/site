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
const DEFAULT_ECONOMY_TOKENS = 140_000; // 140K default auto-compaction threshold
const DEFAULT_PRE_COMPACT_WARNING_TOKENS = 30_000; // 30K default pre-compaction warning margin

interface StoredState {
	active: string | null;
	saveCount: number;
	compactCount: number;
	prompts: string[];
	stack: string[];
	economyTokens?: number | null;
	warningMarginTokens?: number | null;
}

let state: StoredState = { active: null, saveCount: 0, compactCount: 0, prompts: [], stack: [], economyTokens: undefined, warningMarginTokens: undefined };
let lastPromptAt = Date.now();
let pickerCancelledThisSession = false;
let currentContext: ExtensionContext | undefined;

// ---------------------------------------------------------------------------
// Format & Token Economy Helpers
// ---------------------------------------------------------------------------

function formatTokens(num: number): string {
	if (num >= 1_000_000) {
		const m = num / 1_000_000;
		return `${m % 1 === 0 ? m.toFixed(0) : m.toFixed(1)}M`;
	}
	if (num >= 1_000) {
		const k = num / 1_000;
		return `${k % 1 === 0 ? k.toFixed(0) : k.toFixed(1)}k`;
	}
	return `${num}`;
}

function parseTokenAmount(val: unknown, defaultVal: number = DEFAULT_ECONOMY_TOKENS): number | null {
	if (typeof val === "number" && !Number.isNaN(val)) {
		return val > 0 ? Math.round(val) : 0;
	}
	if (typeof val !== "string") return null;
	const s = val.trim().toLowerCase();
	if (!s) return null;
	if (s === "off" || s === "disable" || s === "disabled" || s === "0") return 0;
	if (s === "default") return defaultVal;

	// Matches "140k", "140 k", "1.5m", "140000", "140000 tokens", "140,000"
	const cleaned = s.replace(/,/g, "");
	const match = cleaned.match(/^([\d.]+)\s*([km])?(?:\s*tokens)?$/i);
	if (!match) return null;
	const base = Number.parseFloat(match[1]);
	if (Number.isNaN(base) || base <= 0) return null;
	const unit = match[2]?.toLowerCase();
	if (unit === "k") return Math.round(base * 1000);
	if (unit === "m") return Math.round(base * 1000000);
	return Math.round(base);
}

function readSettingsEconomyThreshold(): number | null {
	for (const p of [".pi/settings.json", "~/.pi/agent/settings.json"]) {
		try {
			const resolved = p.startsWith("~") ? p.replace(/^~/, process.env.HOME || "") : p;
			const raw = readFileSync(resolved, "utf8");
			const json = JSON.parse(raw);
			const val = json?.questJournal?.economyTokens ?? json?.questJournal?.autoCompactTokens ?? json?.compaction?.economyTokens;
			const parsed = parseTokenAmount(val);
			if (parsed !== null) return parsed;
		} catch {
			// ignore missing or unreadable config
		}
	}
	return null;
}

function getEconomyThreshold(): number {
	if (typeof state.economyTokens === "number") {
		return state.economyTokens;
	}
	const envVal = process.env.PI_QUEST_AUTO_COMPACT_TOKENS ?? process.env.QUEST_AUTO_COMPACT_TOKENS;
	const parsedEnv = parseTokenAmount(envVal);
	if (parsedEnv !== null) return parsedEnv;

	const parsedSettings = readSettingsEconomyThreshold();
	if (parsedSettings !== null) return parsedSettings;

	return DEFAULT_ECONOMY_TOKENS;
}

function readSettingsWarningMargin(): number | null {
	for (const p of [".pi/settings.json", "~/.pi/agent/settings.json"]) {
		try {
			const resolved = p.startsWith("~") ? p.replace(/^~/, process.env.HOME || "") : p;
			const raw = readFileSync(resolved, "utf8");
			const json = JSON.parse(raw);
			const val = json?.questJournal?.preCompactWarningTokens ?? json?.questJournal?.warningTokens ?? json?.compaction?.warningMarginTokens;
			const parsed = parseTokenAmount(val, DEFAULT_PRE_COMPACT_WARNING_TOKENS);
			if (parsed !== null) return parsed;
		} catch {
			// ignore missing or unreadable config
		}
	}
	return null;
}

function getWarningMargin(): number {
	if (typeof state.warningMarginTokens === "number") {
		return state.warningMarginTokens;
	}
	const envVal = process.env.PI_QUEST_PRE_COMPACT_WARNING_TOKENS ?? process.env.QUEST_PRE_COMPACT_WARNING_TOKENS;
	const parsedEnv = parseTokenAmount(envVal, DEFAULT_PRE_COMPACT_WARNING_TOKENS);
	if (parsedEnv !== null) return parsedEnv;

	const parsedSettings = readSettingsWarningMargin();
	if (parsedSettings !== null) return parsedSettings;

	return DEFAULT_PRE_COMPACT_WARNING_TOKENS;
}

function formatQuestHierarchy(active: string | null, stack?: string[]): string {
	if (!active) return "(none)";
	if (!stack || stack.length === 0) return active;

	// Clean up duplicate consecutive entries and ensure active is at tail
	const cleanStack: string[] = [];
	for (const item of stack) {
		if (item && (cleanStack.length === 0 || cleanStack[cleanStack.length - 1] !== item)) {
			cleanStack.push(item);
		}
	}
	if (!cleanStack.includes(active)) {
		cleanStack.push(active);
	} else if (cleanStack[cleanStack.length - 1] !== active) {
		const idx = cleanStack.lastIndexOf(active);
		cleanStack.splice(idx + 1);
	}

	if (cleanStack.length <= 1) return active;
	return cleanStack.join(" ↳ ");
}

// ---------------------------------------------------------------------------
// State persistence (custom entries survive reloads and branching)
// ---------------------------------------------------------------------------

function persist(pi: ExtensionAPI, ctx?: ExtensionContext) {
	if (ctx) currentContext = ctx;
	try {
		pi.appendEntry<StoredState>(CUSTOM_TYPE, state);
	} catch {
		// ephemeral / unsupported session: stay in-memory only
	}
	updateUIStatus(ctx);
}

/** Update the persistent status bar above the prompt box. */
function updateUIStatus(ctx?: ExtensionContext) {
	const c = ctx || currentContext;
	if (c?.hasUI) {
		const fresh = compactionReady();
		const hier = formatQuestHierarchy(state.active, state.stack);
		const usage = typeof c.getContextUsage === "function" ? c.getContextUsage() : undefined;
		const threshold = getEconomyThreshold();

		let tokenInfo = "";
		const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);

		if (tokens !== null && tokens > 0) {
			if (threshold > 0) {
				tokenInfo = ` [${formatTokens(tokens)}/${formatTokens(threshold)}]`;
			} else {
				tokenInfo = ` [${formatTokens(tokens)}]`;
			}
		} else if (typeof usage?.percent === "number" && usage.percent > 0) {
			tokenInfo = ` [${Math.round(usage.percent)}%]`;
		}

		let stateTag = "";
		if (!fresh) {
			stateTag = " (save pending)";
		} else if (threshold > 0 && tokens !== null && tokens >= threshold) {
			stateTag = " (compaction ready)";
		}

		const text = state.active
			? `✨ quest: ${hier}${tokenInfo}${stateTag}`
			: undefined;
		c.ui.setStatus("quest", text);
	}
}

/** Auto-detect active quest from docs/current/ if not explicitly set in session. */
async function syncActiveQuestFromDisk(pi?: ExtensionAPI, ctx?: ExtensionContext) {
	if (ctx) currentContext = ctx;
	if (!state.active) {
		const current = await listQuestFiles(QUEST_DIR);
		if (current.length === 1) {
			state.active = current[0].replace(/\.md$/, "");
			state.stack = [state.active];
			if (pi) persist(pi, ctx);
		}
	}
	updateUIStatus(ctx);
}

/** Rebuild `state` from the latest `quest_journal` (or legacy `task_journal`) entry in the active branch. */
function reconstruct(ctx: ExtensionContext) {
	currentContext = ctx;
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && (entry.customType === CUSTOM_TYPE || entry.customType === LEGACY_CUSTOM_TYPE) && entry.data) {
			latest = entry.data as unknown as StoredState;
		}
	}
	state = latest && latest.active
		? {
				active: latest.active,
				saveCount: latest.saveCount || 0,
				compactCount: latest.compactCount || 0,
				prompts: Array.isArray(latest.prompts) ? latest.prompts : [],
				stack: Array.isArray(latest.stack) ? latest.stack : (latest.active ? [latest.active] : []),
				economyTokens: typeof latest.economyTokens === "number" ? latest.economyTokens : undefined,
				warningMarginTokens: typeof latest.warningMarginTokens === "number" ? latest.warningMarginTokens : undefined,
		  }
		: { active: null, saveCount: 0, compactCount: 0, prompts: [], stack: [], economyTokens: undefined, warningMarginTokens: undefined };
	lastPromptAt = Date.now();
	void syncActiveQuestFromDisk(undefined, ctx);
}

// ---------------------------------------------------------------------------
// Helpers & Environment Inspection
// ---------------------------------------------------------------------------

function slugify(name: string, maxLen = 80): string {
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

/** Link a child sub-quest into the parent quest markdown file under ## Sub-Quests. */
async function linkSubQuestInParent(parentSlug: string, childSlug: string, description = ""): Promise<boolean> {
	if (!parentSlug || !childSlug || parentSlug === childSlug) return false;
	const currentPath = questPath(parentSlug);
	const futurePath = `docs/future/${parentSlug}.md`;
	const targetPath = (await fileExists(currentPath)) ? currentPath : (await fileExists(futurePath)) ? futurePath : null;
	if (!targetPath) return false;

	try {
		let content = await readFile(targetPath, "utf8");
		const linkEntry = description ? `- [ ] [[${childSlug}]] — ${description}` : `- [ ] [[${childSlug}]]`;

		// If child already referenced in the file, don't duplicate
		if (content.includes(`[[${childSlug}]]`)) return true;

		const subQuestsSectionRegex = /^(##\s+Sub-Quests\s*\n)([\s\S]*?)(?=\n##\s+|$)/m;
		const match = content.match(subQuestsSectionRegex);

		if (match) {
			const sectionBody = match[2];
			// If body only has empty checkbox `- [ ] \n` or placeholder, replace or append
			const cleanedBody = sectionBody.replace(/- \[\s*\]\s*(\n|$)/g, "").trimEnd();
			const newSectionBody = cleanedBody ? `${cleanedBody}\n${linkEntry}\n` : `> Sub-quests, follow-ups, or tangent quests spawned from this quest.\n${linkEntry}\n`;
			content = content.replace(subQuestsSectionRegex, `$1${newSectionBody}`);
		} else {
			// Insert ## Sub-Quests section before ## Why this matters or ## Decisions made or at end
			const insertBeforeRegex = /\n(##\s+(?:Why this matters|Decisions made|Constraints & Rules|Remaining work))/;
			const newSection = `\n## Sub-Quests\n> Sub-quests, follow-ups, or tangent quests spawned from this quest.\n${linkEntry}\n`;
			if (insertBeforeRegex.test(content)) {
				content = content.replace(insertBeforeRegex, `${newSection}\n$1`);
			} else {
				content = `${content.trimEnd()}\n${newSection}`;
			}
		}

		await writeFile(targetPath, content, "utf8");
		return true;
	} catch {
		return false;
	}
}

/** Extract parent quest slug from quest markdown file, if present. */
function extractParentFromQuest(content: string): string | null {
	const parentRegex = /##\s+Parent Quest\s*\n+(?:>.*?\n+)*\s*(?:\[\[([^\]]+)\]\]|([a-zA-Z0-9_\-\.]+))/i;
	const match = content.match(parentRegex);
	if (match) {
		const raw = match[1] || match[2];
		return raw ? slugify(raw) : null;
	}
	return null;
}

/** Extract sub-quests list from quest markdown file. */
function extractSubQuestsFromQuest(content: string): string[] {
	const subRegex = /##\s+Sub-Quests\s*\n([\s\S]*?)(?=\n##\s+|$)/i;
	const match = content.match(subRegex);
	if (!match) return [];
	const body = match[1];
	const results: string[] = [];
	const linkRegex = /\[\[([^\]]+)\]\]/g;
	let m: RegExpExecArray | null;
	while ((m = linkRegex.exec(body)) !== null) {
		if (m[1]) results.push(m[1].trim());
	}
	return results;
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
	const u = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
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
	if (t.startsWith("/quest") || t.startsWith("/subquest")) return false;
	if (t.startsWith("Quest-journal:")) return false;
	if (t.length < 2) return false;
	return true;
}

function promptsBlock(): string {
	if (!state.prompts || state.prompts.length === 0) return "(none captured yet — this is the first substantive request; use the current user message)";
	return state.prompts.map((p, i) => `${i + 1}. ${p}`).join("\n\n---\n\n");
}

/** Queue a user message asking the model to update the quest file with standard prompt. */
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

/** Queue a user message asking the model to perform an exhaustive context dump before compaction. */
function sendDeepSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request(s) for this quest — keep VERBATIM (or very faithful if truncated) under ## Original request in the quest file:\n${promptsBlock()}`;
	pi.sendUserMessage([
		{
			type: "text",
			text: `${message}\n\n${promptReminder}\n\nActive quest file: \`${questPath(state.active)}\`\n\n**PRE-COMPACTION EXHAUSTIVE CONTEXT PRESERVATION PROTOCOL**:\nBefore context compaction resets working memory, update \`${questPath(state.active)}\` with an exhaustive dump so the next iteration requires ZERO re-research:\n1. ## Original request: Keep user prompts verbatim.\n2. ## Current Status & Progress: Complete checklist of what's done, in progress, or pending.\n3. ## In-Depth Analysis & Findings: All technical findings, root cause analysis, architecture discoveries, data flows, exact function signatures, and explored trade-offs.\n4. ## Files touched: Complete list of touched and examined files.\n5. ## Decisions made: Every architectural and design decision with rationale.\n6. ## Sub-Quests: Current status of all sub-quests and parent links.\n7. ## Remaining work: Exact actionable checklist of remaining tasks.\n8. ## Resume prompt: A comprehensive multi-paragraph briefing giving the next agent iteration complete context so it resumes seamlessly with ZERO re-research.\n\nEnsure build/run verification, clean code (no debug artifacts), and tests are up to date. Once written, call \`quest_mark_saved\`.`,
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
		const hier = formatQuestHierarchy(state.active, state.stack);
		const usage = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
		const threshold = getEconomyThreshold();
		const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
		const tokenStr = tokens !== null ? ` | tokens: ${formatTokens(tokens)}${threshold > 0 ? `/${formatTokens(threshold)}` : ""}` : "";
		const stackInfo = state.stack && state.stack.length > 1 ? ` | LIFO stack: [${state.stack.join(" → ")}]` : "";
		lines.push(
			`- Active quest: \`docs/current/${state.active}.md\` [${hier}] (${fresh ? "fresh" : "SAVE PENDING — update it before compaction"}${tokenStr}${stackInfo}); manage with /quest, /subquest, /quests, /quest-economy.`,
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
	pi.on("before_agent_start", async (event: any) => {
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
	pi.on("before_agent_start", async (_event: any, ctx: ExtensionContext) => {
		currentContext = ctx;
		if (!state.active) return;
		if (pickerCancelledThisSession) return; // user cancelled the boot picker — stay quiet until /quest
		if (compactionReady()) return; // file already freshly saved
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Quest-journal: before starting, refresh the active quest file with the latest state.");
		lastPromptAt = Date.now();
	});
}

function installTurnEnd(pi: ExtensionAPI) {
	pi.on("turn_end", async (_event: any, ctx: ExtensionContext) => {
		currentContext = ctx;
		if (pickerCancelledThisSession) return; // user cancelled at boot — no routine save prompts
		if (!state.active) return;

		const usage = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
		const threshold = getEconomyThreshold();
		const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
		const pct = usagePercent(ctx);

		if (threshold > 0 && tokens !== null) {
			const warningMargin = getWarningMargin();
			const warningTokens = Math.max(0, threshold - warningMargin);
			if (tokens >= warningTokens) {
				if (compactionReady()) {
					// Quest file is updated and saved: trigger auto-compaction immediately!
					ctx.compact({
						customInstructions: `Economy auto-compaction at ${formatTokens(tokens)} tokens (threshold: ${formatTokens(threshold)}). Focus summary on active quest '${state.active}', key architectural decisions, modified files, and immediate next steps. All deep context and research findings have been permanently persisted to disk in docs/current/${state.active}.md.`,
						onComplete: () => {
							if (ctx.hasUI) ctx.ui.notify(`Economy auto-compaction completed at ${formatTokens(tokens)} tokens.`, "info");
							updateUIStatus(ctx);
						},
						onError: (err: any) => {
							if (ctx.hasUI) ctx.ui.notify(`Economy auto-compaction failed: ${err?.message || err}`, "error");
						},
					});
					lastPromptAt = Date.now();
					return;
				} else {
					// In the pre-compaction warning window with save pending: send explicit directive
					sendDeepSaveRequest(
						pi,
						`🚨 Quest-journal economy: token usage is at ${formatTokens(tokens)} (within ${formatTokens(warningMargin)} of the ${formatTokens(threshold)} auto-compaction limit). AUTO-COMPACTION WILL OCCUR SOON to reset working memory. You MUST update \`${questPath(state.active)}\` now with an exhaustive context snapshot (all technical findings, architecture discoveries, decisions made, touched files, test status, and comprehensive ## Resume prompt) so that the subsequent iteration does not re-research. After updating the quest files, compaction will immediately trigger.`,
					);
					lastPromptAt = Date.now();
					return;
				}
			}
		}

		if (pct >= COMPACT_WARN_PERCENT) {
			sendDeepSaveRequest(pi, `Quest-journal: context at ~${Math.round(pct)}% (near auto-compact: tokens > contextWindow - reserveTokens). Write an exhaustive state snapshot to the active quest file (must include faithful ## Original request and comprehensive resume context), then run /compact immediately to free context.`);
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
	pi.on("session_before_compact", async (_event: any, ctx: ExtensionContext) => {
		currentContext = ctx;
		if (!state.active) return; // nothing to protect
		if (compactionReady()) return; // file is fresh since last compaction — allow
		ctx.ui.notify(`Quest-journal: blocking compaction until '${questPath(state.active)}' is saved.`, "warning");
		sendDeepSaveRequest(pi, "Quest-journal: save is REQUIRED before compaction. Write the full exhaustive quest state to disk now (must include faithful ## Original request and comprehensive resume context), then allow compaction to proceed.");
		return { cancel: true };
	});
}

function installAfterCompact(pi: ExtensionAPI) {
	pi.on("session_compact", async (_event: any, ctx: ExtensionContext) => {
		currentContext = ctx;
		if (!state.active) return;
		state.compactCount = state.saveCount; // a compaction happened; next save re-arms the gate
		persist(pi, ctx);
	});
}

function installBeforeSwitch(pi: ExtensionAPI) {
	pi.on("session_before_switch", async (_event: any, ctx: ExtensionContext) => {
		currentContext = ctx;
		if (!state.active) return;
		if (withinCooldown()) return;
		sendSaveRequest(pi, "Quest-journal: persisting before we switch sessions.");
	});
}

interface QuestChoiceResult {
	name: string;
	goal?: string;
}

/** Prompt the user with an interactive selector to choose an existing quest, draft, or create a new quest. */
async function promptForQuestChoice(ctx: ExtensionContext, title = "Select quest:"): Promise<QuestChoiceResult | null> {
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
		const nameInput = (await ctx.ui.input("Enter short quest name / slug (e.g. expand-editor-textarea):")) ?? "";
		const trimmedName = nameInput.trim();
		if (!trimmedName) return null;
		const name = slugify(trimmedName, 45);
		if (!name) return null;
		const goalInput = (await ctx.ui.input("Describe what you want to accomplish (optional):")) ?? "";
		const trimmedGoal = goalInput.trim();
		return { name, goal: trimmedGoal || trimmedName };
	}

	const clean = choice.replace(/ \(active\)$/, "").replace(/ \(draft\)$/, "");
	const name = slugify(clean);
	return name ? { name } : null;
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
	if (choice.goal && choice.goal !== choice.name) {
		pi.sendUserMessage(`/quest ${choice.name} ${choice.goal}`);
	} else {
		pi.sendUserMessage(`/quest ${choice.name}`);
	}
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
	pi.on("tool_result", async (event, ctx) => {
		if (event.toolName !== "write" && event.toolName !== "edit") return;
		const p = event.input.path as string | undefined;
		if (typeof p !== "string") return;
		const norm = normalizePath(p);
		if (norm.startsWith(`${QUEST_DIR}/`) && norm.endsWith(".md")) {
			const slug = basename(norm).replace(/\.md$/, "");
			if (slug && (!state.active || state.active !== slug)) {
				state.active = slug;
				state.saveCount += 1;
				lastPromptAt = Date.now();
				persist(pi, ctx);
				return;
			}
		}
		if (state.active && norm === questPath(state.active)) {
			markSaved(pi);
			updateUIStatus(ctx);

			const c = ctx || currentContext;
			const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
			const threshold = getEconomyThreshold();
			const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
			const warningMargin = getWarningMargin();
			const warningTokens = Math.max(0, threshold - warningMargin);

			if (state.active && threshold > 0 && tokens !== null && tokens >= warningTokens && typeof c?.compact === "function") {
				c.compact({
					customInstructions: `Economy auto-compaction at ${formatTokens(tokens)} tokens (threshold: ${formatTokens(threshold)}). Focus summary on active quest '${state.active}', key architectural decisions, modified files, and immediate next steps. All deep context and research findings have been permanently persisted to disk in docs/current/${state.active}.md.`,
					onComplete: () => {
						if (c.hasUI) c.ui.notify(`Economy auto-compaction completed at ${formatTokens(tokens)} tokens.`, "info");
						updateUIStatus(c);
					},
					onError: (err: any) => {
						if (c.hasUI) c.ui.notify(`Economy auto-compaction failed: ${err?.message || err}`, "error");
					},
				});
			}
		}
	});
}

/** Mark a sub-quest as completed in parent quest markdown file. */
async function markSubQuestCompletedInParent(parentSlug: string, childSlug: string): Promise<boolean> {
	const parentPath = questPath(parentSlug);
	if (!(await fileExists(parentPath))) return false;
	try {
		let content = await readFile(parentPath, "utf8");
		const regex = new RegExp(`(-\\s*\\[)\\s*(\\]\\s*\\[\\[${childSlug}\\]\\])`, "g");
		if (regex.test(content)) {
			content = content.replace(regex, "$1x$2");
			await writeFile(parentPath, content, "utf8");
			return true;
		}
	} catch {
		// ignore
	}
	return false;
}

const normalizePath = (p: string) => p.replace(/^\.\//, "").replace(/\\/g, "/");

/** Helper to archive a quest file and update journal state (LIFO stack pop) */
async function archiveQuestFile(name: string, pi: ExtensionAPI, ctx?: ExtensionContext): Promise<{ success: boolean; message: string; dest?: string; nextActive?: string | null }> {
	if (ctx) currentContext = ctx;
	const path = questPath(name);
	if (!(await fileExists(path))) {
		return { success: false, message: `No quest file found at ${path}` };
	}

	let parentSlug: string | null = null;
	try {
		const content = await readFile(path, "utf8");
		parentSlug = extractParentFromQuest(content);
	} catch {
		// ignore
	}

	await mkdir("docs/archive", { recursive: true });
	const dest = `docs/archive/${basename(path).replace(/\.md$/, "")}-${Date.now().toString(36)}.md`;
	await rename(path, dest);
	await cleanDraftIfExists(name);

	// LIFO stack management: remove archived quest from stack
	const stack = Array.isArray(state.stack) ? [...state.stack] : (state.active ? [state.active] : []);
	const idx = stack.lastIndexOf(name);
	if (idx >= 0) {
		stack.splice(idx, 1);
	}

	// Find the top valid quest remaining on the LIFO stack
	let nextActive: string | null = null;
	while (stack.length > 0) {
		const candidate = stack[stack.length - 1];
		if (await fileExists(questPath(candidate))) {
			nextActive = candidate;
			break;
		}
		stack.pop();
	}

	// Fallback to parent from file if stack had no active candidate
	if (!nextActive && parentSlug && (await fileExists(questPath(parentSlug)))) {
		nextActive = parentSlug;
		stack.push(parentSlug);
	}

	// Mark sub-quest completed (- [x]) in parent quest file
	if (parentSlug) {
		await markSubQuestCompletedInParent(parentSlug, name);
	}

	if (state.active === name) {
		state.active = nextActive;
		state.stack = stack;
		state.prompts = [];
		persist(pi, ctx);
	} else {
		state.stack = stack;
		persist(pi, ctx);
	}

	const returnMsg = nextActive ? ` Resumed parent/previous quest '${nextActive}' (LIFO stack).` : "";
	return { success: true, message: `Archived ${path} → ${dest}.${returnMsg}`, dest, nextActive };
}

/** Tool allowing the model to explicitly archive the active (or named) quest and trigger auto-compaction. */
function installArchiveTool(pi: ExtensionAPI) {
	const archiveHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		currentContext = ctx;
		const targetName = slugify(params.questName || state.active || "");
		if (!targetName) {
			return {
				content: [{ type: "text", text: "Error: No active quest to archive and no questName provided." }],
				details: { error: "no_quest" },
			};
		}

		const res = await archiveQuestFile(targetName, pi, ctx);
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
					updateUIStatus(ctx);
				},
				onError: (err: any) => {
					if (ctx.hasUI) ctx.ui.notify(`Post-archive compaction failed: ${err?.message || err}`, "error");
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
		name: "quest_archive",
		label: "Archive Quest",
		description: "Archive the active (or specified) quest from docs/current/ to docs/archive/ and optionally trigger session context compaction.",
		parameters: {
			type: "object",
			properties: {
				questName: {
					type: "string",
					description: "Quest name to archive. Defaults to currently active quest.",
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

	pi.registerTool({
		name: "quest_journal_archive",
		label: "Archive Quest (alias for quest_archive)",
		description: "Archive the active (or specified) quest from docs/current/ to docs/archive/ and optionally trigger session context compaction.",
		parameters: {
			type: "object",
			properties: {
				questName: {
					type: "string",
					description: "Quest name to archive. Defaults to currently active quest.",
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
}

function installSubQuestTool(pi: ExtensionAPI) {
	const subquestHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		const goal = (params.goal || params.name || "").trim();
		const name = slugify(params.name || goal || "");
		const parentName = slugify(params.parentName || state.active || "");
		const switchNow = params.switchNow === true;

		if (!name) {
			return {
				content: [{ type: "text", text: "Error: Sub-quest name or goal description is required." }],
				details: { error: "missing_name" },
			};
		}
		if (!goal) {
			return {
				content: [{ type: "text", text: "Error: Sub-quest goal description is required." }],
				details: { error: "missing_goal" },
			};
		}

		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(name);

		if (await fileExists(path)) {
			// Sub-quest file already exists — link in parent if not already linked
			if (parentName) {
				await linkSubQuestInParent(parentName, name, goal);
			}
			return {
				content: [{ type: "text", text: `Sub-quest '${name}' already exists at ${path}.${parentName ? ` Verified link in parent '${parentName}'.` : ""}` }],
				details: { subquest: name, path, existing: true, parent: parentName },
			};
		}

		await writeFile(path, QUEST_TEMPLATE(name, goal, parentName), "utf8");
		if (parentName) {
			await linkSubQuestInParent(parentName, name, goal);
		}

		if (switchNow) {
			pickerCancelledThisSession = false;
			if (!Array.isArray(state.stack)) state.stack = [];
			if (parentName && !state.stack.includes(parentName)) {
				state.stack.push(parentName);
			}
			if (!state.stack.includes(name)) {
				state.stack.push(name);
			} else {
				const idx = state.stack.lastIndexOf(name);
				state.stack = state.stack.slice(0, idx + 1);
			}
			state.active = name;
			state.prompts = [goal ? `Goal: ${goal}` : `Sub-quest: ${name}`];
			state.saveCount += 1;
			persist(pi, ctx);
		}

		const msg = `Created sub-quest **${name}** at \`${path}\`${parentName ? ` (parent: **${parentName}**)` : ""}.${switchNow ? " Switched active quest to this sub-quest." : " Kept parent quest active; sub-quest added to tracker."}`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");

		return {
			content: [{ type: "text", text: msg }],
			details: { subquest: name, path, parent: parentName, switched: switchNow },
		};
	};

	pi.registerTool({
		name: "quest_subquest",
		label: "Create Sub-Quest",
		description: "Create a sub-quest for mid-quest remarks, tangents, or follow-ups. Creates its own quest file in docs/current/<sub-quest>.md, links it into the parent quest, and records parent reference.",
		parameters: {
			type: "object",
			properties: {
				name: {
					type: "string",
					description: "Sub-quest name/slug. Optional if goal is provided.",
				},
				goal: {
					type: "string",
					description: "Goal or description of what this sub-quest will accomplish.",
				},
				parentName: {
					type: "string",
					description: "Parent quest name. Defaults to currently active quest.",
				},
				switchNow: {
					type: "boolean",
					description: "Whether to immediately switch the active session quest to this sub-quest (default: false).",
				},
			},
			required: ["goal"],
			additionalProperties: false,
		},
		execute: subquestHandler,
	});

	pi.registerTool({
		name: "quest_journal_subquest",
		label: "Create Sub-Quest (alias for quest_subquest)",
		description: "Create a sub-quest for mid-quest remarks, tangents, or follow-ups.",
		parameters: {
			type: "object",
			properties: {
				name: {
					type: "string",
					description: "Sub-quest name/slug. Optional if goal is provided.",
				},
				goal: {
					type: "string",
					description: "Goal or description of what this sub-quest will accomplish.",
				},
				parentName: {
					type: "string",
					description: "Parent quest name.",
				},
				switchNow: {
					type: "boolean",
					description: "Whether to immediately switch active quest to this sub-quest.",
				},
			},
			required: ["goal"],
			additionalProperties: false,
		},
		execute: subquestHandler,
	});
}

function installMarkTool(pi: ExtensionAPI) {
	const markHandler = async (_toolCallId: string, _params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		if (ctx) currentContext = ctx;
		markSaved(pi);
		updateUIStatus(ctx);

		const c = ctx || currentContext;
		const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
		const threshold = getEconomyThreshold();
		const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
		const warningMargin = getWarningMargin();
		const warningTokens = Math.max(0, threshold - warningMargin);

		let compactTriggered = false;
		if (state.active && threshold > 0 && tokens !== null && tokens >= warningTokens && typeof c?.compact === "function") {
			compactTriggered = true;
			c.compact({
				customInstructions: `Economy auto-compaction at ${formatTokens(tokens)} tokens (threshold: ${formatTokens(threshold)}). Focus summary on active quest '${state.active}', key architectural decisions, modified files, and immediate next steps. All deep context and research findings have been permanently persisted to disk in docs/current/${state.active}.md.`,
				onComplete: () => {
					if (c.hasUI) c.ui.notify(`Economy auto-compaction completed at ${formatTokens(tokens)} tokens.`, "info");
					updateUIStatus(c);
				},
				onError: (err: any) => {
					if (c.hasUI) c.ui.notify(`Economy auto-compaction failed: ${err?.message || err}`, "error");
				},
			});
		}

		return {
			content: [
				{
					type: "text",
					text: `Quest file marked as saved in the journal.${compactTriggered ? " Quest state updated — triggering auto-compaction now." : ""}`,
				},
			],
			details: { compacted: compactTriggered },
		};
	};

	pi.registerTool({
		name: "quest_mark_saved",
		label: "Mark Quest Saved",
		description: "Record that the active quest file has been written to disk. Call after updating the quest file.",
		parameters: { type: "object", properties: {}, additionalProperties: false },
		execute: markHandler,
	});

	pi.registerTool({
		name: "quest_journal_mark_saved",
		label: "Mark Quest Saved (alias for quest_mark_saved)",
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
		let name = "";
		let goal = "";

		const trimmed = args.trim();
		if (trimmed) {
			const spaceIdx = trimmed.indexOf(" ");
			const firstToken = spaceIdx > 0 ? trimmed.slice(0, spaceIdx) : trimmed;
			const isFirstTokenSlug = firstToken.includes("-") || firstToken.includes("_");

			const fullSlug = slugify(trimmed, 45);
			const firstSlug = slugify(firstToken, 45);

			const fullPath = questPath(fullSlug);
			const fullFuturePath = `docs/future/${fullSlug}.md`;
			const firstPath = questPath(firstSlug);
			const firstFuturePath = `docs/future/${firstSlug}.md`;

			if ((await fileExists(fullPath)) || (await fileExists(fullFuturePath))) {
				name = fullSlug;
				if (spaceIdx > 0) {
					goal = trimmed;
				}
			} else if (spaceIdx > 0 && ((await fileExists(firstPath)) || (await fileExists(firstFuturePath)))) {
				name = firstSlug;
				goal = trimmed.slice(spaceIdx + 1).trim();
			} else if (spaceIdx > 0 && isFirstTokenSlug) {
				name = firstSlug;
				goal = trimmed.slice(spaceIdx + 1).trim();
			} else {
				name = fullSlug;
				goal = trimmed;
			}
		} else {
			const choice = await promptForQuestChoice(ctx, "Which quest do you want to work on?");
			if (!choice) {
				ctx.ui.notify("No quest selected.", "warning");
				return;
			}
			name = choice.name;
			goal = choice.goal || "";
		}

		if (!name) {
			ctx.ui.notify("No quest selected.", "warning");
			return;
		}

		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(name);
		const futurePath = `docs/future/${name}.md`;
		
		if (!(await fileExists(path))) {
			if (await fileExists(futurePath)) {
				// Promote future quest to current
				await rename(futurePath, path);
				if (ctx.hasUI) ctx.ui.notify(`Promoted ${futurePath} → ${path}`, "info");
			} else {
				if (!goal && ctx.hasUI && ctx.mode === "tui") {
					goal = ((await ctx.ui.input("Describe the goal for this quest:")) ?? "").trim();
				}
				if (!goal) {
					goal = name.replace(/-/g, " ");
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
		if (!Array.isArray(state.stack)) state.stack = [];
		if (!state.stack.includes(name)) {
			state.stack.push(name);
		} else {
			const idx = state.stack.lastIndexOf(name);
			state.stack = state.stack.slice(0, idx + 1);
		}
		if (switching) state.prompts = [];
		if (goal) {
			state.prompts.push(`Goal: ${goal}`);
		}
		state.saveCount += 1;
		persist(pi, ctx);
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
		persist(pi, ctx);

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

	const questDelHandler = async (args: string, ctx: ExtensionContext) => {
		let name = slugify(args);
		if (!name) {
			const choice = await promptForQuestChoice(ctx, "Select quest to archive:");
			name = choice?.name ? slugify(choice.name) : "";
		}
		if (!name) {
			ctx.ui.notify("No quest selected for archiving.", "warning");
			return;
		}
		const res = await archiveQuestFile(name, pi, ctx);
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

	const questDraftHandler = async (args: string, ctx: ExtensionContext) => {
		let desc = args.trim();
		if (!desc && ctx.mode === "tui") {
			desc = ((await ctx.ui.input("Describe the future quest / proposal (e.g. cx ergonomics):")) ?? "").trim();
		}
		if (!desc) {
			ctx.ui.notify("Usage: /quest-draft <description>", "warning");
			return;
		}
		const name = slugify(desc);
		const currentPath = questPath(name);
		if (await fileExists(currentPath)) {
			ctx.ui.notify(`Quest '${name}' is already active/current in ${currentPath}. Cannot create a draft for an active quest.`, "warning");
			return;
		}
		await mkdir("docs/future", { recursive: true });
		const path = `docs/future/${name}.md`;
		if (!(await fileExists(path))) {
			await writeFile(path, FUTURE_QUEST_TEMPLATE(name, desc), "utf8");
			if (ctx.hasUI) ctx.ui.notify(`Created draft proposal at ${path}`, "info");
		} else {
			if (ctx.hasUI) ctx.ui.notify(`Draft already exists at ${path}`, "warning");
		}
	};

	pi.registerCommand("quest-draft", {
		description: "Draft a future quest or proposal without making it active.",
		handler: questDraftHandler,
	});

	const questEconomyHandler = async (args: string, ctx: ExtensionContext) => {
		currentContext = ctx;
		const trimmed = args.trim();
		const currentThreshold = getEconomyThreshold();
		const currentWarning = getWarningMargin();
		const usage = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
		const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
		const tokenStr = tokens !== null ? formatTokens(tokens) : "unknown";

		if (!trimmed) {
			const thresholdStr = currentThreshold > 0 ? `${formatTokens(currentThreshold)} tokens (${currentThreshold.toLocaleString()})` : "disabled";
			const warnStr = `${formatTokens(currentWarning)} tokens (${currentWarning.toLocaleString()})`;
			const effectiveWarn = currentThreshold > 0 ? `${formatTokens(Math.max(0, currentThreshold - currentWarning))}` : "N/A";
			const msg = `Quest Economy: threshold = ${thresholdStr}, pre-compact warning = ${warnStr} (warns at ${effectiveWarn}, compacts on save or at ${thresholdStr}). Current usage = ${tokenStr} tokens. Usage: /quest-economy <threshold> [warning] (e.g. /quest-economy 140k 30k, /quest-economy off)`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		if (trimmed.toLowerCase() === "default") {
			state.economyTokens = null;
			state.warningMarginTokens = null;
			persist(pi, ctx);
			const newThreshold = getEconomyThreshold();
			const newWarning = getWarningMargin();
			const msg = `Quest Economy: reset to default (threshold = ${formatTokens(newThreshold)}, warning = ${formatTokens(newWarning)}). Current usage: ${tokenStr}.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		if (trimmed.toLowerCase() === "off" || trimmed.toLowerCase() === "disable" || trimmed.toLowerCase() === "disabled" || trimmed === "0") {
			state.economyTokens = 0;
			persist(pi, ctx);
			const msg = `Quest Economy: auto-compaction disabled. Current usage: ${tokenStr}.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		const parts = trimmed.split(/\s+/);
		const parsedThreshold = parseTokenAmount(parts[0]);
		if (parsedThreshold === null || parsedThreshold <= 0) {
			if (ctx.hasUI) ctx.ui.notify(`Invalid token amount: "${parts[0]}". Examples: 140k, 140k 30k, 120000, off, default`, "warning");
			return;
		}

		state.economyTokens = parsedThreshold;
		if (parts.length > 1) {
			const parsedWarn = parseTokenAmount(parts[1], DEFAULT_PRE_COMPACT_WARNING_TOKENS);
			if (parsedWarn !== null && parsedWarn > 0) {
				state.warningMarginTokens = parsedWarn;
			}
		}
		persist(pi, ctx);

		const activeThreshold = getEconomyThreshold();
		const activeWarning = getWarningMargin();
		const msg = `Quest Economy: threshold set to ${formatTokens(activeThreshold)} tokens (${activeThreshold.toLocaleString()}), warning margin = ${formatTokens(activeWarning)} tokens. Current usage: ${tokenStr}.`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
	};

	const questWarningHandler = async (args: string, ctx: ExtensionContext) => {
		currentContext = ctx;
		const trimmed = args.trim();
		const currentWarning = getWarningMargin();
		const currentThreshold = getEconomyThreshold();

		if (!trimmed) {
			const warnStr = `${formatTokens(currentWarning)} tokens (${currentWarning.toLocaleString()})`;
			const effectiveWarn = currentThreshold > 0 ? ` (warns at ${formatTokens(Math.max(0, currentThreshold - currentWarning))})` : "";
			const msg = `Quest Pre-Compaction Warning Margin: ${warnStr}${effectiveWarn}. Usage: /quest-warning <tokens> (e.g. /quest-warning 30k, /quest-warning default)`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		if (trimmed.toLowerCase() === "default") {
			state.warningMarginTokens = null;
			persist(pi, ctx);
			const newWarning = getWarningMargin();
			const msg = `Quest Pre-Compaction Warning: reset to default (${formatTokens(newWarning)} tokens).`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		const parsed = parseTokenAmount(trimmed, DEFAULT_PRE_COMPACT_WARNING_TOKENS);
		if (parsed === null || parsed <= 0) {
			if (ctx.hasUI) ctx.ui.notify(`Invalid warning token amount: "${trimmed}". Examples: 30k, 25000, default`, "warning");
			return;
		}

		state.warningMarginTokens = parsed;
		persist(pi, ctx);
		const msg = `Quest Pre-Compaction Warning: margin set to ${formatTokens(parsed)} tokens (${parsed.toLocaleString()}).`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
	};

	const economyCompletions = async (prefix: string) => {
		const options = ["100k", "120k", "140k", "160k", "180k", "200k", "off", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	const warningCompletions = async (prefix: string) => {
		const options = ["15k", "20k", "25k", "30k", "35k", "40k", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	pi.registerCommand("quest-economy", {
		description: "Configure or check token economy auto-compaction threshold (e.g. /quest-economy 140k, /quest-economy 140k 30k, /quest-economy off).",
		getArgumentCompletions: economyCompletions,
		handler: questEconomyHandler,
	});

	pi.registerCommand("quest-warning", {
		description: "Configure pre-compaction warning margin (e.g. /quest-warning 30k).",
		getArgumentCompletions: warningCompletions,
		handler: questWarningHandler,
	});

	const questStatusHandler = async (_args: string, ctx: ExtensionContext) => {
		currentContext = ctx;
		if (!state.active) {
			if (ctx.hasUI) ctx.ui.notify("No active quest.", "info");
			return;
		}
		const path = questPath(state.active);
		const exists = await fileExists(path);
		const fresh = compactionReady();
		const hier = formatQuestHierarchy(state.active, state.stack);
		const usage = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
		const threshold = getEconomyThreshold();
		const tokens = usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
		const tokenStr = tokens !== null ? `${formatTokens(tokens)}${threshold > 0 ? `/${formatTokens(threshold)}` : ""}` : `~${Math.round(usagePercent(ctx))}%`;
		let parentInfo = "";
		let subInfo = "";

		if (exists) {
			try {
				const content = await readFile(path, "utf8");
				const parent = extractParentFromQuest(content);
				if (parent) parentInfo = ` (parent: [[${parent}]])`;
				const subQuests = extractSubQuestsFromQuest(content);
				if (subQuests.length > 0) subInfo = ` | sub-quests: ${subQuests.join(", ")}`;
			} catch {
				// ignore read error
			}
		}

		const line = exists
			? `${path}${parentInfo} [${hier}] — ${fresh ? "fresh" : "SAVE PENDING"}, tokens ${tokenStr}, prompts ${state.prompts.length}${subInfo}`
			: `${path} — MISSING on disk!`;
		if (ctx.hasUI) ctx.ui.notify(`Active quest: ${line}`, fresh ? "info" : "warning");
	};

	pi.registerCommand("quest-status", {
		description: "Show the active quest and whether its file is fresh.",
		handler: questStatusHandler,
	});

	const questsHandler = async (_args: string, ctx: ExtensionContext) => {
		const current = await listQuestFiles(QUEST_DIR);
		const future = await listQuestFiles("docs/future");

		// Build parent-child map
		const parentOf = new Map<string, string>();
		const childrenOf = new Map<string, string[]>();

		for (const f of current) {
			const slug = f.replace(/\.md$/, "");
			try {
				const content = await readFile(`${QUEST_DIR}/${f}`, "utf8");
				const p = extractParentFromQuest(content);
				if (p && current.includes(`${p}.md`)) {
					parentOf.set(slug, p);
					const list = childrenOf.get(p) || [];
					list.push(slug);
					childrenOf.set(p, list);
				}
			} catch {
				// ignore
			}
		}

		const renderedCurrent: string[] = [];
		for (const f of current) {
			const slug = f.replace(/\.md$/, "");
			if (parentOf.has(slug)) continue; // rendered under parent

			const isActive = state.active === slug;
			renderedCurrent.push(`  ${slug}${isActive ? "  ◀ active" : ""}`);
			const subs = childrenOf.get(slug) || [];
			for (const sub of subs) {
				const isSubActive = state.active === sub;
				renderedCurrent.push(`    ↳ ${sub}${isSubActive ? "  ◀ active" : ""}`);
			}
		}

		const futureRows = future.length
			? future.map((f) => `  ${f.replace(/\.md$/, "")}`)
			: ["  (none — use /quest-draft <name>)"];
		
		ctx.ui.setWidget("quest-journal", [
			`Active: ${state.active ? questPath(state.active) : "(none)"}`, 
			"",
			"Current quests:",
			...(renderedCurrent.length ? renderedCurrent : ["  (none — use /quest <name>)"]),
			"",
			"Future / Backlog quests:",
			...futureRows
		]);
	};

	pi.registerCommand("quests", {
		description: "List current and future quests.",
		handler: questsHandler,
	});

	const subquestHandler = async (args: string, ctx: ExtensionContext) => {
		let desc = args.trim();
		if (!desc && ctx.mode === "tui") {
			desc = ((await ctx.ui.input("Describe the sub-quest (e.g. handle auth edge cases):")) ?? "").trim();
		}
		if (!desc) {
			ctx.ui.notify("Usage: /subquest <description...>", "warning");
			return;
		}

		const goal = desc;
		const name = slugify(desc);

		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(name);
		const parentName = state.active || "";

		if (!(await fileExists(path))) {
			await writeFile(path, QUEST_TEMPLATE(name, goal, parentName), "utf8");
		}
		if (parentName) {
			await linkSubQuestInParent(parentName, name, goal);
		}

		pickerCancelledThisSession = false;
		if (!Array.isArray(state.stack)) state.stack = [];
		if (parentName && !state.stack.includes(parentName)) {
			state.stack.push(parentName);
		}
		if (!state.stack.includes(name)) {
			state.stack.push(name);
		} else {
			const idx = state.stack.lastIndexOf(name);
			state.stack = state.stack.slice(0, idx + 1);
		}
		state.active = name;
		state.prompts = goal ? [`Goal: ${goal}`] : [`Sub-quest: ${name}`];
		state.saveCount += 1;
		persist(pi, ctx);

		const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
		pi.sendUserMessage([
			{
				type: "text",
				text: `Now working on sub-quest **${name}**${parentName ? ` (parent: **${parentName}**)` : ""}. Sub-quest file: \`${path}\`.${goalText}

**Mandatory Upfront Research & Planning Protocol**:
1. First, discover how to build, run, and test the project (e.g. read AGENTS.md, Makefile, scripts).
2. Perform an in-depth codebase investigation for this sub-quest: inspect relevant libraries, module boundaries, data flows, and root causes of complexity. Think ambitiously about clean abstractions.
3. Formulate a comprehensive, multi-stage execution plan where each phase is self-contained with exact function signatures, touched files, and targeted test files.
4. Fill \`${path}\` completely with the goal, parent reference, findings, multi-stage plan, acceptance checklist, and next recommended step.
5. In your very first turn, present the complete analysis, architectural trade-offs, and multi-stage plan clearly to the user for confirmation.

**Mandatory TDD & Quality Workflow**:
1. Develop targeted test(s) for each stage BEFORE feature code.
2. Develop feature -> build -> run -> verify targeted tests.
3. Support end-of-task user feedback loops and polish iterations until final confirmation.
4. Final Quality Gates: zero build errors/warnings, zero debug artifacts, and full test suite passing with zero errors.`,
			},
		]);
	};

	pi.registerCommand("subquest", {
		description: "Create and switch to a sub-quest linked to the current active quest (e.g. /subquest error-handling Handle network disconnects).",
		getArgumentCompletions: questCompletions,
		handler: subquestHandler,
	});

	pi.registerCommand("sub-quest", {
		description: "Alias for /subquest.",
		getArgumentCompletions: questCompletions,
		handler: subquestHandler,
	});
}

function FUTURE_QUEST_TEMPLATE(name: string, goal = ""): string {
	return [
		`# Proposal / Future Quest: ${name}`,
		``,
		`Status: **proposal**`,
		``,
		`## Goals & Scope`,
		goal ? goal : `> What are we proposing to change and why?`,
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

function QUEST_TEMPLATE(name: string, goal = "", parent = ""): string {
	const parentSec = parent
		? `## Parent Quest\n[[${parent}]]\n`
		: `## Parent Quest\n> If this is a sub-quest, reference the parent quest here (e.g. [[parent-quest-name]]).\n`;

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
		parentSec,
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
		`## Sub-Quests`,
		`> Sub-quests, follow-ups, or tangent quests spawned from this quest.`,
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
		
		// Extract key sections: Goal, Parent Quest, Sub-Quests, Current Status, Remaining work, Next recommended step, Resume prompt
		const sections = ["Goal", "Parent Quest", "Sub-Quests", "Current Status", "Remaining work", "Next recommended step", "Resume prompt"];
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
   - **Root / Top-Level Quest Completion**: When the main/top-level quest is completed (all features, acceptance criteria, and quality gates pass), ALWAYS prompt the user via \`ask_questions\` with the following structured options:
     1. **Refine anything**: Keep active quest open to make further adjustments or address feedback.
     2. **Archive quest and auto-compact**: Call \`quest_journal_archive({ compact: true })\` to archive the quest and automatically compact session context.
     3. **Archive quest without auto-compact**: Call \`quest_journal_archive({ compact: false })\` to archive the quest while keeping the current session context intact.
     4. **Change to manual mode**: Switch workflow / compaction to manual mode.
     Execute the exact action corresponding to the user's choice.
   - **Sub-Quest Completion (Autonomous Continuation)**: When finishing a child sub-quest, you do NOT need to ask for user input. Autonomously archive the sub-quest via \`quest_archive({ compact: boolean })\`—deciding \`compact: true\` if context is elevated or \`compact: false\` if context is low—then seamlessly continue working on the parent quest.
7. **Final Verification & Quality Gates**:
   - Zero compiler errors or warnings.
   - Zero debug artifacts (no leftover console.logs, prints, or scratch code).
   - **Server / Daemon Restart**: Always ensure a fresh instance of the server / test daemon is running (e.g. restart background servers or run clean boot test) before running final tests, so tests never run against stale in-memory state.
   - Full test suite (\`make test\`) must pass with zero errors.

# Autonomous Quest Management (Zero Manual User Commands Needed)
You manage quests completely autonomously on disk in \`docs/current/<quest>.md\`. The user should NEVER need to type manual slash commands.

1. **Auto-Initialize New Quest on Any User Request**:
   - Do not ask questions to the user initially on startup. Let the user type as usual.
   - When the user describes an issue, feature, or bug they want fixed:
     - Automatically engage in deep research and brainstorming on the codebase.
     - Infer a clean, concise quest slug (e.g. \`docs/current/<inferred-slug>.md\`).
     - Immediately create/fill \`docs/current/<inferred-slug>.md\` using the quest template with the stated goal, initial analysis, TDD checklist, and verbatim request.
     - Call \`quest_mark_saved\` to activate and track the quest.
     - Present the complete analysis findings, architectural trade-offs, and multi-stage plan in your very first turn.
   - If the user mentions continuing or resuming a quest without specifying which one:
     - Check current quests in \`docs/current/\` and prompt the user via \`ask_questions\` with the available choices.

2. **Auto-Refine Active Quest on User Feedback**:
   - When the user provides feedback, tweaks, or new requirements mid-quest or post-implementation:
     - Immediately read and update \`docs/current/<active-quest>.md\`.
     - Log the feedback under \`## Quest Refinements & User Feedback Loops\`.
     - Update \`## Remaining work\`, \`## Acceptance Criteria & Polish Checklist\`, \`## Decisions made\`, and \`## Next recommended step\`.
     - Call \`quest_journal_mark_saved\`.

3. **Auto-Create Sub-Quests for Tangents or Follow-ups (LIFO Stack)**:
   - Quests operate on a **LIFO (Last-In, First-Out) stack**.
   - When user remarks, tangents, or follow-ups arise during a quest, call \`quest_subquest({ goal: "..." })\` immediately (pushes the sub-quest onto the stack).
   - Link it under \`## Sub-Quests\` in the parent quest file.
   - When the sub-quest is finished and archived with \`quest_archive\`, it automatically pops from the stack and seamlessly returns to the parent quest!

4. **Auto-Archive Upon Completion (LIFO Pop)**:
   - **For Sub-Quests**: Do NOT prompt the user with interactive modal questions. Autonomously archive the sub-quest via \`quest_archive({ compact: boolean })\` (choosing \`compact: true\` if context is high or \`compact: false\` if context is low) and immediately continue parent quest execution.
   - **For Root / Top-Level Quests**: Prompt the user via \`ask_questions\` (Refine, Archive with auto-compact, Archive without auto-compact, Manual mode) and execute \`quest_archive\` based on their choice. If returning to a parent quest on the LIFO stack, resume the parent quest cleanly.

# Economy Auto-Compaction & Zero Re-Research Protocol
Context automatically compacts every ~140k tokens (or configured economy threshold) to preserve speed, minimize cost, and prevent attention drift.
1. **30k Pre-Compaction Warning Window**: When context reaches ~30k tokens before the limit (~110k for a 140k target), you will receive an explicit alert that auto-compaction is imminent.
2. **Exhaustive Context Dump Before Compaction**: Upon receiving this alert, you MUST immediately update \`docs/current/<quest>.md\` with an exhaustive dump of all context knowledge, discoveries, architecture decisions, modified files, data structures, and a comprehensive resume prompt.
3. **Auto-Compaction After Update**: After updating the quest files, context compaction will immediately trigger to reset working memory.
4. **No Re-Research Across Compactions**: The quest file in \`docs/current/<quest>.md\` is the single source of truth on disk. After compaction, read \`docs/current/<quest>.md\` immediately to resume execution seamlessly with ZERO re-research.
5. **Faithful User Request**: The \`## Original request\` section MUST remain verbatim.${resumeContext}`;

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
			if (set.has("quest_journal_mark_saved") || state.active) {
				return [
					"Call `quest_journal_mark_saved` after updating active quest files.",
					"For sub-quests, complete and archive autonomously (`quest_archive({ compact: boolean })`) without asking modal questions.",
					"When completing a top-level quest, prompt via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
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
		await syncActiveQuestFromDisk(pi, ctx);
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
	installSubQuestTool(pi);
	installCommands(pi);
	installShutdownSave(pi);

	// Durable in-session marker, not sent to the LLM.
	const renderEntry = (entry: any, _o: any, theme: any) => {
		const data = entry.data ?? ({} as StoredState);
		const fresh = data.saveCount > data.compactCount;
		const hier = formatQuestHierarchy(data.active, data.stack);
		return new Text(
			`${theme.fg("accent", "✨ ")}${theme.fg("muted", "quest:")} ${hier}${
				fresh ? "" : theme.fg("warning", " (save pending)")
			}`,
			0,
			0,
		);
	};
	pi.registerEntryRenderer<StoredState>(CUSTOM_TYPE, renderEntry);
	pi.registerEntryRenderer<StoredState>(LEGACY_CUSTOM_TYPE, renderEntry);
}
