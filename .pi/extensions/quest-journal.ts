/**
 * Quest Journal & Context Awareness -- unified extension for quest persistence,
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
 *   *enforces*, and *detects staleness* -- it never fabricates plan content.
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
 *     compaction -- guarantees persistence before context is lost. Save request
 *     explicitly requires ## Original request to be faithful.
 *   - `session_compact`: record that a compaction happened (next save gate).
 *   - `tool_result` (edit/write on the active file): counts as a save.
 *   - `session_before_switch`: remind to persist before leaving so a parallel
 *     quest is also captured.
 */

import { AsyncLocalStorage } from "node:async_hooks";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { mkdir, readFile, readdir, rename, unlink, writeFile } from "node:fs/promises";
import { basename } from "node:path";

const ANSI_SGR = /\u001B\[[0-9;]*m/g;
const displayWidth = (s: string): number => {
	let w = 0;
	for (const ch of s.replace(ANSI_SGR, "")) {
		w += ch.codePointAt(0)! > 0x7f ? 2 : 1;
	}
	return w;
};

class Text {
	constructor(public text: string, public x: number = 0, public y: number = 0) {}

	render(width: number): string[] {
		if (!this.text || this.text.trim() === "") return [];
		const visible = displayWidth(this.text);
		if (visible < width) {
			return [this.text + " ".repeat(width - visible)];
		}
		let out = "";
		let w = 0;
		for (const ch of this.text.replace(ANSI_SGR, "")) {
			const cw = ch.codePointAt(0)! > 0x7f ? 2 : 1;
			if (w + cw > width) break;
			out += ch;
			w += cw;
		}
		return [out];
	}
}

export interface ExtensionAPI {
	on(event: string, handler: (event: any, ctx: any) => Promise<any> | any): void;
	appendEntry<T = any>(type: string, data: T): void;
	registerEntryRenderer<T = any>(type: string, renderer: (entry: any, o: any, theme: any) => any): void;
	registerTool(tool: any): void;
	registerCommand(name: string, command: any): void;
	sendUserMessage(msg: any, options?: any): void;
}

export interface ExtensionContext {
	cwd: string;
	sessionManager: any;
	ui: any;
	hasUI?: boolean;
	mode?: string;
	isIdle?: () => boolean;
	getContextUsage?: () => { tokens?: number | null; contextWindow?: number | null; percent?: number | null } | undefined;
	compact?: (options: any) => void;
}

const QUEST_DIR = "docs/current";
const FUTURE_DIR = "docs/future";
const ARCHIVE_DIR = "docs/archive";
const NOTES_FILE = ".pi/context.md";
const CUSTOM_TYPE = "quest_journal";
const LEGACY_CUSTOM_TYPE = "task_journal";
const MIN_PROMPT_MS = 45_000; // never nag more often than this
const SAVE_PERCENT = 70; // context-usage % that escalates the reminder
const COMPACT_WARN_PERCENT = 85; // instruct save + compact when this close to auto-compact
const PROMPT_MAX_CHARS = 4000;
const PROMPT_MAX_COUNT = 10;
const DEFAULT_CEILING_TOKENS = 200_000; // 200K default ceiling
const DEFAULT_PERCENT = 80; // 80% default context percentage
const DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS = 60_000; // 60K default subquest launch compaction threshold
const DEFAULT_PRE_COMPACT_WARNING_TOKENS = 30_000; // 30K default pre-compaction warning margin

interface SaveGeneration {
	count: number;
	path: string;
	hash: string;
	savedAt: number;
}

interface StoredState {
	active: string | null;
	saveCount: number;
	compactCount: number;
	prompts: string[];
	stack: string[];
	dirty?: boolean;
	compactionPending?: boolean;
	saveGeneration?: SaveGeneration | null;
	lastSavedHash?: string | null;
	economyTokens?: number | null;
	economyPercent?: number | null;
	warningMarginTokens?: number | null;
	subquestCompactTokens?: number | null;
	lastWarnedCompactionTokens?: number | null;
}

const sessionStates = new Map<string, StoredState>();
const asyncContext = new AsyncLocalStorage<ExtensionContext>();

function getActiveContext(ctx?: ExtensionContext): ExtensionContext | undefined {
	return ctx || asyncContext.getStore();
}

function createDefaultState(): StoredState {
	return {
		active: null,
		saveCount: 0,
		compactCount: 0,
		prompts: [],
		stack: [],
		dirty: false,
		saveGeneration: null,
		lastSavedHash: null,
		economyTokens: undefined,
		economyPercent: undefined,
		warningMarginTokens: undefined,
		subquestCompactTokens: undefined,
		lastWarnedCompactionTokens: undefined,
	};
}

function getSessionId(ctx?: ExtensionContext): string {
	const c = getActiveContext(ctx);
	if (!c) return "default";
	const sm = c.sessionManager;
	const id = sm?.id || sm?.sessionId || (typeof sm?.getSessionId === "function" ? sm.getSessionId() : null) || (c as any).sessionId;
	return id && typeof id === "string" ? id : "default";
}

function getState(ctx?: ExtensionContext): StoredState {
	const c = getActiveContext(ctx);
	const id = getSessionId(c);
	let s = sessionStates.get(id);
	if (!s) {
		s = createDefaultState();
		sessionStates.set(id, s);
	}
	return s;
}

function setSessionState(ctx: ExtensionContext | undefined, newState: StoredState) {
	const c = getActiveContext(ctx);
	const id = getSessionId(c);
	sessionStates.set(id, newState);
}

// Proxied state object providing transparent session-scoped access
const state = new Proxy({} as StoredState, {
	get(_target, prop: string) {
		const s = getState();
		return (s as any)[prop];
	},
	set(_target, prop: string, value: any) {
		const s = getState();
		(s as any)[prop] = value;
		return true;
	},
});

let lastPromptAt = Date.now();
let pickerCancelledThisSession = false;

/** Wrap async handlers with session AsyncLocalStorage context */
function withContext<T extends (...args: any[]) => any>(fn: T): T {
	return ((...args: any[]) => {
		let ctx: ExtensionContext | undefined;
		for (const arg of args) {
			if (arg && typeof arg === "object" && ("sessionManager" in arg || "cwd" in arg || "ui" in arg)) {
				ctx = arg as ExtensionContext;
				break;
			}
		}
		if (ctx) {
			return asyncContext.run(ctx, () => fn(...args));
		}
		return fn(...args);
	}) as T;
}

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

function parsePercentage(val: unknown): number | null {
	if (typeof val === "number" && !Number.isNaN(val)) {
		if (val > 0 && val <= 1.0) return Math.round(val * 100);
		if (val > 1 && val <= 100) return Math.round(val);
		return null;
	}
	if (typeof val !== "string") return null;
	const s = val.trim().toLowerCase();
	if (!s) return null;
	const match = s.match(/^([\d.]+)\s*%$/);
	if (match) {
		const num = Number.parseFloat(match[1]);
		if (!Number.isNaN(num) && num > 0 && num <= 100) {
			return Math.round(num);
		}
	}
	return null;
}

function parseTokenAmount(val: unknown, defaultVal: number = DEFAULT_CEILING_TOKENS): number | null {
	if (typeof val === "number" && !Number.isNaN(val)) {
		return val > 0 ? Math.round(val) : 0;
	}
	if (typeof val !== "string") return null;
	const s = val.trim().toLowerCase();
	if (!s) return null;
	if (s === "off" || s === "disable" || s === "disabled" || s === "0") return 0;
	if (s === "default") return defaultVal;

	// Matches "333k", "333 k", "1.5m", "333000", "333000 tokens", "333,000"
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

function calculateCurrentTokens(ctx?: ExtensionContext): number | null {
	const c = getActiveContext(ctx);
	const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
	return usage?.tokens ?? (usage?.percent && usage?.contextWindow ? Math.round((usage.percent * usage.contextWindow) / 100) : null);
}

function readSettingsEconomyThreshold(): { tokens?: number | null; percent?: number | null } | null {
	for (const p of [".pi/settings.json", "~/.pi/agent/settings.json"]) {
		try {
			const resolved = p.startsWith("~") ? p.replace(/^~/, process.env.HOME || "") : p;
			const raw = readFileSync(resolved, "utf8");
			const json = JSON.parse(raw);
			const val =
				json?.questJournal?.economyTokens ??
				json?.questJournal?.autoCompactTokens ??
				json?.questJournal?.economyPercent ??
				json?.compaction?.economyTokens;
			if (val !== undefined && val !== null) {
				const pct = parsePercentage(val);
				if (pct !== null) return { percent: pct };
				const tokens = parseTokenAmount(val);
				if (tokens !== null) return { tokens };
			}
		} catch (err: any) {
			logError(`Failed reading economy threshold from ${p}`, err);
		}
	}
	return null;
}

function getEconomyThreshold(ctx?: ExtensionContext): number {
	const c = getActiveContext(ctx);
	const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
	const contextWindow = usage?.contextWindow ?? 0;
	const maxByWindow = contextWindow > 0 ? Math.round(contextWindow * (DEFAULT_PERCENT / 100)) : DEFAULT_CEILING_TOKENS;

	// 1. Explicit state overrides
	if (typeof state.economyPercent === "number" && state.economyPercent > 0) {
		const val = contextWindow > 0 ? Math.round((contextWindow * state.economyPercent) / 100) : DEFAULT_CEILING_TOKENS;
		return Math.min(val, DEFAULT_CEILING_TOKENS);
	}
	if (typeof state.economyTokens === "number") {
		return Math.min(state.economyTokens, maxByWindow);
	}

	// 2. Environment variables
	const envVal = process.env.PI_QUEST_AUTO_COMPACT_TOKENS ?? process.env.QUEST_AUTO_COMPACT_TOKENS;
	if (envVal) {
		const envPct = parsePercentage(envVal);
		if (envPct !== null && envPct > 0) {
			const val = contextWindow > 0 ? Math.round((contextWindow * envPct) / 100) : DEFAULT_CEILING_TOKENS;
			return Math.min(val, DEFAULT_CEILING_TOKENS);
		}
		const parsedEnvTokens = parseTokenAmount(envVal);
		if (parsedEnvTokens !== null) return Math.min(parsedEnvTokens, maxByWindow);
	}

	// 3. Settings file
	const settingsConfig = readSettingsEconomyThreshold();
	if (settingsConfig) {
		if (typeof settingsConfig.percent === "number" && settingsConfig.percent > 0) {
			const val = contextWindow > 0 ? Math.round((contextWindow * settingsConfig.percent) / 100) : DEFAULT_CEILING_TOKENS;
			return Math.min(val, DEFAULT_CEILING_TOKENS);
		}
		if (typeof settingsConfig.tokens === "number") {
			return Math.min(settingsConfig.tokens, maxByWindow);
		}
	}

	// 4. Default: 80% of context window, capped at DEFAULT_CEILING_TOKENS (200k)
	if (contextWindow > 0) {
		const pctValue = Math.round(contextWindow * (DEFAULT_PERCENT / 100));
		return Math.min(pctValue, DEFAULT_CEILING_TOKENS);
	}

	return DEFAULT_CEILING_TOKENS;
}

function readSettingsSubquestThreshold(): number | null {
	for (const p of [".pi/settings.json", "~/.pi/agent/settings.json"]) {
		try {
			const resolved = p.startsWith("~") ? p.replace(/^~/, process.env.HOME || "") : p;
			const raw = readFileSync(resolved, "utf8");
			const json = JSON.parse(raw);
			const val =
				json?.questJournal?.subquestCompactTokens ??
				json?.questJournal?.subquestThreshold ??
				json?.compaction?.subquestTokens;
			const parsed = parseTokenAmount(val, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
			if (parsed !== null) return parsed;
		} catch (err: any) {
			logError(`Failed reading subquest threshold from ${p}`, err);
		}
	}
	return null;
}

function getSubquestCompactThreshold(): number {
	if (typeof state.subquestCompactTokens === "number") {
		return state.subquestCompactTokens;
	}
	const envVal = process.env.PI_QUEST_SUBQUEST_COMPACT_TOKENS ?? process.env.QUEST_SUBQUEST_COMPACT_TOKENS;
	const parsedEnv = parseTokenAmount(envVal, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
	if (parsedEnv !== null) return parsedEnv;

	const parsedSettings = readSettingsSubquestThreshold();
	if (parsedSettings !== null) return parsedSettings;

	return DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS;
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
		} catch (err: any) {
			logError(`Failed reading warning margin from ${p}`, err);
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

function getCompactionInstructions(activeQuest: string, tokens: number | null, threshold: number): string {
	const isSubQuest = Array.isArray(state.stack) && state.stack.length > 1;
	const parentName = isSubQuest ? state.stack[state.stack.length - 2] : null;
	const tokenLabel = tokens !== null ? ` at ${formatTokens(tokens)} tokens (threshold: ${formatTokens(threshold)})` : "";

	if (isSubQuest && parentName) {
		return `Economy auto-compaction${tokenLabel} during sub-quest '${activeQuest}' (parent: '${parentName}'). Focus summary on active sub-quest progress, key architectural decisions, modified files, and immediate sub-quest next steps. Parent quest state is safely preserved on disk in docs/current/${parentName}.md. Following compaction, autonomously read docs/current/${activeQuest}.md and proceed with the next step with zero re-research.`;
	}

	return `Economy auto-compaction${tokenLabel}. Focus summary on active quest '${activeQuest}', key architectural decisions, modified files, and immediate next steps. All deep context and research findings have been permanently persisted to disk in docs/current/${activeQuest}.md. Following compaction, autonomously read docs/current/${activeQuest}.md and proceed with the next step with zero re-research.`;
}

const MAX_QUEST_NAME_DISPLAY_LENGTH = 24;

function truncateQuestName(name: string, maxLen = MAX_QUEST_NAME_DISPLAY_LENGTH): string {
	if (!name) return "";
	if (name.length <= maxLen) return name;
	return name.slice(0, Math.max(1, maxLen - 1)) + "…";
}

function formatQuestHierarchy(active: string | null, stack?: string[], maxNameLength = MAX_QUEST_NAME_DISPLAY_LENGTH): string {
	if (!active) return "(none)";
	if (!stack || stack.length === 0) return truncateQuestName(active, maxNameLength);

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

	const depth = cleanStack.length;
	const activeTruncated = truncateQuestName(active, maxNameLength);

	if (depth <= 1) return activeTruncated;
	// In a sub-quest: show only depth then name (e.g. "d2: my-sub-quest", "d3: child-sub-quest")
	return `d${depth}: ${activeTruncated}`;
}

// ---------------------------------------------------------------------------
// State persistence (custom entries survive reloads and branching)
// ---------------------------------------------------------------------------

function persist(pi: ExtensionAPI, ctx?: ExtensionContext) {
	try {
		pi.appendEntry<StoredState>(CUSTOM_TYPE, state);
	} catch {
		// ephemeral / unsupported session: stay in-memory only
	}
	updateUIStatus(ctx);
}

/** Update the persistent status bar above the prompt box. */
function updateUIStatus(ctx?: ExtensionContext) {
	const c = getActiveContext(ctx);
	if (c?.hasUI) {
		const fresh = compactionReady();
		const hier = formatQuestHierarchy(state.active, state.stack);
		const threshold = getEconomyThreshold(c);

		let tokenInfo = "";
		const tokens = calculateCurrentTokens(c);

		if (tokens !== null && tokens > 0) {
			if (threshold > 0) {
				tokenInfo = ` [${formatTokens(tokens)}/${formatTokens(threshold)}]`;
			} else {
				tokenInfo = ` [${formatTokens(tokens)}]`;
			}
		} else {
			const usage = typeof c.getContextUsage === "function" ? c.getContextUsage() : undefined;
			if (typeof usage?.percent === "number" && usage.percent > 0) {
				tokenInfo = ` [${Math.round(usage.percent)}%]`;
			}
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
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && (entry.customType === CUSTOM_TYPE || entry.customType === LEGACY_CUSTOM_TYPE) && entry.data) {
			latest = entry.data as unknown as StoredState;
		}
	}
	const reconstructedState: StoredState = latest && latest.active
		? {
				active: latest.active,
				saveCount: latest.saveCount || 0,
				compactCount: latest.compactCount || 0,
				prompts: Array.isArray(latest.prompts) ? latest.prompts : [],
				stack: Array.isArray(latest.stack) ? latest.stack : (latest.active ? [latest.active] : []),
				dirty: typeof latest.dirty === "boolean" ? latest.dirty : false,
				compactionPending: false,
				saveGeneration: latest.saveGeneration || null,
				lastSavedHash: latest.lastSavedHash || null,
				economyTokens: typeof latest.economyTokens === "number" ? latest.economyTokens : undefined,
				economyPercent: typeof latest.economyPercent === "number" ? latest.economyPercent : undefined,
				warningMarginTokens: typeof latest.warningMarginTokens === "number" ? latest.warningMarginTokens : undefined,
				subquestCompactTokens: typeof latest.subquestCompactTokens === "number" ? latest.subquestCompactTokens : undefined,
				lastWarnedCompactionTokens: typeof latest.lastWarnedCompactionTokens === "number" ? latest.lastWarnedCompactionTokens : undefined,
		  }
		: createDefaultState();
	setSessionState(ctx, reconstructedState);
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

interface MarkdownSection {
	heading: string;
	normalized: string;
	level: number;
	body: string;
	raw: string;
}

function parseMarkdownSections(content: string): Map<string, MarkdownSection> {
	const sections = new Map<string, MarkdownSection>();
	if (!content) return sections;

	const lines = content.split(/\r?\n/);
	let currentHeading: string | null = null;
	let currentLevel = 0;
	let currentBodyLines: string[] = [];
	let inCodeBlock = false;

	const flush = () => {
		if (currentHeading !== null) {
			const norm = currentHeading.trim().toLowerCase();
			const body = currentBodyLines.join("\n").trim();
			sections.set(norm, {
				heading: currentHeading,
				normalized: norm,
				level: currentLevel,
				body,
				raw: `## ${currentHeading}\n${body}`,
			});
		}
		currentBodyLines = [];
	};

	for (const line of lines) {
		if (/^\s*(```|~~~)/.test(line)) {
			inCodeBlock = !inCodeBlock;
			currentBodyLines.push(line);
			continue;
		}

		const headingMatch = line.match(/^(#{1,6})\s+(.+)$/);
		if (headingMatch && !inCodeBlock) {
			flush();
			currentLevel = headingMatch[1].length;
			currentHeading = headingMatch[2].trim();
		} else {
			currentBodyLines.push(line);
		}
	}
	flush();

	return sections;
}

function logError(msg: string, err?: any, ctx?: ExtensionContext) {
	const errMsg = err?.message ? `${msg}: ${err.message}` : msg;
	console.error(`[QuestJournal] ${errMsg}`);
	if (ctx?.hasUI && typeof ctx.ui?.notify === "function") {
		ctx.ui.notify(`Quest Journal Error: ${errMsg}`, "error");
	}
}

/** Clean up matching proposal draft in docs/future/ if it exists. */
async function cleanDraftIfExists(slug: string, ctx?: ExtensionContext) {
	const futurePath = `${FUTURE_DIR}/${slug}.md`;
	if (await fileExists(futurePath)) {
		try {
			await unlink(futurePath);
		} catch (err: any) {
			logError(`Failed to clean draft at ${futurePath}`, err, ctx);
		}
	}
}

/** Link a child sub-quest into the parent quest markdown file under ## Sub-Quests. */
async function linkSubQuestInParent(parentSlug: string, childSlug: string, description = "", ctx?: ExtensionContext): Promise<boolean> {
	if (!parentSlug || !childSlug || parentSlug === childSlug) return false;
	const currentPath = questPath(parentSlug);
	const futurePath = `${FUTURE_DIR}/${parentSlug}.md`;
	let targetPath = (await fileExists(currentPath)) ? currentPath : (await fileExists(futurePath)) ? futurePath : null;
	if (!targetPath) {
		await mkdir(QUEST_DIR, { recursive: true });
		targetPath = currentPath;
		await writeFile(targetPath, QUEST_TEMPLATE(parentSlug, ""), "utf8");
	}

	try {
		let content = await readFile(targetPath, "utf8");
		const linkEntry = description ? `- [ ] [[${childSlug}]] - ${description}` : `- [ ] [[${childSlug}]]`;

		// If child already referenced in the file, don't duplicate
		if (content.includes(`[[${childSlug}]]`)) return true;

		const sections = parseMarkdownSections(content);
		const subSec = sections.get("sub-quests") || sections.get("subquests") || sections.get("sub quests");

		if (subSec) {
			const cleanedBody = subSec.body.replace(/- \[\s*\]\s*(\n|$)/g, "").trimEnd();
			const newSectionBody = cleanedBody ? `${cleanedBody}\n${linkEntry}` : `> Sub-quests, follow-ups, or tangent quests spawned from this quest.\n${linkEntry}`;
			const regex = new RegExp(`(##\\s+${subSec.heading}\\s*\\n)([\\s\\S]*?)(?=\\n##\\s+|$)`, "i");
			content = content.replace(regex, `$1${newSectionBody}\n\n`);
		} else {
			const insertBeforeRegex = /\n(##\s+(?:Why this matters|Decisions made|Constraints & Rules|Remaining work))/i;
			const newSection = `\n## Sub-Quests\n> Sub-quests, follow-ups, or tangent quests spawned from this quest.\n${linkEntry}\n`;
			if (insertBeforeRegex.test(content)) {
				content = content.replace(insertBeforeRegex, `${newSection}\n$1`);
			} else {
				content = `${content.trimEnd()}\n${newSection}\n`;
			}
		}

		await writeFile(targetPath, content, "utf8");
		return true;
	} catch (err: any) {
		logError(`Failed to link sub-quest in ${targetPath}`, err, ctx);
		return false;
	}
}

/** Extract parent quest slug from quest markdown file, if present. */
function extractParentFromQuest(content: string): string | null {
	const sections = parseMarkdownSections(content);
	const parentSec = sections.get("parent quest") || sections.get("parent") || sections.get("parentquest");
	if (!parentSec || !parentSec.body) return null;

	const wikilinkMatch = parentSec.body.match(/\[\[([^\]]+)\]\]/);
	if (wikilinkMatch && wikilinkMatch[1]) {
		return slugify(wikilinkMatch[1]);
	}
	const cleanLines = parentSec.body.split(/\r?\n/).map((l) => l.replace(/^>\s*/, "").trim()).filter(Boolean);
	if (cleanLines.length > 0) {
		const token = cleanLines[0].replace(/^-\s*\[[ x]\]\s*/, "").replace(/^-\s*/, "").trim();
		return token ? slugify(token) : null;
	}
	return null;
}

/** Extract sub-quests list from quest markdown file. */
function extractSubQuestsFromQuest(content: string): string[] {
	const sections = parseMarkdownSections(content);
	const subSec = sections.get("sub-quests") || sections.get("subquests") || sections.get("sub quests");
	if (!subSec || !subSec.body) return [];

	const results: string[] = [];
	const linkRegex = /\[\[([^\]]+)\]\]/g;
	let m: RegExpExecArray | null;
	while ((m = linkRegex.exec(subSec.body)) !== null) {
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

interface MarkdownBlock {
	type: "preamble" | "section";
	heading?: string;
	title?: string;
	normalizedTitle?: string;
	body: string;
	raw: string;
}

function parseMarkdownBlocks(content: string): MarkdownBlock[] {
	const blocks: MarkdownBlock[] = [];
	if (!content) return blocks;

	const lines = content.split(/\r?\n/);
	const currentPreambleLines: string[] = [];
	let currentHeading: string | null = null;
	let currentTitle: string | null = null;
	let currentBodyLines: string[] = [];
	let hasSeenFirstSection = false;
	let inCodeBlock = false;

	const flush = () => {
		if (!hasSeenFirstSection) {
			if (currentPreambleLines.length > 0) {
				const raw = currentPreambleLines.join("\n");
				blocks.push({
					type: "preamble",
					body: raw,
					raw,
				});
			}
		} else if (currentHeading !== null && currentTitle !== null) {
			const body = currentBodyLines.join("\n");
			blocks.push({
				type: "section",
				heading: currentHeading,
				title: currentTitle,
				normalizedTitle: currentTitle.trim().toLowerCase(),
				body,
				raw: `${currentHeading}\n${body}`,
			});
		}
		currentBodyLines = [];
	};

	for (const line of lines) {
		if (/^\s*(```|~~~)/.test(line)) {
			inCodeBlock = !inCodeBlock;
			if (!hasSeenFirstSection) {
				currentPreambleLines.push(line);
			} else {
				currentBodyLines.push(line);
			}
			continue;
		}

		const match = line.match(/^(##\s+)(.+)$/);
		if (match && !inCodeBlock) {
			flush();
			hasSeenFirstSection = true;
			currentHeading = line;
			currentTitle = match[2].trim();
		} else if (!hasSeenFirstSection) {
			currentPreambleLines.push(line);
		} else {
			currentBodyLines.push(line);
		}
	}
	flush();

	return blocks;
}

const SECTION_ALIASES: Record<string, string[]> = {
	"goal": ["goal", "goals", "goals & scope"],
	"original request": ["original request", "original user request", "request"],
	"current status": ["current status", "status"],
	"in-depth analysis & findings": ["in-depth analysis & findings", "findings", "analysis & findings", "research / findings", "research & findings"],
	"decisions made": ["decisions made", "decisions"],
	"files touched": ["files touched", "touched files", "files modified", "files"],
	"remaining work": ["remaining work", "remaining tasks", "remaining", "checklist"],
	"next recommended step": ["next recommended step", "next step", "next steps"],
};

function matchCanonicalKey(normalizedTitle: string): string | null {
	for (const [canonical, aliases] of Object.entries(SECTION_ALIASES)) {
		if (canonical === normalizedTitle || aliases.includes(normalizedTitle)) {
			return canonical;
		}
	}
	return null;
}

function spliceMarkdownSections(originalContent: string, updates: Map<string, string>): string {
	const blocks = parseMarkdownBlocks(originalContent);
	if (blocks.length === 0) return "";

	const usedCanonicalKeys = new Set<string>();
	const renderedBlocks: string[] = [];

	for (const block of blocks) {
		if (block.type === "preamble") {
			renderedBlocks.push(block.body.trimEnd());
			continue;
		}

		const canonKey = block.normalizedTitle ? matchCanonicalKey(block.normalizedTitle) : null;
		if (canonKey && updates.has(canonKey)) {
			usedCanonicalKeys.add(canonKey);
			const newBody = updates.get(canonKey)!.trim();
			renderedBlocks.push(`${block.heading}\n${newBody}`);
		} else if (block.normalizedTitle && updates.has(block.normalizedTitle)) {
			usedCanonicalKeys.add(block.normalizedTitle);
			const newBody = updates.get(block.normalizedTitle)!.trim();
			renderedBlocks.push(`${block.heading}\n${newBody}`);
		} else {
			// 100% PRESERVE unmanaged section exactly as-is!
			renderedBlocks.push(`${block.heading}\n${block.body.trim()}`);
		}
	}

	// Any requested updates that were not present in the original document:
	const uninsertedKeys = Array.from(updates.keys()).filter(
		(k) => !usedCanonicalKeys.has(k) && !usedCanonicalKeys.has(matchCanonicalKey(k) || "")
	);
	if (uninsertedKeys.length > 0) {
		const newSections: string[] = [];
		for (const key of uninsertedKeys) {
			const val = updates.get(key)!.trim();
			if (!val) continue;
			const title = key
				.split(" ")
				.map((w) => w.charAt(0).toUpperCase() + w.slice(1))
				.join(" ");
			newSections.push(`## ${title}\n${val}`);
		}

		if (newSections.length > 0) {
			const insertIdx = renderedBlocks.findIndex(
				(b) =>
					b.startsWith("## Remaining work") ||
					b.startsWith("## Next recommended step") ||
					b.startsWith("## Resume prompt")
			);
			if (insertIdx >= 0) {
				renderedBlocks.splice(insertIdx, 0, ...newSections);
			} else {
				renderedBlocks.push(...newSections);
			}
		}
	}

	return renderedBlocks.filter(Boolean).join("\n\n") + "\n";
}

function computeContentHash(content: string): string {
	return createHash("sha256").update(content, "utf8").digest("hex").slice(0, 16);
}

async function computeFileFingerprint(p: string): Promise<{ hash: string; size: number } | null> {
	try {
		const content = await readFile(p, "utf8");
		return {
			hash: computeContentHash(content),
			size: new TextEncoder().encode(content).length,
		};
	} catch {
		return null;
	}
}

async function verifyAndMarkSaved(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	expectedSlug?: string,
): Promise<{ success: boolean; hash?: string; count: number; error?: string }> {
	const targetSlug = expectedSlug || state.active;
	if (!targetSlug) {
		return { success: false, count: state.saveCount, error: "No active quest is set." };
	}
	const p = questPath(targetSlug);
	const fp = await computeFileFingerprint(p);
	if (!fp) {
		return {
			success: false,
			count: state.saveCount,
			error: `Quest file not found or unreadable at \`${p}\`. Ensure the file is written to disk before marking as saved.`,
		};
	}

	state.saveCount += 1;
	state.lastSavedHash = fp.hash;
	state.saveGeneration = {
		count: state.saveCount,
		path: p,
		hash: fp.hash,
		savedAt: Date.now(),
	};
	state.dirty = false;
	lastPromptAt = Date.now();
	persist(pi, ctx);
	updateUIStatus(ctx);

	return { success: true, hash: fp.hash, count: state.saveCount };
}

export enum QuestLifecycleState {
	IDLE = "IDLE",
	ACTIVE_CLEAN = "ACTIVE_CLEAN",
	ACTIVE_DIRTY = "ACTIVE_DIRTY",
	PRE_COMPACT_DUMP_PENDING = "PRE_COMPACT_DUMP_PENDING",
	COMPACTING = "COMPACTING",
}

function getLifecycleState(ctx?: ExtensionContext): QuestLifecycleState {
	if (!state.active) return QuestLifecycleState.IDLE;
	if (state.compactionPending) return QuestLifecycleState.COMPACTING;

	const c = getActiveContext(ctx);
	if (c) {
		const threshold = getEconomyThreshold(c);
		const tokens = calculateCurrentTokens(c);
		const warningMargin = getWarningMargin();
		if (threshold > 0 && tokens !== null && tokens >= Math.max(0, threshold - warningMargin) && !compactionReady()) {
			return QuestLifecycleState.PRE_COMPACT_DUMP_PENDING;
		}
	}

	if (state.dirty || !compactionReady()) {
		return QuestLifecycleState.ACTIVE_DIRTY;
	}
	return QuestLifecycleState.ACTIVE_CLEAN;
}

function checkAndTriggerEconomyCompaction(pi: ExtensionAPI, ctx?: ExtensionContext, reason = "economy"): boolean {
	const c = getActiveContext(ctx);
	if (!c) return false;
	if (!state.active) return false;
	if (state.compactionPending) return false;
	if (!compactionReady()) return false;
	const compactFn = c.compact;
	if (typeof compactFn !== "function") return false;

	const threshold = getEconomyThreshold(c);
	const tokens = calculateCurrentTokens(c);

	if (threshold > 0 && tokens !== null && tokens >= threshold) {
		const activeSlug = state.active || "";
		const targetSessionId = getSessionId(c);
		state.compactionPending = true;
		setTimeout(() => {
			asyncContext.run(c, () => {
				const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
				try {
					compactFn({
						customInstructions: getCompactionInstructions(activeSlug, tokens, threshold),
						onComplete: () => {
							sessionState.compactionPending = false;
							if (c.hasUI) c.ui.notify(`Economy auto-compaction completed at ${formatTokens(tokens)} tokens.`, "info");
							updateUIStatus(c);
						},
						onError: (err: any) => {
							sessionState.compactionPending = false;
							const msg = err?.message || String(err);
							if (msg.includes("Nothing to compact") || msg.includes("Already compacted") || msg.includes("cancelled") || msg.includes("session too small")) {
								sessionState.compactCount = sessionState.saveCount;
								return;
							}
							if (c.hasUI) c.ui.notify(`Economy auto-compaction failed: ${msg}`, "error");
						},
					});
				} catch (err: any) {
					sessionState.compactionPending = false;
					logError("Economy compaction scheduling failed", err, c);
				}
			});
		}, 50);
		lastPromptAt = Date.now();
		return true;
	}

	return false;
}

/** A fresh save of the quest file -- increments the save gate so compaction may run. */
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
	if (!state.prompts || state.prompts.length === 0) return "(none captured yet -- this is the first substantive request; use the current user message)";
	return state.prompts.map((p, i) => `${i + 1}. ${p}`).join("\n\n---\n\n");
}

function safeSendUserMessage(pi: ExtensionAPI, text: string, options?: { deliverAs?: "steer" | "followUp" | "nextTurn"; expandPromptTemplates?: boolean }) {
	try {
		if (options) {
			pi.sendUserMessage(text, options);
		} else {
			pi.sendUserMessage(text);
		}
	} catch (err: any) {
		try {
			pi.sendUserMessage(text, { deliverAs: "followUp" });
		} catch (fallbackErr: any) {
			logError("Failed to send user message to agent", fallbackErr);
		}
	}
}

/** Queue a user message asking the model to update the quest file with standard prompt. */
function sendSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request(s) for this quest -- keep VERBATIM (or very faithful if truncated) under ## Original request in the quest file. This section MUST be present and faithful; do not summarize away details:\n${promptsBlock()}`;
	const text = `${message}\n\n${promptReminder}\n\nActive quest file: \`${questPath(state.active)}\`\n\nFinish current work, then update that file with the latest state (goal, progress, decisions, files touched, findings, TDD & Quality checklist, remaining work, next step). The file MUST contain a ## Original request section with the verbatim/faithful user request(s) above. Ensure TDD (tests written first), build/run verification, clean code (no debug artifacts), and full test suite passing are checked off. Make it complete enough to resume without re-research, then reply with a one-line confirmation.`;
	safeSendUserMessage(pi, text);
}

/** Queue a user message asking the model to perform an exhaustive context dump before compaction. */
function sendDeepSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request(s) for this quest -- keep VERBATIM (or very faithful if truncated) under ## Original request in the quest file:\n${promptsBlock()}`;
	const text = `${message}\n\n${promptReminder}\n\nActive quest file: \`${questPath(state.active)}\`\n\n**PRE-COMPACTION EXHAUSTIVE CONTEXT PRESERVATION PROTOCOL**:\nBefore context compaction resets working memory, update \`${questPath(state.active)}\` with an exhaustive dump so the next iteration requires ZERO re-research:\n1. ## Original request: Keep user prompts verbatim.\n2. ## Current Status & Progress: Complete checklist of what's done, in progress, or pending.\n3. ## In-Depth Analysis & Findings: All technical findings, root cause analysis, architecture discoveries, data flows, exact function signatures, and explored trade-offs.\n4. ## Files touched: Complete list of touched and examined files.\n5. ## Decisions made: Every architectural and design decision with rationale.\n6. ## Sub-Quests: Current status of all sub-quests and parent links.\n7. ## Remaining work: Exact actionable checklist of remaining tasks.\n8. ## Resume prompt: A comprehensive multi-paragraph briefing giving the next agent iteration complete context so it resumes seamlessly with ZERO re-research.\n\nEnsure build/run verification, clean code (no debug artifacts), and tests are up to date. Once written, call \`quest_mark_saved\`.`;
	safeSendUserMessage(pi, text);
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
		const threshold = getEconomyThreshold(ctx);
		const tokens = calculateCurrentTokens(ctx);
		const tokenStr = tokens !== null ? ` | tokens: ${formatTokens(tokens)}${threshold > 0 ? `/${formatTokens(threshold)}` : ""}` : "";
		const stackInfo = state.stack && state.stack.length > 1 ? ` | LIFO stack: [${state.stack.join(" → ")}]` : "";
		lines.push(
			`- Active quest: \`docs/current/${state.active}.md\` [${hier}] (${fresh ? "fresh" : "SAVE PENDING - update it before compaction"}${tokenStr}${stackInfo}); manage with /quest, /subquest, /quests, /quest-economy.`,
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
	pi.on(
		"before_agent_start",
		withContext(async (event: any) => {
			if (!state.active) return;
			const raw = (event as { prompt?: unknown }).prompt;
			if (typeof raw !== "string" || !shouldCapturePrompt(raw)) return;
			const trimmed = raw.trim().slice(0, PROMPT_MAX_CHARS);
			if (state.prompts.length > 0 && state.prompts[state.prompts.length - 1] === trimmed) return;
			state.prompts.push(trimmed);
			if (state.prompts.length > PROMPT_MAX_COUNT) state.prompts = state.prompts.slice(-PROMPT_MAX_COUNT);
			persist(pi);
		}),
	);
}

function installBeforeAgentStart(pi: ExtensionAPI) {
	pi.on(
		"before_agent_start",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			// Passive awareness is already injected via buildSessionAwarenessBlock
			// Do not inject synthetic user messages before agent start to prevent message spam.
		}),
	);
}

function installTurnEnd(pi: ExtensionAPI) {
	pi.on(
		"turn_end",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (pickerCancelledThisSession) return;
			if (!state.active) return;

			const threshold = getEconomyThreshold(ctx);
			const tokens = calculateCurrentTokens(ctx);

			if (threshold > 0 && tokens !== null) {
				const warningMargin = getWarningMargin();
				const warningTokens = Math.max(0, threshold - warningMargin);

				if (tokens >= threshold) {
					if (compactionReady() && typeof ctx.compact === "function") {
						checkAndTriggerEconomyCompaction(pi, ctx);
						return;
					}
				} else if (tokens >= warningTokens && !compactionReady()) {

					// In the pre-compaction warning window with save pending:
					// Send a single deep save warning if we haven't warned yet for this compaction cycle
					const alreadyWarned =
						typeof state.lastWarnedCompactionTokens === "number" &&
						Math.abs(tokens - state.lastWarnedCompactionTokens) < warningMargin;

					if (!alreadyWarned) {
						state.lastWarnedCompactionTokens = tokens;
						persist(pi, ctx);
						sendDeepSaveRequest(
							pi,
							`🚨 Quest-journal economy: token usage is at ${formatTokens(tokens)} (within ${formatTokens(warningMargin)} of the ${formatTokens(threshold)} auto-compaction limit). AUTO-COMPACTION WILL OCCUR SOON to reset working memory. You MUST update \`${questPath(state.active)}\` now with an exhaustive context snapshot (all technical findings, architecture discoveries, decisions made, touched files, test status, and comprehensive ## Resume prompt) so that the subsequent iteration does not re-research. After updating the quest files, compaction will immediately trigger.`,
						);
						lastPromptAt = Date.now();
						return;
					}
				}
			}

			// Routine turns do not inject synthetic follow-up messages into the conversation.
			// Quest status and compaction readiness are communicated via session awareness & UI widgets.
		}),
	);
}

function installBeforeCompact(pi: ExtensionAPI) {
	pi.on(
		"session_before_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return; // nothing to protect
			if (compactionReady()) return; // file is fresh since last compaction -- allow

			sendDeepSaveRequest(
				pi,
				`🚨 Quest-journal economy: compaction requested while quest file \`${questPath(state.active)}\` has unsaved changes. You MUST update \`${questPath(state.active)}\` now with an exhaustive context dump before compaction resets working memory.`,
			);
			if (ctx.hasUI) ctx.ui.notify(`Quest-journal: blocking compaction until '${questPath(state.active)}' is saved.`, "warning");
			return { cancel: true };
		}),
	);
}

function sendPostCompactionResumePrompt(pi: ExtensionAPI, activeQuest: string) {
	if (!activeQuest) return;
	const isSubQuest = Array.isArray(state.stack) && state.stack.length > 1;
	const parentQuest = isSubQuest ? state.stack[state.stack.length - 2] : null;

	const subquestContext = isSubQuest
		? `You are inside sub-quest **${activeQuest}** (parent: **${parentQuest}**).`
		: `You are working on active quest **${activeQuest}**.`;

	const directiveText = `⚡ **Post-Compaction Autonomous Resumption Directive**:
Context compaction has finished. Working memory has been cleanly reset.

${subquestContext}
The single authoritative source of truth on disk is \`docs/current/${activeQuest}.md\`.

**Action Required Now**:
1. Read \`docs/current/${activeQuest}.md\` using your \`read\` tool.
2. Check \`## Current Status\`, \`## Detailed Multi-Stage Execution Plan\`, \`## Remaining work\`, and \`## Next recommended step\`.
3. If the plan has not yet been confirmed by the user in Turn 1, present the complete analysis findings and multi-stage plan clearly to the user for confirmation before touching code.
4. If the plan is already confirmed, proceed directly with implementing the next step without waiting for user commands, without asking modal questions, and without re-researching solved context.`;

	safeSendUserMessage(pi, directiveText);
}

function installAfterCompact(pi: ExtensionAPI) {
	pi.on(
		"session_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			state.compactionPending = false;
			if (!state.active) return;
			state.compactCount = state.saveCount; // a compaction happened; next save re-arms the gate
			state.lastWarnedCompactionTokens = null; // reset warning flag for next compaction cycle
			persist(pi, ctx);
			sendPostCompactionResumePrompt(pi, state.active);
		}),
	);
	pi.on(
		"session_compact_failed",
		withContext(async (_event: any, _ctx: ExtensionContext) => {
			state.compactionPending = false;
		}),
	);
}

function installBeforeSwitch(pi: ExtensionAPI) {
	pi.on(
		"session_before_switch",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			if (ctx.hasUI && !compactionReady()) {
				ctx.ui.notify(`Quest-journal: active quest '${state.active}' has unsaved changes before session switch.`, "warning");
			}
		}),
	);
}

interface QuestChoiceResult {
	name: string;
	goal?: string;
}

/** Prompt the user with an interactive selector to choose an existing quest, draft, or create a new quest. */
async function promptForQuestChoice(ctx: ExtensionContext, title = "Select quest:"): Promise<QuestChoiceResult | null> {
	if (!ctx.hasUI || ctx.mode !== "tui") return null;
	const current = await listQuestFiles(QUEST_DIR);
	const future = await listQuestFiles(FUTURE_DIR);
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
		pickerCancelledThisSession = true; // user opted out -- suppress quest-journal prompts until /quest
		return;
	}
	if (choice.goal && choice.goal !== choice.name) {
		pi.sendUserMessage(`/quest ${choice.name} ${choice.goal}`);
	} else {
		pi.sendUserMessage(`/quest ${choice.name}`);
	}
}

/** On quit, notify if the active quest has unsaved changes. */
function installShutdownSave(pi: ExtensionAPI) {
	pi.on("session_shutdown", async (event: any, ctx: any) => {
		if (event.reason !== "quit") return;
		if (!state.active) return;
		if (ctx?.hasUI && !compactionReady()) {
			ctx.ui.notify(`Quest-journal: quest '${state.active}' has unsaved changes.`, "warning");
		}
	});
}

/** A write/edit to the active quest file counts as a save (lowers the compaction gate). */
function installFileWatch(pi: ExtensionAPI) {
	pi.on("tool_result", async (event: any, ctx: any) => {
		// Tool execution failure check: failed writes/edits must NOT count as saves!
		if (event.isError || event.error || (event.details && (event.details.error || event.details.success === false))) {
			return;
		}

		if (event.toolName !== "write" && event.toolName !== "edit") {
			// Commands and subagent tasks mark state dirty
			if (event.toolName === "bash" || event.toolName === "subagent") {
				state.dirty = true;
			}
			return;
		}

		const p = event.input?.path as string | undefined;
		if (typeof p !== "string") return;
		const norm = normalizePath(p);

		// If writing to the active quest file, verify and fingerprint it
		if (state.active && norm === questPath(state.active)) {
			await verifyAndMarkSaved(pi, ctx, state.active);
		} else if (!state.active && norm.startsWith(`${QUEST_DIR}/`) && norm.endsWith(".md")) {
			const slug = basename(norm).replace(/\.md$/, "");
			state.active = slug;
			if (!Array.isArray(state.stack)) state.stack = [slug];
			else if (!state.stack.includes(slug)) state.stack.push(slug);
			await verifyAndMarkSaved(pi, ctx, slug);
		} else {
			// Write or edit to any other file marks quest state dirty.
			// Never implicitly activate or switch quests based on file writes!
			state.dirty = true;
		}
	});
}

async function markSubQuestCompletedInParent(parentSlug: string, childSlug: string, ctx?: ExtensionContext): Promise<boolean> {
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
	} catch (err: any) {
		logError(`Failed to mark subquest ${childSlug} completed in ${parentPath}`, err, ctx);
	}
	return false;
}

const normalizePath = (p: string) => p.replace(/^\.\//, "").replace(/\\/g, "/");

/** Helper to archive a quest file and update journal state (LIFO stack pop) */
async function archiveQuestFile(name: string, pi: ExtensionAPI, ctx?: ExtensionContext): Promise<{ success: boolean; message: string; dest?: string; nextActive?: string | null }> {
	const path = questPath(name);
	if (!(await fileExists(path))) {
		return { success: false, message: `No quest file found at ${path}` };
	}

	let parentSlug: string | null = null;
	try {
		const content = await readFile(path, "utf8");
		parentSlug = extractParentFromQuest(content);
	} catch (err: any) {
		logError(`Failed to read quest file for parent extraction at ${path}`, err, ctx);
	}

	await mkdir(ARCHIVE_DIR, { recursive: true });
	const dest = `${ARCHIVE_DIR}/${basename(path).replace(/\.md$/, "")}-${Date.now().toString(36)}.md`;
	await rename(path, dest);
	await cleanDraftIfExists(name, ctx);

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
		await markSubQuestCompletedInParent(parentSlug, name, ctx);
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
		if (shouldCompact && typeof ctx.compact === "function") {
			state.archiveCompactionPending = targetName;
		}

		if (ctx.hasUI) ctx.ui.notify(res.message, "info");
		return {
			content: [
				{
					type: "text",
					text: `${res.message}${shouldCompact ? " Context compaction queued for turn end." : ""}`,
				},
			],
			details: { archived: targetName, dest: res.dest, compacted: shouldCompact },
		};
	};

	pi.registerTool({
		name: "quest_archive",
		label: "Archive Quest",
		description: `Archive the active (or specified) quest from docs/current/ to ${ARCHIVE_DIR}/ and optionally trigger session context compaction.`,
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
		description: `Archive the active (or specified) quest from docs/current/ to ${ARCHIVE_DIR}/ and optionally trigger session context compaction.`,
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
		const switchNow = params.switchNow !== false;

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
			// Sub-quest file already exists -- link in parent if not already linked
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
			updateUIStatus(ctx);
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
		description: "Create or plan a sub-quest. Use switchNow: false during initial quest planning to pre-create planned sub-quests without switching away from the active parent quest, or switchNow: true (default) to create and immediately switch focus to the sub-quest. Creates docs/current/<sub-quest>.md, links it into the parent quest, and records parent reference.",
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
					description: "Whether to immediately switch the active session quest to this sub-quest (default: true). Set to false when planning sub-quests upfront.",
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
		description: "Create or plan a sub-quest for initial planning (switchNow: false) or mid-quest remarks/tangents/follow-ups (switchNow: true).",
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
					description: "Whether to immediately switch active quest to this sub-quest. Set false to plan without switching.",
				},
			},
			required: ["goal"],
			additionalProperties: false,
		},
		execute: subquestHandler,
	});
}

function installMarkTool(pi: ExtensionAPI) {
	const markHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		let targetName = params?.name ? slugify(params.name) : state.active;
		if (!targetName) {
			const files = await listQuestFiles(QUEST_DIR);
			if (files.length === 1) {
				targetName = files[0].replace(/\.md$/, "");
			} else if (files.length > 1) {
				let newestTime = 0;
				let newestSlug = "";
				for (const f of files) {
					try {
						const st = await stat(`${QUEST_DIR}/${f}`);
						if (st.mtimeMs > newestTime) {
							newestTime = st.mtimeMs;
							newestSlug = f.replace(/\.md$/, "");
						}
					} catch {}
				}
				if (newestSlug) targetName = newestSlug;
			}
		}

		if (!targetName) {
			return {
				content: [{ type: "text", text: "Error: No active quest is set. Pass the quest name or use quest_update_state({ name: '...' })." }],
				details: { error: "no_active_quest" },
			};
		}

		if (!state.active) {
			state.active = targetName;
			if (!Array.isArray(state.stack)) state.stack = [targetName];
			else if (!state.stack.includes(targetName)) state.stack.push(targetName);
			persist(pi, ctx);
		}

		const res = await verifyAndMarkSaved(pi, ctx, targetName);
		if (!res.success) {
			return {
				content: [{ type: "text", text: `Error: ${res.error}` }],
				details: { error: "file_missing_or_unreadable", path: questPath(targetName) },
			};
		}

		return {
			content: [
				{
					type: "text",
					text: `Quest file '${questPath(targetName)}' verified and marked as saved in the journal (gen #${res.count}, hash: ${res.hash}).`,
				},
			],
			details: { hash: res.hash, generation: res.count },
		};
	};

	pi.registerTool({
		name: "quest_mark_saved",
		label: "Mark Quest Saved",
		description: "Record that the active quest file has been written to disk. Call after updating the quest file.",
		parameters: {
			type: "object",
			properties: {
				name: {
					type: "string",
					description: "Optional quest name/slug if setting or marking for the first time.",
				},
			},
			additionalProperties: false,
		},
		execute: markHandler,
	});

	pi.registerTool({
		name: "quest_journal_mark_saved",
		label: "Mark Quest Saved (alias for quest_mark_saved)",
		description: "Record that the active quest file has been written to disk. Call after updating the quest file.",
		parameters: {
			type: "object",
			properties: {
				name: {
					type: "string",
					description: "Optional quest name/slug if setting or marking for the first time.",
				},
			},
			additionalProperties: false,
		},
		execute: markHandler,
	});

	const updateStateHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		const targetName = slugify(params.name || params.questName || state.active || "");
		if (!targetName) {
			return {
				content: [{ type: "text", text: "Error: No active quest to update and no quest name provided." }],
				details: { error: "no_active_quest" },
			};
		}

		if (!state.active) {
			state.active = targetName;
			if (!Array.isArray(state.stack)) state.stack = [targetName];
			else if (!state.stack.includes(targetName)) state.stack.push(targetName);
			persist(pi, ctx);
		}

		const path = questPath(targetName);
		if (!(await fileExists(path))) {
			await writeFile(path, QUEST_TEMPLATE(targetName, params.goal || ""), "utf8");
		}

		try {
			const content = await readFile(path, "utf8");
			const updates = new Map<string, string>();

			if (params.goal) {
				updates.set("goal", params.goal);
			}

			if (params.status) {
				updates.set("current status", params.status);
			}

			if (Array.isArray(params.findings) && params.findings.length > 0) {
				const findingsText = params.findings.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
				updates.set("in-depth analysis & findings", findingsText);
			}

			if (Array.isArray(params.decisions) && params.decisions.length > 0) {
				const decisionsText = params.decisions.map((d: string) => (d.startsWith("- ") ? d : `- ${d}`)).join("\n");
				updates.set("decisions made", decisionsText);
			}

			if (Array.isArray(params.filesTouched) && params.filesTouched.length > 0) {
				const filesText = params.filesTouched.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
				updates.set("files touched", filesText);
			}

			if (Array.isArray(params.remaining) && params.remaining.length > 0) {
				const remainingText = params.remaining.map((r: string) => (r.startsWith("- [") ? r : `- [ ] ${r}`)).join("\n");
				updates.set("remaining work", remainingText);
			}

			if (params.nextStep) {
				updates.set("next recommended step", params.nextStep);
			}

			const updatedMarkdown = spliceMarkdownSections(content, updates);
			await writeFile(path, updatedMarkdown, "utf8");

			const saveRes = await verifyAndMarkSaved(pi, ctx, targetName);

			return {
				content: [{ type: "text", text: `Successfully updated quest state for **${targetName}** at \`${path}\` (gen #${saveRes.count}, hash: ${saveRes.hash}).` }],
				details: { quest: targetName, path, status: params.status, hash: saveRes.hash, generation: saveRes.count },
			};
		} catch (err: any) {
			logError(`Failed to update quest state at ${path}`, err, ctx);
			return {
				content: [{ type: "text", text: `Error updating quest state: ${err?.message || err}` }],
				details: { error: "update_failed", message: String(err) },
			};
		}
	};

	pi.registerTool({
		name: "quest_update_state",
		label: "Update Quest State",
		description: "Update the active quest state on disk with structured fields (status, findings, decisions, remaining work, next step). Formats and saves the quest file deterministically.",
		parameters: {
			type: "object",
			properties: {
				name: {
					type: "string",
					description: "Quest name/slug. Defaults to currently active quest.",
				},
				goal: {
					type: "string",
					description: "Quest goal description.",
				},
				status: {
					type: "string",
					description: "Current status description (e.g. 'Phase 1 Complete - tests passing').",
				},
				findings: {
					type: "array",
					items: { type: "string" },
					description: "Key findings or architectural discoveries.",
				},
				decisions: {
					type: "array",
					items: { type: "string" },
					description: "Key architectural decisions made.",
				},
				filesTouched: {
					type: "array",
					items: { type: "string" },
					description: "List of files modified or examined.",
				},
				remaining: {
					type: "array",
					items: { type: "string" },
					description: "List of remaining tasks / checklist items.",
				},
				nextStep: {
					type: "string",
					description: "Next recommended action or step.",
				},
			},
			additionalProperties: false,
		},
		execute: updateStateHandler,
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
			const fullFuturePath = `${FUTURE_DIR}/${fullSlug}.md`;
			const firstPath = questPath(firstSlug);
			const firstFuturePath = `${FUTURE_DIR}/${firstSlug}.md`;

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
		const futurePath = `${FUTURE_DIR}/${name}.md`;
		
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
		const startMsg = `Now working on quest **${name}**. Quest file: \`${path}\`.${goalText}

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
4. Final Quality Gates: zero build errors/warnings, zero debug artifacts, and full test suite passing with zero errors.`;
		safeSendUserMessage(pi, startMsg);
	};

	const questCompletions = async (prefix: string) => {
		const current = await listQuestFiles(QUEST_DIR);
		const future = await listQuestFiles(FUTURE_DIR);
		const names = [...new Set([...current, ...future])].map((f) => f.replace(/\.md$/, ""));
		const filtered = names.filter((n) => n.startsWith(prefix));
		return filtered.length ? filtered.map((value) => ({ value, label: value })) : null;
	};

	pi.registerCommand("quest", {
		description: `Set the active quest (e.g. /quest cx). Promotes from ${FUTURE_DIR}/ if it exists, or creates docs/current/<name>.md.`,
		getArgumentCompletions: questCompletions,
		handler: questHandler,
	});

	const questSaveHandler = async (_args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			ctx.ui.notify("No active quest -- use /quest <name> first.", "warning");
			return;
		}
		sendSaveRequest(pi, "Quest-journal: /quest-save -- write a full state snapshot to the active quest file now.");
		lastPromptAt = Date.now();
	};

	pi.registerCommand("quest-save", {
		description: "Persist the active quest file now.",
		handler: questSaveHandler,
	});

	const questRefineHandler = async (args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			ctx.ui.notify("No active quest -- use /quest <name> first.", "warning");
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
			`Quest-journal: /quest-refine -- User quest refinement received:\n"${refinement}"\n\nUpdate \`docs/current/${state.active}.md\` now: expand ## Goal if needed, add entry under ## Quest Refinements & User Feedback Loops, update ## Remaining work and ## TDD & Quality Checklist, and record any new decisions.`
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
		description: `Archive (rename to ${ARCHIVE_DIR}/) the current or named quest file.`,
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
		await mkdir(FUTURE_DIR, { recursive: true });
		const path = `${FUTURE_DIR}/${name}.md`;
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
		const trimmed = args.trim();
		const currentThreshold = getEconomyThreshold(ctx);
		const currentWarning = getWarningMargin();
		const currentSubquest = getSubquestCompactThreshold();
		const tokens = calculateCurrentTokens(ctx);
		const tokenStr = tokens !== null ? formatTokens(tokens) : "unknown";

		if (!trimmed) {
			const thresholdStr = currentThreshold > 0 ? `${formatTokens(currentThreshold)} tokens (${currentThreshold.toLocaleString()})` : "disabled";
			const warnStr = `${formatTokens(currentWarning)} tokens (${currentWarning.toLocaleString()})`;
			const subStr = `${formatTokens(currentSubquest)} tokens (${currentSubquest.toLocaleString()})`;
			const effectiveWarn = currentThreshold > 0 ? `${formatTokens(Math.max(0, currentThreshold - currentWarning))}` : "N/A";
			const msg = `Quest Economy: threshold = ${thresholdStr}, pre-compact warning = ${warnStr} (warns at ${effectiveWarn}), subquest launch limit = ${subStr}. Current usage = ${tokenStr} tokens. Usage: /quest-economy <threshold|percent> [warning] [subquestLaunch] (e.g. /quest-economy 80%, /quest-economy 333k 30k 40k, /quest-economy off)`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		if (trimmed.toLowerCase() === "default") {
			state.economyTokens = null;
			state.economyPercent = null;
			state.warningMarginTokens = null;
			state.subquestCompactTokens = null;
			persist(pi, ctx);
			const newThreshold = getEconomyThreshold(ctx);
			const newWarning = getWarningMargin();
			const newSub = getSubquestCompactThreshold();
			const msg = `Quest Economy: reset to default (threshold = ${formatTokens(newThreshold)}, warning = ${formatTokens(newWarning)}, subquest = ${formatTokens(newSub)}). Current usage: ${tokenStr}.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		if (trimmed.toLowerCase() === "off" || trimmed.toLowerCase() === "disable" || trimmed.toLowerCase() === "disabled" || trimmed === "0") {
			state.economyTokens = 0;
			state.economyPercent = null;
			persist(pi, ctx);
			const msg = `Quest Economy: auto-compaction disabled. Current usage: ${tokenStr}.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		const parts = trimmed.split(/\s+/);
		const pct = parsePercentage(parts[0]);
		if (pct !== null && pct > 0) {
			state.economyPercent = pct;
			state.economyTokens = null;
		} else {
			const parsedThreshold = parseTokenAmount(parts[0]);
			if (parsedThreshold === null || parsedThreshold <= 0) {
				if (ctx.hasUI) ctx.ui.notify(`Invalid threshold: "${parts[0]}". Examples: 80%, 333k, 333k 30k, 500000, off, default`, "warning");
				return;
			}
			state.economyTokens = parsedThreshold;
			state.economyPercent = null;
		}

		if (parts.length > 1) {
			const parsedWarn = parseTokenAmount(parts[1], DEFAULT_PRE_COMPACT_WARNING_TOKENS);
			if (parsedWarn !== null && parsedWarn > 0) {
				state.warningMarginTokens = parsedWarn;
			}
		}

		if (parts.length > 2) {
			const parsedSub = parseTokenAmount(parts[2], DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
			if (parsedSub !== null && parsedSub >= 0) {
				state.subquestCompactTokens = parsedSub;
			}
		}
		persist(pi, ctx);

		const activeThreshold = getEconomyThreshold(ctx);
		const activeWarning = getWarningMargin();
		const activeSub = getSubquestCompactThreshold();
		const msg = `Quest Economy: threshold set to ${formatTokens(activeThreshold)} tokens (${activeThreshold.toLocaleString()}), warning margin = ${formatTokens(activeWarning)}, subquest launch limit = ${formatTokens(activeSub)}. Current usage: ${tokenStr}.`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
	};

	const questWarningHandler = async (args: string, ctx: ExtensionContext) => {
		const trimmed = args.trim();
		const currentWarning = getWarningMargin();
		const currentThreshold = getEconomyThreshold(ctx);

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

	const questSubquestThresholdHandler = async (args: string, ctx: ExtensionContext) => {
		const trimmed = args.trim();
		if (!trimmed) {
			const current = getSubquestCompactThreshold();
			const msg = `Sub-quest launch compaction threshold = ${formatTokens(current)} tokens (${current.toLocaleString()}). Usage: /quest-subquest-threshold <tokens|off|default> (e.g. /quest-subquest-threshold 40k)`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}
		if (trimmed.toLowerCase() === "default") {
			state.subquestCompactTokens = null;
			persist(pi, ctx);
			const msg = `Sub-quest launch compaction threshold reset to default (${formatTokens(getSubquestCompactThreshold())}).`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}
		if (trimmed.toLowerCase() === "off" || trimmed.toLowerCase() === "disable" || trimmed.toLowerCase() === "0") {
			state.subquestCompactTokens = 0;
			persist(pi, ctx);
			const msg = `Sub-quest launch compaction disabled.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}
		const parsed = parseTokenAmount(trimmed, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
		if (parsed === null) {
			if (ctx.hasUI) ctx.ui.notify(`Invalid token amount: "${trimmed}". Examples: 40k, 50000, off, default`, "warning");
			return;
		}
		state.subquestCompactTokens = parsed;
		persist(pi, ctx);
		const msg = `Sub-quest launch compaction threshold set to ${formatTokens(parsed)} tokens (${parsed.toLocaleString()}).`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
	};

	const economyCompletions = async (prefix: string) => {
		const options = ["80%", "75%", "70%", "333k", "400k", "500k", "off", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	const warningCompletions = async (prefix: string) => {
		const options = ["15k", "20k", "25k", "30k", "35k", "40k", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	const subquestThresholdCompletions = async (prefix: string) => {
		const options = ["20k", "30k", "40k", "50k", "60k", "off", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	pi.registerCommand("quest-economy", {
		description: "Configure or check token economy auto-compaction threshold (e.g. /quest-economy 80%, /quest-economy 333k 30k, /quest-economy off).",
		getArgumentCompletions: economyCompletions,
		handler: questEconomyHandler,
	});

	pi.registerCommand("quest-warning", {
		description: "Configure pre-compaction warning margin (e.g. /quest-warning 30k).",
		getArgumentCompletions: warningCompletions,
		handler: questWarningHandler,
	});

	pi.registerCommand("quest-subquest-threshold", {
		description: "Configure the minimum token threshold for auto-compacting when launching a sub-quest (e.g. /quest-subquest-threshold 40k).",
		getArgumentCompletions: subquestThresholdCompletions,
		handler: questSubquestThresholdHandler,
	});

	const questStatusHandler = async (_args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			if (ctx.hasUI) ctx.ui.notify("No active quest.", "info");
			return "No active quest.";
		}
		const path = questPath(state.active);
		const exists = await fileExists(path);
		const fresh = compactionReady();
		const hier = formatQuestHierarchy(state.active, state.stack);
		const threshold = getEconomyThreshold(ctx);
		const tokens = calculateCurrentTokens(ctx);
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
			} catch (err: any) {
				logError(`Failed to read quest file for status at ${path}`, err, ctx);
			}
		}

		const line = exists
			? `${path}${parentInfo} [${hier}] - ${fresh ? "fresh" : "SAVE PENDING"}, tokens ${tokenStr}, prompts ${state.prompts.length}${subInfo}`
			: `${path} - MISSING on disk!`;
		if (ctx.hasUI) ctx.ui.notify(`Active quest: ${line}`, fresh ? "info" : "warning");
		return line;
	};

	pi.registerCommand("quest-status", {
		description: "Show the active quest and whether its file is fresh.",
		handler: questStatusHandler,
	});

	const questsHandler = async (_args: string, ctx: ExtensionContext) => {
		const current = await listQuestFiles(QUEST_DIR);
		const future = await listQuestFiles(FUTURE_DIR);

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
			} catch (err: any) {
				logError(`Failed to read quest file for parent linking at ${QUEST_DIR}/${f}`, err, ctx);
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
			: ["  (none - use /quest-draft <name>)"];
		
		ctx.ui.setWidget("quest-journal", [
			`Active: ${state.active ? questPath(state.active) : "(none)"}`, 
			"",
			"Current quests:",
			...(renderedCurrent.length ? renderedCurrent : ["  (none - use /quest <name>)"]),
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
		let raw = args.trim();
		if (!raw && ctx.mode === "tui") {
			raw = ((await ctx.ui.input("Describe the sub-quest (e.g. handle auth edge cases):")) ?? "").trim();
		}
		if (!raw) {
			ctx.ui.notify("Usage: /subquest [--plan|-p] <description...>", "warning");
			return;
		}

		let switchNow = true;
		if (raw.startsWith("--plan ") || raw.startsWith("-p ") || raw.startsWith("--no-switch ")) {
			switchNow = false;
			raw = raw.replace(/^(--plan|-p|--no-switch)\s+/, "").trim();
		}

		const goal = raw;
		const name = slugify(raw);

		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(name);
		const parentName = state.active || "";

		if (!(await fileExists(path))) {
			await writeFile(path, QUEST_TEMPLATE(name, goal, parentName), "utf8");
		}
		if (parentName) {
			await linkSubQuestInParent(parentName, name, goal);
		}

		if (!switchNow) {
			const msg = `Planned sub-quest **${name}** at \`${path}\`${parentName ? ` linked in parent **${parentName}**` : ""}. Kept active quest **${state.active}**.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
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
		updateUIStatus(ctx);

		const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
		const subquestMsg = `Now working on sub-quest **${name}**${parentName ? ` (parent: **${parentName}**)` : ""}. Sub-quest file: \`${path}\`.${goalText}

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
4. Final Quality Gates: zero build errors/warnings, zero debug artifacts, and full test suite passing with zero errors.`;

		safeSendUserMessage(pi, subquestMsg);
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
		goal ? `> Goal: ${goal}` : `> Paste the verbatim user prompt here (or very faithful summary if truncated). This section MUST stay faithful -- it is enforced by the extension.`,
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
		`> Planned sub-quests, follow-ups, or tangent quests linked to this quest.`,
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

		const sections = parseMarkdownSections(content);
		const targetSections = [
			{ key: "goal", title: "Goal" },
			{ key: "parent quest", title: "Parent Quest" },
			{ key: "sub-quests", title: "Sub-Quests" },
			{ key: "current status", title: "Current Status" },
			{ key: "remaining work", title: "Remaining work" },
			{ key: "next recommended step", title: "Next recommended step" },
			{ key: "resume prompt", title: "Resume prompt" },
		];

		const extracted: string[] = [];
		for (const target of targetSections) {
			const sec = sections.get(target.key);
			if (sec && sec.body) {
				extracted.push(`### ${target.title}\n${sec.body}`);
			}
		}

		if (extracted.length === 0) return "";
		return `\n\n# Active Quest Resume Context (from \`${path}\`)\n${extracted.join("\n\n")}`;
	} catch (err: any) {
		logError(`Failed to load resume context from ${path}`, err);
		return "";
	}
}

function installWorkflowSystemPrompt(pi: ExtensionAPI) {
	pi.on("before_agent_start", async (event: any, ctx: any) => {
		try {
			const awarenessBlock = buildSessionAwarenessBlock(ctx);
			const resumeContext = await loadActiveQuestResumeContext();
			const workflowInstructions = `\n\n# Mandatory Quest Workflow Rules (TDD & Quality Gates)
When working on quests:
1. **Upfront Deep Research, Planning & Confirmation (Turn 1 Protocol)**:
   - Before writing or editing feature code, conduct a thorough architectural audit of the relevant codebase, understand constraints, read related files, and design an ambitious, multi-stage plan where each phase is self-contained.
   - Infer a clean quest slug, create and fill the quest file on disk in \`docs/current/<inferred-slug>.md\`, and call \`quest_mark_saved\`.
   - **MANDATORY TURN 1 STOP**: In your very first turn, present the complete analysis findings, architectural trade-offs, and multi-stage plan clearly to the user, and ASK FOR USER CONFIRMATION BEFORE modifying any project code!
2. **Build & Run Discovery**: Discovered how to build and run the project before editing code (\`make\`, \`make watch\`).
3. **Develop Tests First (TDD)**: Develop targeted test(s) BEFORE developing each feature phase.
4. **Iterative Build, Run & Test**: Feature implementation -> build -> run -> verify targeted tests.
5. **Post-Implementation & User Feedback Loops**:
   - Expect and support user polish iterations at the end of a quest or sub-quest.
   - When the user provides feedback, refinements, or tweaks mid-quest or post-implementation, log them under \`## Quest Refinements & User Feedback Loops\`, update acceptance checklists, execute the changes, and verify with tests until the user confirms satisfaction.
6. **Quest Completion & Wrap-Up Flow**:
   - **Root / Top-Level Quest Completion**: When all stages, features, and acceptance criteria are completed, you MUST restart the test daemon/server and execute the FULL test suite (\`make test\`) to verify zero errors or regressions. Only after the full test suite passes with zero failures, prompt the user via \`ask_questions\` with the following structured options:
     1. **Refine anything**: Keep active quest open to make further adjustments or address feedback.
     2. **Archive quest and auto-compact**: Call \`quest_journal_archive({ compact: true })\` to archive the quest and automatically compact session context.
     3. **Archive quest without auto-compact**: Call \`quest_journal_archive({ compact: false })\` to archive the quest while keeping the current session context intact.
     4. **Change to manual mode**: Switch workflow / compaction to manual mode.
     Execute the exact action corresponding to the user's choice.
   - **Sub-Quest Completion (Autonomous Continuation)**: When finishing a child sub-quest, you do NOT need to ask for user input. Autonomously archive the sub-quest via \`quest_archive({ compact: boolean })\`--deciding \`compact: true\` if context is elevated or \`compact: false\` if context is low--then seamlessly continue working on the parent quest.
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
     - **Upfront Sub-Quest Planning**: When a quest consists of multiple distinct stages or components, plan and pre-create sub-quests at the beginning of the quest (e.g. using \`quest_subquest({ name, goal, switchNow: false })\` and linking them under \`## Sub-Quests\` in the parent quest file).
     - Present the complete analysis findings, architectural trade-offs, and multi-stage plan in your very first turn, and PROPOSE the plan for user confirmation before touching code.
   - If the user mentions continuing or resuming a quest without specifying which one:
     - Check current quests in \`docs/current/\` and prompt the user via \`ask_questions\` with the available choices.

2. **Auto-Refine Active Quest on User Feedback**:
   - When the user provides feedback, tweaks, or new requirements mid-quest or post-implementation:
     - Immediately read and update \`docs/current/<active-quest>.md\` using \`quest_update_state\` (or edit + \`quest_journal_mark_saved\`).
     - Log the feedback under \`## Quest Refinements & User Feedback Loops\`.
     - Update \`## Remaining work\`, \`## Acceptance Criteria & Polish Checklist\`, \`## Decisions made\`, and \`## Next recommended step\`.
     - Call \`quest_journal_mark_saved\`.

3. **Auto-Create Sub-Quests for Planned Work, Tangents, Follow-ups, Checks & Side Tasks (LIFO Stack)**:
   - Quests operate on a **LIFO (Last-In, First-Out) stack**.
   - Sub-quests can be planned upfront (\`switchNow: false\`) or switched into immediately (\`switchNow: true\`).
   - When user remarks, tangents, syntax checks (e.g. 'Can you check syntax?'), side investigations, code audits, or follow-ups arise during a quest: IMMEDIATELY invoke \`quest_subquest({ goal: "..." })\` (pushes sub-quest onto stack with fresh focus).
   - Perform the task inside the sub-quest, document findings in \`docs/current/<subquest>.md\`, and when finished call \`quest_archive({ compact: false })\` to pop the stack and resume the parent quest seamlessly!
   - Link it under \`## Sub-Quests\` in the parent quest file.

4. **Auto-Archive Upon Completion (LIFO Pop)**:
   - **For Sub-Quests**: Do NOT prompt the user with interactive modal questions. Autonomously archive the sub-quest via \`quest_archive({ compact: boolean })\` (choosing \`compact: true\` if context is high or \`compact: false\` if context is low) and immediately continue parent quest execution.
   - **For Root / Top-Level Quests**: Prompt the user via \`ask_questions\` (Refine, Archive with auto-compact, Archive without auto-compact, Manual mode) and execute \`quest_archive\` based on their choice. If returning to a parent quest on the LIFO stack, resume the parent quest cleanly.

# Economy Auto-Compaction & Autonomous Resumption Protocol
Context automatically compacts dynamically (default: max(80% of context window, 333k tokens) or configured threshold) to preserve speed, maximize reasoning bandwidth, and prevent attention drift.
1. **Dynamic Threshold**: Auto-compacts dynamically (default: max(80%, 333k tokens)).
2. **Pre-Compaction Warning Window**: When context approaches within the warning margin (~30k tokens) of the threshold, you will receive an explicit alert.
3. **Pre-Compaction Exhaustive Context Dump**: Upon receiving this alert, you MUST immediately update \`docs/current/<quest>.md\` with an exhaustive dump of all context knowledge, discoveries, architecture decisions, modified files, data structures, and a comprehensive resume prompt.
4. **Auto-Compaction After Update**: After updating the quest files, context compaction will immediately trigger to reset working memory.
5. **Autonomous Post-Compaction Resumption (Zero Interruption & Zero Re-Research)**: Following ANY compaction:
   - You MUST immediately read \`docs/current/<active-quest>.md\` (the single source of truth on disk).
   - Autonomously proceed with the next recommended step with ZERO re-research and ZERO pause.
   - Do NOT ask modal questions or wait for manual user commands. Continue execution directly.
6. **Faithful User Request**: The \`## Original request\` section MUST remain verbatim.${resumeContext}`;

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
			if (set.has("quest_journal_mark_saved") || set.has("quest_mark_saved") || set.has("quest_update_state") || state.active) {
				return [
					"Never propose anything without doing your homework first: thoroughly investigate codebase architecture, read files, discover build/run commands, and evaluate constraints before proposing plans or code changes.",
					"Turn 1 Confirmation: In turn 1 of any quest, present the research findings, architectural trade-offs, and multi-stage plan clearly to the user, and ask for confirmation BEFORE writing code.",
					"Active Quest: `docs/current/<quest>.md` is the single source of truth on disk. Update it proactively using `quest_update_state` (or `edit` + `quest_mark_saved`).",
					"Zero Re-Research: Never re-read or re-search context already documented in active/archived quest files.",
					"Autonomous Continuation: Following compaction or sub-quest return, read `docs/current/<active-quest>.md` and proceed immediately without user interruption.",
					"Sub-quests: Create sub-quests immediately for side tasks/tangents (`quest_subquest({ goal })`); complete and archive autonomously (`quest_archive({ compact })`) without modal questions.",
					"Full Test Suite Quality Gate: Before completing/archiving a top-level quest, restart the test server/daemon, run the fresh FULL test suite (`make test`), and verify zero errors.",
					"Top-level Quest Completion: When root quest is done, prompt user via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
				];
			}
			return [];
		});
	}
}

export default function (pi: ExtensionAPI) {
	// Automatically wrap callbacks and command/tool handlers with asyncContext
	const originalOn = pi.on.bind(pi);
	pi.on = (event: string, handler: any) => {
		originalOn(event, withContext(handler));
	};

	const originalRegisterTool = pi.registerTool.bind(pi);
	pi.registerTool = (tool: any) => {
		if (tool && typeof tool.execute === "function") {
			tool.execute = withContext(tool.execute);
		}
		originalRegisterTool(tool);
	};

	const originalRegisterCommand = pi.registerCommand.bind(pi);
	pi.registerCommand = (name: string, command: any) => {
		if (command && typeof command.handler === "function") {
			command.handler = withContext(command.handler);
		}
		originalRegisterCommand(name, command);
	};

	registerQuestJournalCRBHook();
	pi.on("session_start", async (event: any, ctx: any) => {
		reconstruct(ctx);
		await syncActiveQuestFromDisk(pi, ctx);
	});
	pi.on("session_tree", async (_event: any, ctx: any) => reconstruct(ctx));

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
