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
 *   /quest [name]      – set active quest (creates docs/current/<name>.md if missing)
 *   /quest-save        – persist current state now
 *   /quest-refine      – add mid-workflow or post-implementation requirements
 *   /quest-del [name]  – archive (rename to docs/archive/) the current/named quest
 *   /quest-draft <name>– draft a future quest in docs/future/
 *   /quest-economy     – configure token economy compaction threshold
 *   /quest-warning     – configure pre-compaction warning margin
 *   /quest-subquest-threshold – configure sub-quest launch compaction threshold
 *   /quest-status      – show active quest and staleness
 *   /quests            – list docs/current/*.md and docs/future/*.md
 *   /subquest          – create or plan a sub-quest linked to active parent
 *
 * Auto-behaviour
 * --------------
 *   - `before_agent_start`: inspects prompt, auto-creates root quest for substantive
 *     requests before execution begins, captures user refinements, injects session awareness.
 *   - `turn_end`: tracks progress/dirty state and arms the pre-compaction warning state when appropriate.
 *   - `session_before_compact`: acts as the final compaction safety gate and blocks
 *     compaction unless the active quest has a verified fresh save.
 *   - `session_compact`: records completed compaction and resumes autonomously.
 *   - `tool_result`: edits to active quest file count as saves; edits/commands mark state dirty.
 *   - `session_before_switch`: reminds to persist before leaving.
 */

import { AsyncLocalStorage } from "node:async_hooks";
import { createHash } from "node:crypto";
import { readFileSync } from "node:fs";
import { mkdir, readFile, readdir, rename, stat, unlink, writeFile } from "node:fs/promises";
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
	sendMessage?(message: any, options?: any): void;
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
const PROMPT_MAX_CHARS = 4000;
const PROMPT_MAX_COUNT = 10;
const DEFAULT_CEILING_TOKENS = 333_000; // 333K default ceiling
const DEFAULT_PERCENT = 80; // 80% default context percentage
const DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS = 60_000; // 60K default subquest launch compaction threshold
const DEFAULT_PRE_COMPACT_WARNING_TOKENS = 30_000; // 30K default pre-compaction warning margin

export interface SaveGeneration {
	count: number;
	path: string;
	hash: string;
	savedAt: number;
}

export interface StoredState {
	active: string | null;
	saveCount: number;
	compactCount: number;
	prompts: string[];
	refinements?: string[];
	stack: string[];
	dirty?: boolean;
	compactionPending?: boolean;
	archiveCompactionPending?: string | null;
	subquestLaunchCompactionPending?: boolean;
	preCompactionCheckpointPending?: boolean;
	preCompactionSaveRequestPending?: boolean;
	saveGeneration?: SaveGeneration | null;
	lastSavedHash?: string | null;
	economyTokens?: number | null;
	economyPercent?: number | null;
	warningMarginTokens?: number | null;
	subquestCompactTokens?: number | null;
	lastWarnedCompactionTokens?: number | null;
	lastPromptAt?: number;
	lastResumePromptAt?: number;
	lastResumeTarget?: string | null;
	lastResumeCompactCount?: number;
	pickerCancelled?: boolean;

	// Iterative research & reassessment epistemic state
	pendingRootQuest?: boolean;
	pendingRootRequest?: string | null;
	questIdentityEstablished?: boolean;
	researchRound?: number;
	researchComplete?: boolean;
	researchRequired?: boolean;
	reassessmentRequired?: boolean;
	reassessmentReason?: string | null;
	reassessmentEvidence?: string | null;
	reassessmentVersion?: number;
	resolvedReassessmentVersion?: number;
	lastPlanRevisionsText?: string | null;
	confirmedQuests?: string[];
	lastReassessmentPromptAt?: number;
	lastReassessmentReason?: string | null;
	lastCheckpointPromptAt?: number;
	planVersion?: number;
	planConfidence?: "low" | "medium" | "high";
	lastResearchAt?: number;
	lastPlanRevisionAt?: number;
	lastPromptedReassessmentVersion?: number;
	implementationAllowed?: boolean;
	awaitingUserConfirmation?: boolean;
	consecutiveFailures?: number;
	substantiveTurnsSinceCheckpoint?: number;
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
		refinements: [],
		stack: [],
		dirty: false,
		compactionPending: false,
		archiveCompactionPending: null,
		subquestLaunchCompactionPending: false,
		preCompactionCheckpointPending: false,
		preCompactionSaveRequestPending: false,
		saveGeneration: null,
		lastSavedHash: null,
		economyTokens: undefined,
		economyPercent: undefined,
		warningMarginTokens: undefined,
		subquestCompactTokens: undefined,
		lastWarnedCompactionTokens: undefined,
		lastPromptAt: Date.now(),
		lastResumePromptAt: 0,
		lastResumeTarget: null,
		lastResumeCompactCount: undefined,
		pickerCancelled: false,
		pendingRootQuest: false,
		pendingRootRequest: null,
		questIdentityEstablished: false,
		researchRound: 1,
		researchComplete: false,
		researchRequired: true,
		reassessmentRequired: false,
		reassessmentReason: null,
		reassessmentEvidence: null,
		reassessmentVersion: 0,
		resolvedReassessmentVersion: 0,
		lastPlanRevisionsText: null,
		confirmedQuests: [],
		lastReassessmentPromptAt: 0,
		lastReassessmentReason: null,
		lastCheckpointPromptAt: 0,
		planVersion: 1,
		planConfidence: "low",
		lastResearchAt: Date.now(),
		lastPlanRevisionAt: Date.now(),
		lastPromptedReassessmentVersion: 0,
		implementationAllowed: false,
		awaitingUserConfirmation: false,
		consecutiveFailures: 0,
		substantiveTurnsSinceCheckpoint: 0,
	};
}

function snapshotState(ctx?: ExtensionContext): StoredState {
	const s = getState(ctx);
	return {
		active: s.active,
		saveCount: s.saveCount || 0,
		compactCount: s.compactCount || 0,
		prompts: Array.isArray(s.prompts) ? [...s.prompts] : [],
		refinements: Array.isArray(s.refinements) ? [...s.refinements] : [],
		stack: Array.isArray(s.stack) ? [...s.stack] : [],
		dirty: !!s.dirty,
		compactionPending: !!s.compactionPending,
		archiveCompactionPending: s.archiveCompactionPending ?? null,
		subquestLaunchCompactionPending: !!s.subquestLaunchCompactionPending,
		preCompactionCheckpointPending: !!s.preCompactionCheckpointPending,
		preCompactionSaveRequestPending: !!s.preCompactionSaveRequestPending,
		saveGeneration: s.saveGeneration ? { ...s.saveGeneration } : null,
		lastSavedHash: s.lastSavedHash ?? null,
		economyTokens: s.economyTokens ?? null,
		economyPercent: s.economyPercent ?? null,
		warningMarginTokens: s.warningMarginTokens ?? null,
		subquestCompactTokens: s.subquestCompactTokens ?? null,
		lastWarnedCompactionTokens: s.lastWarnedCompactionTokens ?? null,
		lastPromptAt: s.lastPromptAt ?? Date.now(),
		lastResumePromptAt: s.lastResumePromptAt ?? 0,
		lastResumeTarget: s.lastResumeTarget ?? null,
		lastResumeCompactCount: s.lastResumeCompactCount,
		pickerCancelled: !!s.pickerCancelled,
		pendingRootQuest: !!s.pendingRootQuest,
		pendingRootRequest: s.pendingRootRequest ?? null,
		questIdentityEstablished: !!s.questIdentityEstablished,
		researchRound: typeof s.researchRound === "number" ? s.researchRound : 1,
		researchComplete: !!s.researchComplete,
		researchRequired: typeof s.researchRequired === "boolean" ? s.researchRequired : !s.researchComplete,
		reassessmentRequired: !!s.reassessmentRequired,
		reassessmentReason: s.reassessmentReason ?? null,
		reassessmentEvidence: s.reassessmentEvidence ?? null,
		reassessmentVersion: typeof s.reassessmentVersion === "number" ? s.reassessmentVersion : 0,
		resolvedReassessmentVersion: typeof s.resolvedReassessmentVersion === "number" ? s.resolvedReassessmentVersion : 0,
		lastPlanRevisionsText: s.lastPlanRevisionsText ?? null,
		confirmedQuests: Array.isArray(s.confirmedQuests) ? [...s.confirmedQuests] : [],
		lastReassessmentPromptAt: s.lastReassessmentPromptAt ?? 0,
		lastReassessmentReason: s.lastReassessmentReason ?? null,
		lastCheckpointPromptAt: s.lastCheckpointPromptAt ?? 0,
		planVersion: typeof s.planVersion === "number" ? s.planVersion : 1,
		planConfidence: s.planConfidence ?? "low",
		lastResearchAt: s.lastResearchAt ?? Date.now(),
		lastPlanRevisionAt: s.lastPlanRevisionAt ?? Date.now(),
		lastPromptedReassessmentVersion: s.lastPromptedReassessmentVersion ?? 0,
		implementationAllowed: canImplement(s),
		awaitingUserConfirmation: !!s.awaitingUserConfirmation,
		consecutiveFailures: typeof s.consecutiveFailures === "number" ? s.consecutiveFailures : 0,
		substantiveTurnsSinceCheckpoint: typeof s.substantiveTurnsSinceCheckpoint === "number" ? s.substantiveTurnsSinceCheckpoint : 0,
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

function getEconomyThreshold(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
	const contextWindow = usage?.contextWindow ?? 0;

	// 1. Explicit state overrides
	if (typeof s.economyPercent === "number" && s.economyPercent > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * s.economyPercent) / 100) : DEFAULT_CEILING_TOKENS;
	}
	if (typeof s.economyTokens === "number") {
		return s.economyTokens;
	}

	// 2. Environment variables
	const envVal = process.env.PI_QUEST_AUTO_COMPACT_TOKENS ?? process.env.QUEST_AUTO_COMPACT_TOKENS;
	if (envVal) {
		const envPct = parsePercentage(envVal);
		if (envPct !== null && envPct > 0) {
			return contextWindow > 0 ? Math.round((contextWindow * envPct) / 100) : DEFAULT_CEILING_TOKENS;
		}
		const parsedEnvTokens = parseTokenAmount(envVal);
		if (parsedEnvTokens !== null) return parsedEnvTokens;
	}

	// 3. Settings file
	const settingsConfig = readSettingsEconomyThreshold();
	if (settingsConfig) {
		if (typeof settingsConfig.percent === "number" && settingsConfig.percent > 0) {
			return contextWindow > 0 ? Math.round((contextWindow * settingsConfig.percent) / 100) : DEFAULT_CEILING_TOKENS;
		}
		if (typeof settingsConfig.tokens === "number") {
			return settingsConfig.tokens;
		}
	}

	// 4. Default: 80% of context window, capped at DEFAULT_CEILING_TOKENS (333k)
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

function getSubquestCompactThreshold(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	if (typeof s.subquestCompactTokens === "number") {
		return s.subquestCompactTokens;
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

function getWarningMargin(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	if (typeof s.warningMarginTokens === "number") {
		return s.warningMarginTokens;
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
		return `Economy auto-compaction${tokenLabel} during sub-quest '${activeQuest}' (parent: '${parentName}'). Focus summary on active sub-quest progress, tested hypotheses, key architectural decisions, modified files, and immediate sub-quest next steps. Parent quest state is safely preserved on disk in docs/current/${parentName}.md. Following compaction, autonomously read docs/current/${activeQuest}.md, validate current understanding against the recovered state, and proceed with the most justified next action.`;
	}

	return `Economy auto-compaction${tokenLabel}. Focus summary on active quest '${activeQuest}', tested hypotheses, key architectural decisions, modified files, and immediate next steps. The latest durable quest state is persisted in docs/current/${activeQuest}.md. Following compaction, autonomously read docs/current/${activeQuest}.md, validate current understanding against the recovered state, and proceed with the most justified next action.`;
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
	return `d${depth}: ${activeTruncated}`;
}

export enum CompactionPressure {
	NONE = "NONE",
	WARNING = "WARNING",
	CRITICAL = "CRITICAL",
}

export function getCompactionPressure(ctx?: ExtensionContext, targetState?: StoredState): {
	pressure: CompactionPressure;
	tokens: number | null;
	threshold: number;
	warningThreshold: number;
	warningMargin: number;
	fraction: number;
} {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	if (!c || !s.active || s.compactionPending) {
		return {
			pressure: CompactionPressure.NONE,
			tokens: null,
			threshold: 0,
			warningThreshold: 0,
			warningMargin: 0,
			fraction: 0,
		};
	}

	const threshold = getEconomyThreshold(c, s);
	const tokens = calculateCurrentTokens(c);
	const warningMargin = getWarningMargin(c, s);

	if (threshold <= 0 || tokens === null) {
		return {
			pressure: CompactionPressure.NONE,
			tokens,
			threshold,
			warningThreshold: 0,
			warningMargin,
			fraction: 0,
		};
	}

	const warningThreshold = Math.max(0, threshold - warningMargin);

	if (tokens >= threshold) {
		const span = Math.max(1, warningMargin);
		const fraction = 1 + (tokens - threshold) / span;
		return {
			pressure: CompactionPressure.CRITICAL,
			tokens,
			threshold,
			warningThreshold,
			warningMargin,
			fraction,
		};
	}

	if (tokens >= warningThreshold) {
		const span = Math.max(1, threshold - warningThreshold);
		const fraction = (tokens - warningThreshold) / span;
		return {
			pressure: CompactionPressure.WARNING,
			tokens,
			threshold,
			warningThreshold,
			warningMargin,
			fraction,
		};
	}

	return {
		pressure: CompactionPressure.NONE,
		tokens,
		threshold,
		warningThreshold,
		warningMargin,
		fraction: 0,
	};
}

function isRootQuest(targetState?: StoredState): boolean {
	const s = targetState || state;
	return !Array.isArray(s.stack) || s.stack.length <= 1;
}

function canImplement(targetState?: StoredState, ctx?: ExtensionContext): boolean {
	const s = targetState || state;
	if (s.pendingRootQuest) return false;
	if (!s.active) return true;
	const c = getActiveContext(ctx);
	if (c) {
		const pressureInfo = getCompactionPressure(c, s);
		if (pressureInfo.pressure === CompactionPressure.CRITICAL && !compactionReady(s.active)) {
			return false;
		}
	}
	if (s.researchRequired) return false;
	if (!s.researchComplete) return false;
	if (s.reassessmentRequired) return false;
	if (isRootQuest(s) && s.awaitingUserConfirmation) return false;
	return true;
}

function syncImplementationPermission(targetState?: StoredState, ctx?: ExtensionContext): boolean {
	const s = targetState || state;
	s.implementationAllowed = canImplement(s, ctx);
	return s.implementationAllowed;
}

function getImplementationBlockReason(targetState?: StoredState, ctx?: ExtensionContext): {
	blocked: boolean;
	stateName: string;
	reason: string;
	requiredAction: string;
} {
	const s = targetState || state;
	const c = getActiveContext(ctx);
	if (c && s.active) {
		const pressureInfo = getCompactionPressure(c, s);
		if (pressureInfo.pressure === CompactionPressure.CRITICAL && !compactionReady(s.active)) {
			return {
				blocked: true,
				stateName: "CRITICAL_COMPACTION_CHECKPOINT_REQUIRED",
				reason: `Context usage (${formatTokens(pressureInfo.tokens || 0)} tokens) has reached or exceeded the compaction threshold (${formatTokens(pressureInfo.threshold)} tokens) and no fresh verified durable checkpoint exists.`,
				requiredAction: `Perform an exhaustive durable state update in docs/current/${s.active}.md and call quest_mark_saved before modifying project code.`
			};
		}
	}
	if (s.pendingRootQuest) {
		return {
			blocked: true,
			stateName: "PROVISIONAL_RESEARCH_PENDING",
			reason: "Initial orientation & research required to understand the objective and establish the quest identity before modifying project code.",
			requiredAction: "Investigate relevant architecture and code paths using read/search/bash tools, establish a concise semantic quest identity, and call quest_update_state to initialize the durable quest with your research findings."
		};
	}
	if (!s.active) {
		return { blocked: false, stateName: "IDLE", reason: "", requiredAction: "" };
	}
	if (s.reassessmentRequired) {
		return {
			blocked: true,
			stateName: "REASSESSMENT_PENDING",
			reason: s.reassessmentReason || "The current plan has been invalidated by contradictory evidence or test failure.",
			requiredAction: `Investigate the contradiction, challenge prior assumptions, update docs/current/${s.active}.md, and complete reassessment via quest_update_state({ reassessmentComplete: true, reassessmentConclusion: "..." }) before modifying project code.`
		};
	}
	if (s.researchRequired || !s.researchComplete) {
		return {
			blocked: true,
			stateName: "RESEARCH_PENDING",
			reason: `Research & falsification pass is pending (Round ${s.researchRound || 1}). Key architecture, module boundaries, and assumptions must be verified first.`,
			requiredAction: `Perform targeted read/search investigation, update docs/current/${s.active}.md with verified understanding and plan, and call quest_update_state({ researchComplete: true }) with medium or high confidence.`
		};
	}
	if (isRootQuest(s) && s.awaitingUserConfirmation) {
		return {
			blocked: true,
			stateName: "CONFIRMATION_PENDING",
			reason: "Root quest research is complete, but user confirmation is required before modifying project code.",
			requiredAction: "Present your research findings, tested assumptions, and proposed plan clearly to the user (using ask_questions or a plain text question), and wait for user confirmation before editing code."
		};
	}
	return { blocked: false, stateName: "IMPLEMENTATION_ALLOWED", reason: "", requiredAction: "" };
}

export enum UserMessageClassification {
	CONVERSATIONAL_ACK = "CONVERSATIONAL_ACK",
	CONFIRMATION = "CONFIRMATION",
	QUESTION_OR_DISCUSSION = "QUESTION_OR_DISCUSSION",
	REFINEMENT_OR_REQUIREMENT = "REFINEMENT_OR_REQUIREMENT",
}

function hasRequirementKeyword(text: string): boolean {
	const lower = text.toLowerCase();
	return /\b(also add|change requirement|instead of|new requirement|must also|must not|actually need|do not|please change|refactor|fix|bug|broken|error|fail)\b/i.test(lower);
}

function classifyUserMessage(text: string): UserMessageClassification {
	const trimmed = text.trim();
	if (!trimmed) return UserMessageClassification.CONVERSATIONAL_ACK;

	const lower = trimmed.toLowerCase();
	const clean = lower.replace(/[.,!?;:]+/g, " ").trim();
	const words = clean.split(/\s+/).filter(Boolean);

	const hasReq = hasRequirementKeyword(lower);

	// 1. Explicit user confirmations (standalone or combination of confirmation phrases)
	const confirmPhrases = [
		"yes", "yep", "yeah", "sure", "go ahead", "proceed", "approved", "approve",
		"do it", "confirm", "confirmed", "lgtm", "looks good", "sounds good",
		"start", "implement", "implement it", "let's do it", "lets do it", "let's go", "lets go",
		"continue", "fine by me", "go for it", "go", "please proceed", "please implement",
		"go ahead and implement", "looks good to me", "sounds good to me", "all good"
	];

	const matchesConfirmationPattern = confirmPhrases.some((phrase) => {
		if (clean === phrase) return true;
		if (clean.startsWith(phrase + " ") || clean.endsWith(" " + phrase) || clean.includes(" " + phrase + " ")) {
			if (!hasReq && clean.length < 120) return true;
		}
		return false;
	});

	if (matchesConfirmationPattern) {
		return UserMessageClassification.CONFIRMATION;
	}

	// 2. Pure conversational acknowledgments / greetings / closures
	const ackWords = new Set(["hi", "hello", "hey", "greetings", "thanks", "thank", "you", "thx", "ok", "okay", "k", "got", "it", "cool", "nice", "great", "good", "fine", "done", "quit", "exit", "no", "nope", "bye"]);
	const allAckWords = words.length > 0 && words.every((w) => ackWords.has(w));
	if (allAckWords && !hasReq) {
		return UserMessageClassification.CONVERSATIONAL_ACK;
	}

	// 3. Informational questions / inquiries about status, files, syntax
	const isQuestion = /^(what|where|who|how|why|is there|are there|can you explain|explain|tell me|show me|which|status|how does|what is|what are)\b/i.test(lower) || lower.endsWith("?");
	if (isQuestion && trimmed.length < 250) {
		if (!hasReq) {
			return UserMessageClassification.QUESTION_OR_DISCUSSION;
		}
	}

	// 4. Material requirements / refinements
	return UserMessageClassification.REFINEMENT_OR_REQUIREMENT;
}

function acceptRootConfirmation(pi: ExtensionAPI, ctx?: ExtensionContext): void {
	if (!state.awaitingUserConfirmation) return;
	state.awaitingUserConfirmation = false;
	if (state.active) {
		if (!Array.isArray(state.confirmedQuests)) state.confirmedQuests = [];
		if (!state.confirmedQuests.includes(state.active)) {
			state.confirmedQuests.push(state.active);
		}
	}
	syncImplementationPermission(state);
	persist(pi, ctx);
	updateUIStatus(ctx);
}

function isConfirmationQuestion(
	questionText = "",
	headerText = "",
	inputQuestions?: any[],
	optionsList?: any[],
): boolean {
	const qLower = questionText.toLowerCase();
	const hLower = headerText.toLowerCase();
	const combined = `${hLower} ${qLower}`;

	const confirmKeywords = [
		"proceed", "confirm", "approve", "approval", "implement", "implementation",
		"start", "begin", "go ahead", "apply changes", "ready to proceed",
		"shall i", "should i", "permission", "authorization", "execute plan",
		"next action", "next step", "wrap up", "wrap-up"
	];

	if (confirmKeywords.some((kw) => combined.includes(kw))) {
		return true;
	}

	const checkOptions = (opts?: any[]) => {
		if (!Array.isArray(opts)) return false;
		for (const opt of opts) {
			const optText = (typeof opt === "string" ? opt : opt?.label || opt?.description || "").toLowerCase();
			if (
				optText.includes("proceed") ||
				optText.includes("implement") ||
				optText.includes("go ahead") ||
				optText.includes("approve") ||
				optText.includes("confirm") ||
				optText.includes("start")
			) {
				return true;
			}
		}
		return false;
	};

	if (checkOptions(optionsList)) return true;

	if (Array.isArray(inputQuestions)) {
		for (const q of inputQuestions) {
			if (checkOptions(q?.options)) return true;
		}
	}

	return false;
}

function handleAskQuestionsResult(pi: ExtensionAPI, event: any, ctx: ExtensionContext) {
	if (event.isError || event.error) return;
	const details = event.details;
	if (details && (details.status === "cancelled" || details.status === "unavailable" || details.error)) {
		return;
	}

	const answers: Array<{ question: string; header: string; answer: string; options?: any[] }> = [];

	if (Array.isArray(details?.answers)) {
		for (const a of details.answers) {
			if (typeof a?.answer === "string") {
				const qIndex = typeof a.questionIndex === "number" ? a.questionIndex : -1;
				const matchedQ = qIndex >= 0 && Array.isArray(details.questions) ? details.questions[qIndex] : null;
				answers.push({
					question: typeof a.question === "string" ? a.question : (matchedQ?.question || ""),
					header: typeof a.header === "string" ? a.header : (matchedQ?.header || ""),
					answer: a.answer.trim(),
					options: matchedQ?.options || (Array.isArray(event.input?.questions) && qIndex >= 0 ? event.input.questions[qIndex]?.options : undefined),
				});
			}
		}
	} else if (Array.isArray(event.content)) {
		for (const item of event.content) {
			if (item?.type === "text" && typeof item.text === "string") {
				const lines = item.text.split(/\r?\n/);
				let currentQ = "";
				for (const line of lines) {
					const qMatch = line.match(/Question:\s*(.+)$/i);
					if (qMatch) currentQ = qMatch[1].trim();
					const aMatch = line.match(/Answer:\s*(.+)$/i);
					if (aMatch) {
						answers.push({
							question: currentQ,
							header: "",
							answer: aMatch[1].trim(),
						});
						currentQ = "";
					}
				}
			}
		}
	}

	if (answers.length === 0) return;

	for (const item of answers) {
		const answerText = item.answer;
		if (!answerText) continue;

		const qText = item.question || "";
		const hText = item.header || "";
		const isConfirmQ = isConfirmationQuestion(qText, hText, event.input?.questions, item.options);

		const classification = classifyUserMessage(answerText);

		if (isConfirmQ && classification === UserMessageClassification.CONFIRMATION) {
			acceptRootConfirmation(pi, ctx);
		} else if (
			classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT &&
			hasRequirementKeyword(answerText)
		) {
			if (state.active) {
				if (!Array.isArray(state.refinements)) state.refinements = [];
				state.refinements.push(answerText);
				if (!Array.isArray(state.prompts)) state.prompts = [];
				state.prompts.push(answerText);
				if (state.prompts.length > PROMPT_MAX_COUNT) {
					state.prompts = [state.prompts[0], ...state.prompts.slice(-(PROMPT_MAX_COUNT - 1))];
				}
				if (state.refinements.length > PROMPT_MAX_COUNT) {
					state.refinements = state.refinements.slice(-PROMPT_MAX_COUNT);
				}
				triggerReassessment(state, `User refinement received via ask_questions: "${answerText.slice(0, 100)}..."`, answerText);
				persist(pi, ctx);
				updateUIStatus(ctx);
			}
		}
	}
}

function isMutationTool(toolName: string, input?: any): boolean {
	const normName = (toolName || "").toLowerCase();
	if (normName === "edit" || normName === "write" || normName === "user_edit" || normName === "user_write") {
		const rawPath = typeof input?.path === "string" ? input.path : "";
		const normPath = rawPath.replace(/^\.\//, "").replace(/\\/g, "/");
		// Allow updating the active quest file or future drafts during research
		if (state.active && normPath === questPath(state.active)) {
			return false;
		}
		if (normPath.startsWith(`${QUEST_DIR}/`) || normPath.startsWith(`${FUTURE_DIR}/`)) {
			return false;
		}
		return true; // Any other file write/edit is an implementation mutation!
	}
	return false;
}

function installToolCallGate(pi: ExtensionAPI) {
	pi.on(
		"tool_call",
		withContext(async (event: any, ctx: ExtensionContext) => {
			if (!state.active && !state.pendingRootQuest) return;
			const toolName = event?.toolName || "";
			const isMutation = isMutationTool(toolName, event?.input);
			if (!isMutation) return;

			const gate = getImplementationBlockReason(state, ctx);
			if (gate.blocked) {
				const questLabel = state.active ? `docs/current/${state.active}.md` : "(Provisional Root Quest Initializing)";
				const blockMessage = `[Quest Journal Gate: Blocked] Implementation tool '${toolName}' is forbidden while ${gate.stateName}.\n\nQuest: ${questLabel}\nReason: ${gate.reason}\nRequired next step: ${gate.requiredAction}`;
				if (ctx?.hasUI) {
					ctx.ui.notify(`Blocked ${toolName}: ${gate.stateName}`, "warning");
				}
				return {
					block: true,
					reason: blockMessage,
				};
			}
		}),
	);
}

// ---------------------------------------------------------------------------
// State persistence (custom entries survive reloads and branching)
// ---------------------------------------------------------------------------

function persist(pi: ExtensionAPI, ctx?: ExtensionContext) {
	try {
		const snapshot = snapshotState(ctx);
		pi.appendEntry<StoredState>(CUSTOM_TYPE, snapshot);
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
		if (state.pendingRootQuest) {
			stateTag = " [PROVISIONAL RESEARCH]";
		} else if (!fresh) {
			stateTag = " (save pending)";
		} else if (threshold > 0 && tokens !== null && tokens >= threshold) {
			stateTag = " (compaction ready)";
		}

		const text = state.active
			? `✨ quest: ${hier}${tokenInfo}${stateTag}`
			: state.pendingRootQuest
			? `✨ quest: [provisional root]${tokenInfo}${stateTag}`
			: undefined;
		c.ui.setStatus("quest", text);
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
	const reconstructedState: StoredState = latest && (latest.active || latest.pendingRootQuest)
		? {
				active: latest.active || null,
				pendingRootQuest: typeof latest.pendingRootQuest === "boolean" ? latest.pendingRootQuest : false,
				pendingRootRequest: typeof latest.pendingRootRequest === "string" ? latest.pendingRootRequest : null,
				questIdentityEstablished: typeof latest.questIdentityEstablished === "boolean" ? latest.questIdentityEstablished : false,
				saveCount: latest.saveCount || 0,
				compactCount: latest.compactCount || 0,
				prompts: Array.isArray(latest.prompts) ? latest.prompts : [],
				refinements: Array.isArray(latest.refinements) ? latest.refinements : [],
				stack: Array.isArray(latest.stack) ? latest.stack : (latest.active ? [latest.active] : []),
				dirty: typeof latest.dirty === "boolean" ? latest.dirty : false,
				compactionPending: false,
				archiveCompactionPending: null,
				subquestLaunchCompactionPending:
					typeof latest.subquestLaunchCompactionPending === "boolean"
						? latest.subquestLaunchCompactionPending
						: false,
				preCompactionCheckpointPending: false,
				preCompactionSaveRequestPending: false,
				saveGeneration: latest.saveGeneration || null,
				lastSavedHash: latest.lastSavedHash || null,
				economyTokens: typeof latest.economyTokens === "number" ? latest.economyTokens : undefined,
				economyPercent: typeof latest.economyPercent === "number" ? latest.economyPercent : undefined,
				warningMarginTokens: typeof latest.warningMarginTokens === "number" ? latest.warningMarginTokens : undefined,
				subquestCompactTokens: typeof latest.subquestCompactTokens === "number" ? latest.subquestCompactTokens : undefined,
				lastWarnedCompactionTokens: undefined,
				lastPromptAt: typeof latest.lastPromptAt === "number" ? latest.lastPromptAt : Date.now(),
				lastResumePromptAt: typeof latest.lastResumePromptAt === "number" ? latest.lastResumePromptAt : 0,
				lastResumeTarget: typeof latest.lastResumeTarget === "string" ? latest.lastResumeTarget : null,
				lastResumeCompactCount: typeof latest.lastResumeCompactCount === "number" ? latest.lastResumeCompactCount : undefined,
				pickerCancelled: typeof latest.pickerCancelled === "boolean" ? latest.pickerCancelled : false,
				researchRound: typeof latest.researchRound === "number" ? latest.researchRound : 1,
				researchComplete: typeof latest.researchComplete === "boolean" ? latest.researchComplete : false,
				researchRequired: typeof latest.researchRequired === "boolean" ? latest.researchRequired : (!latest.researchComplete),
				reassessmentRequired: typeof latest.reassessmentRequired === "boolean" ? latest.reassessmentRequired : false,
				reassessmentReason: typeof latest.reassessmentReason === "string" ? latest.reassessmentReason : null,
				reassessmentEvidence: typeof latest.reassessmentEvidence === "string" ? latest.reassessmentEvidence : null,
				reassessmentVersion: typeof latest.reassessmentVersion === "number" ? latest.reassessmentVersion : 0,
				resolvedReassessmentVersion: typeof latest.resolvedReassessmentVersion === "number" ? latest.resolvedReassessmentVersion : 0,
				lastPlanRevisionsText: typeof latest.lastPlanRevisionsText === "string" ? latest.lastPlanRevisionsText : null,
				confirmedQuests: Array.isArray(latest.confirmedQuests) ? latest.confirmedQuests : [],
				lastReassessmentPromptAt: typeof latest.lastReassessmentPromptAt === "number" ? latest.lastReassessmentPromptAt : 0,
				lastReassessmentReason: typeof latest.lastReassessmentReason === "string" ? latest.lastReassessmentReason : null,
				lastCheckpointPromptAt: typeof latest.lastCheckpointPromptAt === "number" ? latest.lastCheckpointPromptAt : 0,
				planVersion: typeof latest.planVersion === "number" ? latest.planVersion : 1,
				planConfidence: (latest.planConfidence as any) || "low",
				lastResearchAt: typeof latest.lastResearchAt === "number" ? latest.lastResearchAt : Date.now(),
				lastPlanRevisionAt: typeof latest.lastPlanRevisionAt === "number" ? latest.lastPlanRevisionAt : Date.now(),
				lastPromptedReassessmentVersion: typeof latest.lastPromptedReassessmentVersion === "number" ? latest.lastPromptedReassessmentVersion : 0,
				implementationAllowed: false,
				awaitingUserConfirmation: typeof latest.awaitingUserConfirmation === "boolean" ? latest.awaitingUserConfirmation : false,
				consecutiveFailures: typeof latest.consecutiveFailures === "number" ? latest.consecutiveFailures : 0,
				substantiveTurnsSinceCheckpoint: typeof latest.substantiveTurnsSinceCheckpoint === "number" ? latest.substantiveTurnsSinceCheckpoint : 0,
		  }
		: createDefaultState();
	syncImplementationPermission(reconstructedState);
	setSessionState(ctx, reconstructedState);
	updateUIStatus(ctx);
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

function generateSlugFromPrompt(prompt: string, maxLen = 45): string {
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

function shouldStartPersistentQuest(prompt: string): boolean {
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

function initProvisionalRootQuest(pi: ExtensionAPI, ctx: ExtensionContext, prompt: string): void {
	state.pendingRootQuest = true;
	state.pendingRootRequest = prompt;
	state.questIdentityEstablished = false;
	state.active = null;
	state.stack = [];
	state.prompts = [prompt];
	state.refinements = [];
	state.dirty = false;
	state.saveGeneration = null;
	state.lastSavedHash = null;
	state.consecutiveFailures = 0;
	state.substantiveTurnsSinceCheckpoint = 0;
	state.lastReassessmentPromptAt = 0;
	state.lastReassessmentReason = null;
	state.lastCheckpointPromptAt = 0;
	state.researchRound = 1;
	state.researchComplete = false;
	state.researchRequired = true;
	state.reassessmentRequired = false;
	state.reassessmentReason = null;
	state.reassessmentEvidence = null;
	state.reassessmentVersion = 0;
	state.resolvedReassessmentVersion = 0;
	state.lastPlanRevisionsText = null;
	state.planVersion = 1;
	state.planConfidence = "low";
	state.lastResearchAt = Date.now();
	state.lastPlanRevisionAt = Date.now();
	state.awaitingUserConfirmation = false;

	syncImplementationPermission(state);
	persist(pi, ctx);
	updateUIStatus(ctx);
}

async function activateExistingQuest(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	slug: string,
	promptText?: string,
): Promise<boolean> {
	if (!slug) return false;
	const path = questPath(slug);
	const futurePath = `${FUTURE_DIR}/${slug}.md`;

	const isExistingOnDisk = await fileExists(path);
	if (!isExistingOnDisk && (await fileExists(futurePath))) {
		await mkdir(QUEST_DIR, { recursive: true });
		await rename(futurePath, path);
		if (ctx?.hasUI) ctx.ui.notify(`Promoted draft ${futurePath} → ${path}`, "info");
	} else if (!isExistingOnDisk) {
		return false;
	}

	await cleanDraftIfExists(slug, ctx);

	state.pendingRootQuest = false;
	state.pendingRootRequest = null;
	state.questIdentityEstablished = true;
	state.pickerCancelled = false;
	state.active = slug;
	state.stack = [slug];
	state.dirty = false;
	state.saveGeneration = null;
	state.lastSavedHash = null;
	state.consecutiveFailures = 0;
	state.substantiveTurnsSinceCheckpoint = 0;
	state.lastReassessmentPromptAt = 0;
	state.lastReassessmentReason = null;
	state.lastCheckpointPromptAt = 0;

	const loaded = await loadExistingQuestEpistemicState(slug);
	if (loaded && loaded.exists) {
		state.prompts = loaded.originalRequest ? [loaded.originalRequest] : promptText ? [promptText] : [];
		state.refinements = loaded.refinements;
		state.researchRound = loaded.researchRound;
		state.researchComplete = loaded.researchComplete;
		state.researchRequired = loaded.researchRequired;
		state.planVersion = loaded.planVersion;
		state.planConfidence = loaded.planConfidence;
		state.lastPlanRevisionsText = loaded.lastPlanRevisionsText;
		state.reassessmentRequired = loaded.reassessmentRequired;
		state.reassessmentReason = loaded.reassessmentReason;
		state.reassessmentEvidence = loaded.reassessmentEvidence;
		state.reassessmentVersion = loaded.reassessmentVersion;
		state.resolvedReassessmentVersion = loaded.resolvedReassessmentVersion;
		state.lastResearchAt = loaded.lastResearchAt ?? Date.now();
		state.lastPlanRevisionAt = loaded.lastPlanRevisionAt ?? Date.now();
		state.awaitingUserConfirmation = !loaded.researchComplete;
	}

	syncImplementationPermission(state);
	await verifyAndMarkSaved(pi, ctx, slug);
	persist(pi, ctx);
	updateUIStatus(ctx);

	return true;
}

async function ensureRootQuestForPrompt(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	prompt: string,
): Promise<boolean> {
	if (state.active) return false;
	const trimmed = prompt.trim();
	if (!trimmed) return false;

	const currentFiles = await listQuestFiles(QUEST_DIR);
	for (const f of currentFiles) {
		const s = f.replace(/\.md$/, "");
		if (s.length >= 3 && trimmed.toLowerCase().includes(s.toLowerCase())) {
			return await activateExistingQuest(pi, ctx, s, trimmed);
		}
	}
	const futureFiles = await listQuestFiles(FUTURE_DIR);
	for (const f of futureFiles) {
		const s = f.replace(/\.md$/, "");
		if (s.length >= 3 && trimmed.toLowerCase().includes(s.toLowerCase())) {
			return await activateExistingQuest(pi, ctx, s, trimmed);
		}
	}

	initProvisionalRootQuest(pi, ctx, trimmed);
	return true;
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

			if (currentLevel <= 2 && body.includes("###")) {
				const subLines = body.split(/\r?\n/);
				let subHeading: string | null = null;
				let subLevel = 0;
				let subBodyLines: string[] = [];
				let subInCode = false;

				const flushSub = () => {
					if (subHeading !== null) {
						const subNorm = subHeading.trim().toLowerCase();
						const subBody = subBodyLines.join("\n").trim();
						if (!sections.has(subNorm)) {
							sections.set(subNorm, {
								heading: subHeading,
								normalized: subNorm,
								level: subLevel,
								body: subBody,
								raw: `### ${subHeading}\n${subBody}`,
							});
						}
					}
					subBodyLines = [];
				};

				for (const sLine of subLines) {
					if (/^\s*(```|~~~)/.test(sLine)) {
						subInCode = !subInCode;
						subBodyLines.push(sLine);
						continue;
					}
					const subMatch = sLine.match(/^(#{3,6})\s+(.+)$/);
					if (subMatch && !subInCode) {
						flushSub();
						subLevel = subMatch[1].length;
						subHeading = subMatch[2].trim();
					} else {
						subBodyLines.push(sLine);
					}
				}
				flushSub();
			}
		}
		currentBodyLines = [];
	};

	for (const line of lines) {
		if (/^\s*(```|~~~)/.test(line)) {
			inCodeBlock = !inCodeBlock;
			currentBodyLines.push(line);
			continue;
		}

		const headingMatch = line.match(/^(#{1,2})\s+(.+)$/);
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

		const sections = parseMarkdownSections(content);
		const subSec = sections.get("sub-quests") || sections.get("subquests") || sections.get("sub quests");

		if (subSec && subSec.body && subSec.body.includes(`[[${childSlug}]]`)) {
			return true;
		}

		if (subSec) {
			const cleanedBody = subSec.body.replace(/- \[\s*\]\s*(\n|$)/g, "").replace(/^-\s*(\n|$)/gm, "").trimEnd();
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

			const match = content.match(/(##\s+(?:Guidelines|Rules|Invariants|Mandatory Guidelines)[\s\S]*?)(?=\n##\s+|$)/i);
			if (match && match[1].trim()) {
				return `# MANDATORY PROJECT INVARIANTS & GUIDELINES (Strictly Enforced from \`${file}\`)\nCRITICAL: The following guidelines are absolute architectural invariants. You MUST strictly adhere to them across all turns without exception.\n\n${match[1].trim()}`;
			}

			if (content.length <= 2500) {
				return `# MANDATORY PROJECT INVARIANTS & GUIDELINES (Strictly Enforced from \`${file}\`)\nCRITICAL: The following guidelines are absolute architectural invariants. You MUST strictly adhere to them across all turns without exception.\n\n${content}`;
			}
		} catch {
			// file unreadable or missing
		}
	}
	return null;
}

function usagePercent(ctx: ExtensionContext): number {
	const u = typeof ctx.getContextUsage === "function" ? ctx.getContextUsage() : undefined;
	if (u && typeof u.percent === "number" && Number.isFinite(u.percent)) return u.percent;
	return 0;
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

		const match = line.match(/^(#{1,6}\s+)(.+)$/);
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
	"parent quest": ["parent quest", "parent", "parentquest"],
	"current status": ["current status", "status"],
	"current understanding": ["current understanding", "understanding", "architectural understanding"],
	"key assumptions": ["key assumptions", "assumptions", "working assumptions"],
	"open questions & uncertainties": ["open questions & uncertainties", "open questions", "uncertainties", "known problems / uncertainties"],
	"research findings": ["research findings", "in-depth analysis & findings", "important findings", "findings", "analysis & findings", "research / findings", "research & findings", "important discoveries", "discoveries"],
	"plan": ["plan", "detailed multi-stage execution plan", "execution plan", "provisional plan", "plan (provisional)"],
	"plan confidence": ["plan confidence", "confidence"],
	"plan version": ["plan version", "version"],
	"plan revisions": ["plan revisions", "plan revision history", "revisions", "plan history"],
	"last research at": ["last research at", "last research timestamp", "research at", "last research"],
	"last plan revision at": ["last plan revision at", "last plan revision timestamp", "plan revision at", "last plan revision"],
	"research round": ["research round", "research cycle"],
	"reassessment status": ["reassessment status", "reassessment state"],
	"reassessment version": ["reassessment version"],
	"resolved reassessment version": ["resolved reassessment version"],
	"reassessment evidence": ["reassessment evidence"],
	"rejected approaches": ["rejected approaches", "discarded approaches", "failed approaches", "disproved hypotheses"],
	"latest reassessment": ["latest reassessment", "reassessment conclusion", "reassessment", "reassessment findings"],
	"execution snapshot": ["execution snapshot", "execution state", "snapshot", "state"],
	"execution state": ["execution state"],
	"build & run commands": ["build & run commands", "build & run", "commands"],
	"decisions made": ["decisions made", "decisions"],
	"constraints & rules": ["constraints & rules", "constraints", "rules"],
	"files examined": ["files examined", "examined files"],
	"files touched": ["files touched", "files modified", "touched files", "modified files", "files"],
	"test / build status": ["test / build status", "tdd & quality checklist", "test status", "build & test status"],
	"acceptance criteria & polish checklist": ["acceptance criteria & polish checklist", "acceptance criteria", "polish checklist"],
	"sub-quests": ["sub-quests", "subquests", "sub quests"],
	"quest refinements & user feedback loops": ["quest refinements & user feedback loops", "refinements", "user refinements", "feedback loops"],
	"remaining work": ["remaining work", "remaining tasks", "remaining", "checklist"],
	"exact next action": ["exact next action", "next recommended step", "next action", "next step", "next steps"],
	"resume prompt": ["resume prompt", "resume context", "resume briefing"],
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
			renderedBlocks.push(`${block.heading}\n${block.body.trim()}`);
		}
	}

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
					b.startsWith("## Next action") ||
					b.startsWith("## Resume prompt") ||
					b.startsWith("## Resume context")
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

	const isSameAsLastSave =
		state.saveGeneration &&
		state.saveGeneration.path === p &&
		state.saveGeneration.hash === fp.hash &&
		state.saveCount > state.compactCount;

	if (isSameAsLastSave && !state.dirty) {
		state.lastPromptAt = Date.now();
		updateUIStatus(ctx);
		return { success: true, hash: fp.hash, count: state.saveCount };
	}

	state.saveCount = Math.max(state.saveCount + 1, state.compactCount + 1);
	state.lastSavedHash = fp.hash;
	state.saveGeneration = {
		count: state.saveCount,
		path: p,
		hash: fp.hash,
		savedAt: Date.now(),
	};
	state.dirty = false;
	state.preCompactionCheckpointPending = false;
	state.preCompactionSaveRequestPending = false;
	state.lastPromptAt = Date.now();
	persist(pi, ctx);
	updateUIStatus(ctx);

	return { success: true, hash: fp.hash, count: state.saveCount };
}

function isPlaceholderOrEmpty(text: string | undefined): boolean {
	if (!text) return true;
	const trimmed = text.trim();
	if (!trimmed || trimmed === "-" || trimmed === "- [ ]" || trimmed === "- [ ] not started · in progress · blocked · done") return true;

	const placeholderPrefixes = [
		"> Core architectural facts",
		"> Material assumptions",
		"> Material uncertainties",
		"> Factual discoveries",
		"> Multi-stage execution plan",
		"> low · medium · high",
		"> Most justified immediate action",
		"> What we are trying to accomplish",
		"> Paste the verbatim user prompt",
		"> Concise epistemic briefing",
		"> A concise briefing",
		"> What are we proposing to change",
	];

	for (const prefix of placeholderPrefixes) {
		if (trimmed.toLowerCase().startsWith(prefix.toLowerCase())) {
			return true;
		}
	}

	const lines = trimmed
		.split(/\r?\n/)
		.map((l) => l.trim())
		.filter((l) => l && !l.startsWith(">") && l !== "-" && l !== "- [ ]" && l !== "1." && l !== "- [ ] not started · in progress · blocked · done");

	return lines.length === 0;
}

function validateResearchPrerequisites(
	markdownContent: string,
	planConfidence?: string,
	allowLowConfidence = false,
	planConfidenceReason?: string,
): {
	valid: boolean;
	missingSections: string[];
	confidenceIssue?: string;
} {
	const sections = parseMarkdownSections(markdownContent);
	const missingSections: string[] = [];

	const requiredEpistemicKeys: Array<{ key: string; label: string }> = [
		{ key: "current understanding", label: "Current Understanding" },
		{ key: "key assumptions", label: "Key Assumptions" },
		{ key: "research findings", label: "Research Findings" },
		{ key: "open questions & uncertainties", label: "Open Questions & Uncertainties" },
		{ key: "plan", label: "Plan" },
		{ key: "plan confidence", label: "Plan Confidence" },
		{ key: "exact next action", label: "Exact Next Action" },
	];

	for (const req of requiredEpistemicKeys) {
		const aliases = [req.key, ...(SECTION_ALIASES[req.key] || [])];
		let foundSec: MarkdownSection | undefined;
		for (const alias of aliases) {
			const s = sections.get(alias);
			if (s && !isPlaceholderOrEmpty(s.body)) {
				foundSec = s;
				break;
			}
		}
		if (!foundSec) {
			missingSections.push(req.label);
		}
	}

	let confidenceIssue: string | undefined;
	const confSec = sections.get("plan confidence") || sections.get("confidence");
	const confBody = confSec?.body || "";
	const confText = (planConfidence || confBody).toLowerCase();
	const hasLow = confText.includes("low");
	const hasMediumOrHigh = confText.includes("medium") || confText.includes("high");

	if (hasLow && !hasMediumOrHigh) {
		const reasonText = (planConfidenceReason || confBody).trim();
		const reasonSubstantive =
			reasonText.length > 0 &&
			(reasonText.includes("Reason:") || reasonText.includes("justif") || reasonText.includes("acceptable") || (planConfidenceReason && planConfidenceReason.trim().length > 5)) &&
			!isPlaceholderOrEmpty(reasonText);

		if (!allowLowConfidence || !reasonSubstantive) {
			confidenceIssue = "Plan confidence is 'low'. To complete research with low confidence, you must pass allowLowConfidence: true AND provide explicit justification in planConfidenceReason.";
		}
	}

	return {
		valid: missingSections.length === 0 && !confidenceIssue,
		missingSections,
		confidenceIssue,
	};
}

export enum QuestLifecycleState {
	IDLE = "IDLE",
	RESEARCH_PENDING = "RESEARCH_PENDING",
	REASSESSMENT_PENDING = "REASSESSMENT_PENDING",
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
		if (
			threshold > 0 &&
			tokens !== null &&
			tokens >= Math.max(0, threshold - warningMargin) &&
			tokens < threshold &&
			state.dirty
		) {
			return QuestLifecycleState.PRE_COMPACT_DUMP_PENDING;
		}
	}

	if (state.reassessmentRequired) {
		return QuestLifecycleState.REASSESSMENT_PENDING;
	}

	if (state.researchRequired || !state.researchComplete) {
		return QuestLifecycleState.RESEARCH_PENDING;
	}

	if (state.dirty || !compactionReady()) {
		return QuestLifecycleState.ACTIVE_DIRTY;
	}
	return QuestLifecycleState.ACTIVE_CLEAN;
}

function buildWarningSavePrompt(
	activeQuest: string,
	fraction: number,
	tokens: number,
	threshold: number,
): string {
	const isClose = fraction >= 0.5;
	const escalationLabel = isClose ? "Close to Threshold" : "Approaching Threshold";
	const escalationAdvice = isClose
		? "Context is close to the compaction threshold. Prioritize an exhaustive durable checkpoint now. Avoid unnecessary further work."
		: "Context is approaching the compaction threshold. Keep the quest file current and prepare an exhaustive checkpoint.";

	const promptReminder = `Original user request -- keep VERBATIM under ## Original request in the quest file:\n"${originalRequestText()}"${
		state.refinements && state.refinements.length > 0 ? `\n\nUser refinements -- list under ## Quest Refinements & User Feedback Loops:\n${refinementsBlock()}` : ""
	}`;

	return `⚡ **Context Compaction Warning (${escalationLabel}: ${formatTokens(tokens)} / ${formatTokens(threshold)} tokens)**:
Context compaction is imminent. ${escalationAdvice}

Before auto-compaction occurs and resets conversation working memory, perform an EXHAUSTIVE DURABLE STATE SAVE in:
\`${questPath(activeQuest)}\`

${promptReminder}

**Epistemic & Execution State Checklist**:
- Current Status
- Discoveries & Learnings (architectural facts, verified invariants)
- Tested Assumptions (validated / invalidated / uncertain)
- Contradictions & Plan Revisions (if any)
- Files Touched / Examined
- Test / Build Status
- Remaining Work
- EXACT NEXT ACTION (concrete and immediate)

Update the quest file and call \`quest_mark_saved\` to ensure your state is verified before compaction.`;
}

function buildCriticalSavePrompt(
	activeQuest: string,
	tokens: number,
	threshold: number,
): string {
	const promptReminder = `Original user request -- keep VERBATIM under ## Original request in the quest file:\n"${originalRequestText()}"${
		state.refinements && state.refinements.length > 0 ? `\n\nUser refinements -- list under ## Quest Refinements & User Feedback Loops:\n${refinementsBlock()}` : ""
	}`;

	return `🚨 **CRITICAL QUEST JOURNAL COMPACTION SAFETY STATE** 🚨

Context usage (${formatTokens(tokens)} tokens) has reached or exceeded the configured compaction threshold (${formatTokens(threshold)} tokens).

Before doing ANY further substantive work, you MUST perform a FINAL EXHAUSTIVE DURABLE STATE SAVE.

Do NOT continue implementation merely because the current task is unfinished.
Do NOT defer the checkpoint. STOP treating checkpointing as optional.
Do NOT assume the existing quest file is sufficient.
Do NOT provide a short summary in place of the checkpoint.

FIRST:
1. Reconstruct the current state of the work.
2. Update the active quest file exhaustively in \`${questPath(activeQuest)}\`.
3. Preserve discoveries, assumptions, contradictions, decisions, rejected approaches, files touched, verification state, remaining work, and the EXACT NEXT ACTION.
4. Call quest_mark_saved.
5. Ensure the save is actually verified.

${promptReminder}

ONLY AFTER THE DURABLE SAVE IS VERIFIED may you continue ordinary work.`;
}

function buildCriticalCompactionReadyPrompt(
	activeQuest: string,
	tokens: number,
	threshold: number,
): string {
	return `🚨 **CRITICAL QUEST JOURNAL COMPACTION SAFETY STATE (DURABLE STATE SAVED)** 🚨

Context usage (${formatTokens(tokens)} tokens) remains at or above the configured compaction threshold (${formatTokens(threshold)} tokens).

Your durable quest checkpoint in \`${questPath(activeQuest)}\` is VERIFIED and ready.

Context auto-compaction is now required. Stand by for auto-compaction across the turn boundary.
Do NOT begin large new implementation streams before compaction resets working memory.
Ensure your exact next action is fully documented so execution can resume cleanly post-compaction.`;
}

let lastPreCompactionSteerTime = 0;

function requestPreCompactionCheckpoint(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	force = false,
): boolean {
	const c = getActiveContext(ctx);
	if (!c || !state.active) return false;
	if (state.compactionPending) return false;
	if (state.pickerCancelled) return false;

	const now = Date.now();
	if (!force && now - lastPreCompactionSteerTime < 50) {
		return false;
	}

	const { pressure, tokens, threshold, fraction } = getCompactionPressure(c);
	if (pressure === CompactionPressure.NONE || tokens === null) {
		return false;
	}

	lastPreCompactionSteerTime = now;
	state.lastWarnedCompactionTokens = tokens;
	persist(pi, c);

	if (pressure === CompactionPressure.CRITICAL) {
		state.preCompactionCheckpointPending = true;
		state.preCompactionSaveRequestPending = true;
		const isReady = compactionReady();
		const text = isReady
			? buildCriticalCompactionReadyPrompt(state.active, tokens, threshold)
			: buildCriticalSavePrompt(state.active, tokens, threshold);

		sendInternalAgentMessage(pi, text, "followUp");

		if (c.hasUI) {
			const saveStatus = isReady ? "saved & ready for compaction" : "SAVE REQUIRED IMMEDIATELY";
			c.ui.notify(
				`🚨 Quest-journal: CRITICAL context pressure (${formatTokens(tokens)}/${formatTokens(threshold)}) for '${state.active}' [${saveStatus}].`,
				"error",
			);
		}
		return true;
	}

	if (pressure === CompactionPressure.WARNING) {
		state.preCompactionCheckpointPending = true;
		const text = buildWarningSavePrompt(state.active, fraction, tokens, threshold);

		sendInternalAgentMessage(pi, text, "followUp");

		if (c.hasUI) {
			const levelStr = fraction < 0.5 ? "approaching" : "close to";
			c.ui.notify(
				`Quest-journal: context ${levelStr} compaction threshold for '${state.active}' (${formatTokens(tokens)}/${formatTokens(threshold)}).`,
				"warning",
			);
		}
		return true;
	}

	return false;
}

function checkAndTriggerDeferredCompaction(pi: ExtensionAPI, ctx?: ExtensionContext): boolean {
	const c = getActiveContext(ctx);
	if (!c) return false;
	if (state.pickerCancelled) return false;
	if (state.compactionPending) return false;
	if (typeof c.compact !== "function") return false;

	// 1. Check if archive compaction was requested (e.g. sub-quest finished and returning to parent)
	if (state.archiveCompactionPending) {
		const targetName = state.archiveCompactionPending;
		state.compactionPending = true;
		persist(pi, c);

		const targetSessionId = getSessionId(c);
		const parentName = state.active;
		const instructions = parentName
			? `Sub-quest '${targetName}' completed and archived. Returning to parent quest '${parentName}'. Focus summary on key architecture decisions, completed sub-quest findings, and remaining parent roadmap. Parent quest state is safely preserved on disk in docs/current/${parentName}.md. Following compaction, read docs/current/${parentName}.md first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified parent action.`
			: `Quest '${targetName}' completed and archived. Focus summary on key architecture decisions, completed work, and remaining roadmap.`;

		setTimeout(() => {
			asyncContext.run(c, () => {
				const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
				try {
					c.compact!({
						customInstructions: instructions,
						onComplete: () => {},
						onError: (err: any) => {
							sessionState.compactionPending = false;
							sessionState.archiveCompactionPending = null;
							sessionState.preCompactionCheckpointPending = false;
							sessionState.preCompactionSaveRequestPending = false;
							const msg = err?.message || String(err);
							if (msg.includes("Nothing to compact") || msg.includes("Already compacted") || msg.includes("cancelled") || msg.includes("session too small")) {
								if (parentName) {
									sendPostCompactionResumePrompt(pi, parentName, false, c);
								}
								return;
							}
							if (c.hasUI) c.ui.notify(`Post-archive compaction failed: ${msg}`, "error");
							if (parentName) {
								sendPostCompactionResumePrompt(pi, parentName, false, c);
							}
						},
					});
				} catch (err: any) {
					sessionState.compactionPending = false;
					sessionState.archiveCompactionPending = null;
					sessionState.preCompactionCheckpointPending = false;
					sessionState.preCompactionSaveRequestPending = false;
					logError("Compaction error following archive", err, c);
					if (parentName) {
						sendPostCompactionResumePrompt(pi, parentName, false, c);
					}
				}
			});
		}, 50);
		return true;
	}

	// 2. Check if sub-quest launch compaction is pending
	if (state.subquestLaunchCompactionPending) {
		const childName = state.active;
		const subLaunchThreshold = getSubquestCompactThreshold();
		const tokens = calculateCurrentTokens(c);

		if (childName && compactionReady(childName) && subLaunchThreshold > 0 && tokens !== null && tokens >= subLaunchThreshold) {
			state.compactionPending = true;
			persist(pi, c);

			const isSubQuest = Array.isArray(state.stack) && state.stack.length > 1;
			const parentName = isSubQuest ? state.stack[state.stack.length - 2] : null;
			const instructions = parentName
				? `Launching sub-quest '${childName}' (parent: '${parentName}'). Focus summary on parent quest status, key architectural decisions, and why sub-quest '${childName}' was launched. Child sub-quest state is safely saved on disk in docs/current/${childName}.md. Following compaction, read docs/current/${childName}.md first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified next action.`
				: `Launching sub-quest '${childName}'. Focus summary on key architectural decisions and why sub-quest '${childName}' was launched. Child sub-quest state is safely saved on disk in docs/current/${childName}.md. Following compaction, read docs/current/${childName}.md first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified next action.`;

			const targetSessionId = getSessionId(c);
			setTimeout(() => {
				asyncContext.run(c, () => {
					const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
					try {
						c.compact!({
							customInstructions: instructions,
							onComplete: () => {},
							onError: (err: any) => {
								sessionState.compactionPending = false;
								sessionState.subquestLaunchCompactionPending = false;
								sessionState.preCompactionCheckpointPending = false;
								sessionState.preCompactionSaveRequestPending = false;
								const msg = err?.message || String(err);
								if (!msg.includes("Nothing to compact") && !msg.includes("Already compacted") && !msg.includes("session too small")) {
									if (c.hasUI) c.ui.notify(`Sub-quest launch compaction failed: ${msg}`, "error");
								}
								if (childName) {
									sendPostCompactionResumePrompt(pi, childName, false, c);
								}
							},
						});
					} catch (err: any) {
						sessionState.compactionPending = false;
						sessionState.subquestLaunchCompactionPending = false;
						sessionState.preCompactionCheckpointPending = false;
						sessionState.preCompactionSaveRequestPending = false;
						logError("Sub-quest launch compaction scheduling failed", err, c);
						if (childName) {
							sendPostCompactionResumePrompt(pi, childName, false, c);
						}
					}
				});
			}, 50);
			return true;
		}
	}

	return false;
}

/** A fresh save of the quest file -- increments the save gate so compaction may run. */
async function markSaved(pi: ExtensionAPI, ctx?: ExtensionContext) {
	await verifyAndMarkSaved(pi, ctx);
}

/** True when compaction should be allowed: active quest has a fresh save matching its current path since last compaction. */
function compactionReady(expectedSlug?: string): boolean {
	const target = expectedSlug || state.active;
	if (!target) return false;
	const p = questPath(target);
	const hasValidGen = Boolean(state.saveGeneration && state.saveGeneration.path === p);
	const hasSaveSinceCompact = state.saveCount > state.compactCount;
	return hasValidGen && hasSaveSinceCompact && !state.dirty;
}

const SYNTHETIC_PROMPT_PREFIXES = [
	"/quest",
	"/subquest",
	"/sub-quest",
	"/quest-save",
	"/quest-refine",
	"/quest-del",
	"/quest-draft",
	"/quest-economy",
	"/quest-warning",
	"/quest-subquest-threshold",
	"/quest-status",
	"/quests",
	"quest-journal:",
	"quest-journal economy:",
	"quest-journal in-flight steer:",
	"quest-journal in-flight budget steer:",
	"⚡",
	"[questjournal]",
	"**post-compaction",
	"⚡ **post-compaction",
	"now working on quest",
	"now working on sub-quest",
	"finish current work, then update that file",
	"before context compaction resets",
	"original user request(s) for this quest",
	"original user request --",
	"sub-quest '",
	"quest '",
	"economy auto-compaction",
	"turn has performed",
	"turn 1 protocol",
	"mandatory upfront research",
	"mandatory tdd & quality",
	"context is approaching the configured compaction threshold",
	"context compaction is now being requested",
	"context compaction is imminent",
	"🚨",
	"🚨 **critical",
	"critical quest journal compaction",
	"context compaction warning",
	"context usage has reached or exceeded",
];

const INTERNAL_MESSAGE_PREFIX = "[QuestJournal internal]";

function shouldCapturePrompt(text: string): boolean {
	const t = text.trim();
	if (!t || t.length < 2) return false;
	if (t.startsWith("/")) return false;
	const lower = t.toLowerCase();
	if (lower.startsWith(INTERNAL_MESSAGE_PREFIX.toLowerCase())) return false;
	for (const p of SYNTHETIC_PROMPT_PREFIXES) {
		if (lower.startsWith(p)) return false;
	}
	if (lower.includes("post-compaction autonomous resumption directive")) return false;
	if (lower.includes("pre-compaction exhaustive context preservation protocol")) return false;
	if (lower.includes("context is approaching the configured compaction threshold")) return false;
	if (lower.includes("context compaction is now being requested")) return false;
	if (lower.includes("context compaction is imminent")) return false;
	if (lower.includes("final exhaustive durable state save")) return false;
	if (lower.includes("critical quest journal compaction safety state")) return false;
	if (lower.includes("context compaction warning")) return false;
	if (lower.includes("context usage has reached or exceeded")) return false;
	if (lower.includes("compaction safety state")) return false;
	return true;
}

function originalRequestText(): string {
	if (!state.prompts || state.prompts.length === 0) {
		return "(none captured yet -- this is the first substantive request; use the current user message)";
	}
	return state.prompts[0];
}

function refinementsBlock(): string {
	if (!state.refinements || state.refinements.length === 0) {
		return "(none captured yet)";
	}
	return state.refinements.map((r, i) => `${i + 1}. ${r}`).join("\n\n");
}

function promptsBlock(): string {
	const orig = originalRequestText();
	if (!state.refinements || state.refinements.length === 0) {
		return orig;
	}
	const refs = state.refinements.map((r, i) => `Refinement ${i + 1}: ${r}`).join("\n");
	return `Original Request:\n${orig}\n\nUser Refinements:\n${refs}`;
}

function safeSendUserMessage(pi: ExtensionAPI, text: string, options?: { deliverAs?: "steer" | "followUp" | "nextTurn"; expandPromptTemplates?: boolean }) {
	try {
		if (options) {
			pi.sendUserMessage(text, options);
		} else {
			pi.sendUserMessage(text);
		}
	} catch (err: any) {
		logError("Failed to send user message to agent", err);
	}
}

function sendInternalAgentMessage(
	pi: ExtensionAPI,
	text: string,
	deliverAs: "steer" | "followUp" | "nextTurn" = "followUp",
) {
	if (typeof pi.sendMessage === "function") {
		try {
			pi.sendMessage(
				{
					customType: "quest_journal",
					content: text,
					display: false,
				},
				{
					deliverAs,
				},
			);
			return;
		} catch (err: any) {
			logError("Failed to send internal custom message", err);
		}
	}
	const marked = text.startsWith(INTERNAL_MESSAGE_PREFIX) ? text : `${INTERNAL_MESSAGE_PREFIX}\n${text}`;
	safeSendUserMessage(pi, marked, { deliverAs });
}

function sendInternalUserMessage(pi: ExtensionAPI, text: string, options?: { deliverAs?: "steer" | "followUp" | "nextTurn"; expandPromptTemplates?: boolean }) {
	const marked = text.startsWith(INTERNAL_MESSAGE_PREFIX) ? text : `${INTERNAL_MESSAGE_PREFIX}\n${text}`;
	safeSendUserMessage(pi, marked, options);
}

/** Queue a user message asking the model to update the quest file with standard prompt. */
function sendSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request -- keep VERBATIM under ## Original request in the quest file:\n"${originalRequestText()}"${
		state.refinements && state.refinements.length > 0 ? `\n\nUser refinements -- list under ## Quest Refinements & User Feedback Loops:\n${refinementsBlock()}` : ""
	}`;
	const text = `${message}\n\n${promptReminder}\n\nActive quest file: \`${questPath(state.active)}\`\n\nUpdate that file with the latest state (goal, progress, decisions, files touched, findings, Test / Build Status, remaining work, next step). Ensure external working memory is updated accurately, then call \`quest_mark_saved\`.`;
	sendInternalAgentMessage(pi, text, "followUp");
}

function triggerReassessment(
	targetState: StoredState,
	reason: string,
	evidence?: string | null,
) {
	targetState.reassessmentVersion = (targetState.reassessmentVersion || 0) + 1;
	targetState.reassessmentRequired = true;
	targetState.reassessmentReason = reason;
	targetState.reassessmentEvidence = evidence ?? null;
	targetState.researchRequired = true;
	targetState.researchComplete = false;
	targetState.planConfidence = "low";
	targetState.researchRound = (targetState.researchRound || 1) + 1;
	targetState.dirty = true;
	targetState.awaitingUserConfirmation = false;
	if (Array.isArray(targetState.confirmedQuests) && targetState.active) {
		targetState.confirmedQuests = targetState.confirmedQuests.filter((q) => q !== targetState.active);
	}
	syncImplementationPermission(targetState);
}

function buildReassessmentPrompt(
	activeQuest: string,
	reason: string,
	evidence?: string | null,
	planVersion?: number,
): string {
	const evidenceBlock = evidence ? `\n\n**Contradictory Evidence & Observed Output**:\n\`\`\`\n${evidence.slice(0, 1500)}\n\`\`\`` : "";
	const versionStr = typeof planVersion === "number" ? ` (current plan: v${planVersion})` : "";

	return `⚡ **Quest Reassessment Required**: Evidence has been encountered that challenges or invalidates the current plan for **${activeQuest}**${versionStr}.

**Trigger Reason**: ${reason}${evidenceBlock}

**Reassessment & Falsification Directive**:
Do not blindly push forward with the current plan, and do not merely declare the plan valid without investigation.

1. **Recover Prior Belief**: What did we assume or expect to happen?
2. **Inspect New Evidence**: What specific contradiction, test failure, unexpected execution path, or child finding occurred?
3. **Perform Targeted Fresh Investigation**: Use \`read\` / \`search_graph\` to investigate the specific contradiction. Determine where the mental model diverged from reality.
4. **Validate or Falsify Assumptions**: Determine whether previous assumptions were invalid or need reformulation.
5. **Decide Plan Validity & Revise**: Decide whether the plan survives or requires revision. If revising, record the previous plan, invalidating evidence, and new plan under \`## Plan Revisions\` and \`## Rejected Approaches\`.
6. **Persist & Explicitly Complete Reassessment**:
   Do not call \`reassessmentComplete\` merely because the quest file was updated.
   Reassessment is complete only after you have:
   - investigated the triggering contradiction;
   - established what was actually true;
   - validated, invalidated, or reformulated the relevant assumptions;
   - determined whether the current plan survives;
   - revised the plan if necessary;
   - persisted the new evidence and reasoning in \`docs/current/${activeQuest}.md\`;
   - provided a justified confidence level and concrete next action via \`quest_update_state({ reassessmentComplete: true, ... })\`.
7. **Continue Execution**: Proceed autonomously with the revised Exact Next Action.`;
}

function buildResearchCheckpointPrompt(activeQuest: string, planVersion?: number, planConfidence?: string): string {
	const verStr = typeof planVersion === "number" ? `v${planVersion}` : "v1";
	const confStr = planConfidence || "unspecified";

	return `⚡ **Periodic Durable Memory & Reasoning Checkpoint**: Substantial execution has occurred on **${activeQuest}** (Current Plan: ${verStr}, Confidence: ${confStr}).

**Reasoning & Decision Questions**:
1. **Discoveries & Learnings**: What concrete facts or data paths have been established in recent turns?
2. **Assumption Verification**: Have any key assumptions been disproved or weakened?
3. **Plan Evaluation & Epistemic Action**: Choose the most justified path and act accordingly:
   - **If current understanding is well supported** -> Save durable state and continue execution.
   - **If an important uncertainty exists** -> Perform targeted research pass before continuing to write code.
   - **If evidence contradicts current model** -> Enter reassessment and update \`## Rejected Approaches\`.
   - **If plan changed** -> Update plan and record the revision in \`## Plan Revisions\` (auto-increments planVersion).
4. **Exact Next Action**: What is the most justified immediate execution step?

**Action Required**:
Update \`docs/current/${activeQuest}.md\` (using \`quest_update_state\` or edit + \`quest_mark_saved\`), then proceed autonomously.`;
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
	} else if (state.pendingRootQuest) {
		lines.push(
			`- Active quest: [PROVISIONAL ROOT INITIALIZATION] Research required to establish quest identity and plan. Use read/search/bash tools to investigate, determine a concise semantic quest name (e.g. 'persistent-agent-research', 'oauth-login-flow'), and call quest_update_state to initialize the durable quest with your research findings. Original user request is captured in session state.`,
		);
	} else {
		lines.push("- Active quest: none (substantive requests will automatically receive a persistent quest context in docs/current/).");
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

function installTurnEnd(pi: ExtensionAPI) {
	pi.on(
		"turn_end",
		withContext(async (event: any, ctx: ExtensionContext) => {
			if (state.pickerCancelled) return;
			if (state.compactionPending) return;

			// Handle deferred archive compaction even if active quest is now null
			if (state.archiveCompactionPending) {
				checkAndTriggerDeferredCompaction(pi, ctx);
				return;
			}

			if (!state.active) return;

			const toolResults: any[] = Array.isArray(event.toolResults) ? event.toolResults : [];
			let didUpdateQuestThisTurn = false;
			let isSubstantiveTurn = false;
			let meaningfulFailureDetected = false;
			let failureReason = "";

			let failureEvidence = "";

			for (const tr of toolResults) {
				const toolName = (tr?.toolName || tr?.name || "").toLowerCase();

				const toolFailed =
					!!tr?.isError ||
					!!tr?.error ||
					!!(tr?.details && (
						tr.details.error ||
						tr.details.success === false
					));

				// Detect command/test failure in bash tools
				if (toolName === "bash" || toolName === "user_bash") {
					const output = typeof tr?.content === "string" ? tr.content :
						Array.isArray(tr?.content) ? tr.content.map((c: any) => c.text || "").join("\n") :
						typeof tr?.output === "string" ? tr.output : "";
					const cmd = tr?.args?.command || tr?.input?.command || tr?.command || "";

					const isTestOrBuild =
						/make\s+test|deno\s+test|npm\s+test|pytest|cargo\s+test|jest|vitest|make\b/i.test(cmd);

					const hasFailureSignals =
						toolFailed ||
						/\b(?:FAIL|FAILED|assertion failed|panic:|Segmentation fault|make:\s*\*\*\*|TypeError|SyntaxError)\b/i.test(output);

					if (hasFailureSignals && isTestOrBuild) {
						meaningfulFailureDetected = true;
						failureReason = `Test/build command failed: ${cmd || "test execution"}`;
						failureEvidence = output ? output.slice(0, 1500) : `Command '${cmd}' failed`;
					} else if (toolFailed) {
						meaningfulFailureDetected = true;
						failureReason = `Command failed with error: ${cmd || "bash command"}`;
						failureEvidence = output ? output.slice(0, 1500) : `Command '${cmd}' failed with error`;
					}
				}

				if (
					!toolFailed &&
					(
						toolName.includes("quest_update_state") ||
						toolName.includes("quest_mark_saved") ||
						toolName.includes("quest_journal_mark_saved") ||
						toolName.includes("quest_archive") ||
						toolName.includes("quest_subquest") ||
						toolName.includes("quest_journal_archive") ||
						toolName.includes("quest_journal_subquest")
					)
				) {
					didUpdateQuestThisTurn = true;
				}

				if (toolName === "edit" || toolName === "write") {
					const targetPath = tr?.args?.path || tr?.input?.path || "";

					if (
						targetPath &&
						targetPath.includes(`docs/current/${state.active}.md`)
					) {
						if (!toolFailed) {
							didUpdateQuestThisTurn = true;
						}
					} else if (!toolFailed) {
						isSubstantiveTurn = true;
					}
				} else if (
					!toolFailed &&
					(
						toolName === "bash" ||
						toolName === "user_bash" ||
						toolName === "subagent" ||
						toolName.startsWith("bg_run") ||
						toolName.startsWith("fusion_") ||
						toolName === "doc_to_md"
					)
				) {
					isSubstantiveTurn = true;
				}
			}

			if (meaningfulFailureDetected) {
				state.consecutiveFailures = (state.consecutiveFailures || 0) + 1;
				triggerReassessment(state, failureReason, failureEvidence);
				persist(pi, ctx);
				updateUIStatus(ctx);
			} else if (didUpdateQuestThisTurn) {
				state.substantiveTurnsSinceCheckpoint = 0;
				persist(pi, ctx);
				updateUIStatus(ctx);
			} else if (isSubstantiveTurn) {
				state.dirty = true;
				state.substantiveTurnsSinceCheckpoint = (state.substantiveTurnsSinceCheckpoint || 0) + 1;
				persist(pi, ctx);
				updateUIStatus(ctx);
			}

			// Proactive context-pressure save request before compaction
			requestPreCompactionCheckpoint(pi, ctx);

			// Check deferred (archive / subquest launch) compaction
			checkAndTriggerDeferredCompaction(pi, ctx);
		}),
	);
}

function installBeforeCompact(pi: ExtensionAPI) {
	pi.on(
		"session_before_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			if (!compactionReady()) {
				state.compactionPending = false;
				state.preCompactionCheckpointPending = true;
				state.preCompactionSaveRequestPending = true;
				persist(pi, ctx);

				const activeFile = questPath(state.active);
				const msg = `⚡ **Compaction Blocked (Unsaved Working Memory)**:
Compaction is blocked because the active quest file \`${activeFile}\` contains unsaved changes or unverified state.

To allow auto-compaction and preserve continuity across the boundary:
1. Update \`${activeFile}\` with your current understanding, decisions, plan confidence, remaining work, and exact next step.
2. Call \`quest_mark_saved\` to persist the state.
Once saved, auto-compaction will safely proceed.`;

				sendInternalAgentMessage(pi, msg, "followUp");

				if (ctx?.hasUI) {
					ctx.ui.notify(`Quest-journal: blocking compaction until '${activeFile}' is saved.`, "warning");
				}
				return { cancel: true };
			}

			// Just before compaction: queue the post-compaction resumption follow-up
			sendPostCompactionResumePrompt(pi, state.active, true, ctx);
		}),
	);
}

function handleCompactionCompleted(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	completedAtTokens?: number | null,
): void {
	const c = getActiveContext(ctx);
	const targetSessionId = getSessionId(c);
	const sessionState = sessionStates.get(targetSessionId) ?? getState(c);

	const tokens = typeof completedAtTokens === "number" ? completedAtTokens : calculateCurrentTokens(c);

	sessionState.compactionPending = false;
	sessionState.dirty = false;
	sessionState.lastWarnedCompactionTokens = null;
	sessionState.preCompactionCheckpointPending = false;
	sessionState.preCompactionSaveRequestPending = false;
	sessionState.subquestLaunchCompactionPending = false;
	sessionState.archiveCompactionPending = null;
	sessionState.compactCount = sessionState.saveCount;

	persist(pi, c);

	if (c?.hasUI) {
		if (typeof tokens === "number" && tokens > 0) {
			c.ui.notify(`Economy auto-compaction completed at ${formatTokens(tokens)} tokens.`, "info");
		}
		updateUIStatus(c);
	}

	if (sessionState.active && sessionState.lastResumeCompactCount !== sessionState.compactCount) {
		sendPostCompactionResumePrompt(pi, sessionState.active, true, c);
	}
}

function buildPostCompactionResumeDirective(
	activeQuest: string,
	targetState: StoredState,
): string {
	const isSubQuest = Array.isArray(targetState.stack) && targetState.stack.length > 1;
	const parentQuest = isSubQuest ? targetState.stack[targetState.stack.length - 2] : null;

	const subquestContext = isSubQuest
		? `You are inside sub-quest **${activeQuest}** (parent: **${parentQuest}**).
This sub-quest is temporary work in service of the parent/root objective. Completing this sub-quest does not mean the overall objective is complete. After finishing it, return to the parent quest and continue its remaining work.`
		: `You are working on active quest **${activeQuest}**.`;

	let specificAction = "";
	if (targetState.reassessmentRequired) {
		specificAction = `⚡ **State: REASSESSMENT_PENDING** (Reason: ${targetState.reassessmentReason || "Unresolved contradiction"}).
1. Read \`docs/current/${activeQuest}.md\` using \`read\`.
2. Do NOT jump into implementation. First investigate the contradiction, challenge previous assumptions, and evaluate whether the current plan is still valid.
3. Update the quest file with the revised plan and call \`quest_update_state({ reassessmentComplete: true })\`.
4. Proceed with the revised Exact Next Action.`;
	} else if (targetState.researchRequired || !targetState.researchComplete) {
		specificAction = `⚡ **State: RESEARCH_PENDING** (Research Round: ${targetState.researchRound || 1}).
1. Read \`docs/current/${activeQuest}.md\` using \`read\`.
2. Complete the required research & falsification pass before writing feature code.
3. Establish Current Understanding, Key Assumptions, and provisional Plan with medium/high confidence.
4. Update the quest file and call \`quest_update_state({ researchComplete: true })\`.
5. Only then proceed to implementation / confirmation.`;
	} else {
		specificAction = `⚡ **State: PLAN_ESTABLISHED** (Plan: v${targetState.planVersion || 1}, Confidence: ${targetState.planConfidence || "high"}).
1. Read \`docs/current/${activeQuest}.md\` using \`read\`.
2. Validate whether the current plan is still supported by the recovered state. Do not repeat research merely to reconstruct lost context; use the quest file to recover established knowledge. However, if an important assumption is uncertain, tests disagree with the model, or the plan no longer explains observed behavior, re-investigate that specific aspect before executing.
3. If the plan is established and supported, proceed directly with executing the justified EXACT NEXT ACTION without waiting for user commands and without modal questions.`;
	}

	return `⚡ **Post-Compaction Autonomous Resumption Directive**:
Context compaction has finished. Working memory has been cleanly reset.

${subquestContext}
The single authoritative source of truth on disk is \`docs/current/${activeQuest}.md\`.

**Action Required Now**:
${specificAction}`;
}

function sendPostCompactionUserMessage(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
	text: string = "",
): void {
	try {
		pi.sendUserMessage(
			`${INTERNAL_MESSAGE_PREFIX}\n${text}`,
		);
	} catch (err: any) {
		logError("Failed to trigger post-compaction resume", err, ctx);
	}
}

function sendPostCompactionResumePrompt(pi: ExtensionAPI, activeQuest: string, isCompaction = true, ctx?: ExtensionContext) {
	if (!activeQuest) return;
	const c = getActiveContext(ctx);
	const targetSessionId = getSessionId(c);
	const targetState = sessionStates.get(targetSessionId) ?? getState(c);

	if (isCompaction && typeof targetState.lastResumeCompactCount === "number" && (targetState.lastResumeCompactCount === targetState.saveCount || targetState.lastResumeCompactCount === targetState.compactCount)) {
		return;
	}
	const now = Date.now();
	if (!isCompaction && targetState.lastResumeTarget === activeQuest && typeof targetState.lastResumePromptAt === "number" && now - targetState.lastResumePromptAt < 1000) {
		return;
	}
	targetState.lastResumePromptAt = now;
	targetState.lastResumeTarget = activeQuest;
	if (isCompaction) {
		targetState.lastResumeCompactCount = targetState.saveCount;
	}
	persist(pi, c);

	const directiveText = buildPostCompactionResumeDirective(activeQuest, targetState);
	sendInternalAgentMessage(pi, directiveText, "followUp");
}

function installAfterCompact(pi: ExtensionAPI) {
	pi.on(
		"session_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			handleCompactionCompleted(pi, ctx);
		}),
	);
	pi.on(
		"session_compact_failed",
		withContext(async (_event: any, _ctx: ExtensionContext) => {
			state.compactionPending = false;
			state.lastWarnedCompactionTokens = null;
			state.preCompactionCheckpointPending = false;
			state.preCompactionSaveRequestPending = false;
			state.subquestLaunchCompactionPending = false;
		}),
	);
}

function installContextListener(pi: ExtensionAPI) {
	pi.on(
		"context",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			if (state.pickerCancelled) return;
			requestPreCompactionCheckpoint(pi, ctx);
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

function installFileWatch(pi: ExtensionAPI) {
	pi.on("tool_result", async (event: any, ctx: any) => {
		if (event.isError || event.error || (event.details && (event.details.error || event.details.success === false))) {
			return;
		}

		if (event.toolName === "ask_questions" || (typeof event.toolName === "string" && event.toolName.toLowerCase().includes("ask_question"))) {
			handleAskQuestionsResult(pi, event, ctx);
			return;
		}

		if (event.toolName !== "write" && event.toolName !== "edit") {
			if (
				event.toolName === "bash" ||
				event.toolName === "user_bash" ||
				event.toolName === "subagent" ||
				(typeof event.toolName === "string" && (event.toolName.startsWith("bg_run") || event.toolName.startsWith("fusion_") || event.toolName === "doc_to_md"))
			) {
				state.dirty = true;
			}
			return;
		}

		const p = event.input?.path as string | undefined;
		if (typeof p !== "string") return;
		const norm = normalizePath(p);

		if (state.active && norm === questPath(state.active)) {
			await verifyAndMarkSaved(pi, ctx, state.active);
		} else if (!state.active && norm.startsWith(`${QUEST_DIR}/`) && norm.endsWith(".md")) {
			const slug = basename(norm).replace(/\.md$/, "");
			state.active = slug;
			if (!Array.isArray(state.stack)) state.stack = [slug];
			else if (!state.stack.includes(slug)) state.stack.push(slug);
			await verifyAndMarkSaved(pi, ctx, slug);
		} else {
			state.dirty = true;
		}
	});
}

async function markSubQuestCompletedInParent(parentSlug: string, childSlug: string, ctx?: ExtensionContext): Promise<boolean> {
	const parentPath = questPath(parentSlug);
	if (!(await fileExists(parentPath))) return false;
	try {
		const content = await readFile(parentPath, "utf8");
		const lines = content.split(/\r?\n/);
		let matched = false;
		const updatedLines = lines.map((line) => {
			if (line.includes(childSlug) && /^\s*-\s*\[\s*\]/.test(line)) {
				matched = true;
				return line.replace(/^(\s*-\s*\[)\s*(\]\s*.*)$/, "$1x$2");
			}
			return line;
		});
		if (matched) {
			await writeFile(parentPath, updatedLines.join("\n"), "utf8");
			return true;
		}
	} catch (err: any) {
		logError(`Failed to mark subquest ${childSlug} completed in ${parentPath}`, err, ctx);
	}
	return false;
}

const normalizePath = (p: string) => p.replace(/^\.\//, "").replace(/\\/g, "/");

interface LoadedQuestState {
	originalRequest: string;
	refinements: string[];
	exists: boolean;
	researchRound: number;
	researchComplete: boolean;
	researchRequired: boolean;
	planVersion: number;
	planConfidence: "low" | "medium" | "high";
	lastResearchAt?: number;
	lastPlanRevisionAt?: number;
	awaitingUserConfirmation?: boolean;
	reassessmentRequired: boolean;
	reassessmentReason: string | null;
	reassessmentEvidence: string | null;
	reassessmentVersion: number;
	resolvedReassessmentVersion: number;
	lastPlanRevisionsText: string | null;
}

async function loadExistingQuestEpistemicState(slug: string): Promise<LoadedQuestState> {
	const path = questPath(slug);
	if (!(await fileExists(path))) {
		return {
			originalRequest: "",
			refinements: [],
			exists: false,
			researchRound: 1,
			researchComplete: false,
			researchRequired: true,
			planVersion: 1,
			planConfidence: "low",
			reassessmentRequired: false,
			reassessmentReason: null,
			reassessmentEvidence: null,
			reassessmentVersion: 0,
			resolvedReassessmentVersion: 0,
			lastPlanRevisionsText: null,
		};
	}
	try {
		const content = await readFile(path, "utf8");
		const sections = parseMarkdownSections(content);
		let originalRequest = "";
		const reqSec = sections.get("original request") || sections.get("original user request");
		if (reqSec) {
			const rawText = reqSec.body.replace(/^>\s*/gm, "").trim();
			if (rawText && !rawText.startsWith("Paste the verbatim user prompt") && !rawText.startsWith("Goal:")) {
				originalRequest = rawText;
			}
		}
		const refinements: string[] = [];
		const refSec = sections.get("quest refinements & user feedback loops") || sections.get("refinements");
		if (refSec) {
			const lines = refSec.body
				.split(/\r?\n/)
				.map((l) => l.replace(/^[-*]\s*/, "").replace(/^\d+\.\s*/, "").trim())
				.filter((l) => l && l !== "-" && !l.startsWith(">"));
			if (lines.length > 0) {
				refinements.push(...lines);
			}
		}

		let planConfidence: "low" | "medium" | "high" = "low";
		const confSec = sections.get("plan confidence") || sections.get("confidence");
		if (confSec && !isPlaceholderOrEmpty(confSec.body)) {
			const lower = confSec.body.toLowerCase();
			if (lower.includes("high")) planConfidence = "high";
			else if (lower.includes("medium")) planConfidence = "medium";
		}

		let planVersion = 1;
		const verSec = sections.get("plan version") || sections.get("version");
		if (verSec && !isPlaceholderOrEmpty(verSec.body)) {
			const parsed = Number.parseInt(verSec.body.replace(/\D/g, ""), 10);
			if (!Number.isNaN(parsed) && parsed >= 1) {
				planVersion = parsed;
			}
		}

		let researchRound = 1;
		const roundSec = sections.get("research round") || sections.get("research cycle");
		if (roundSec && !isPlaceholderOrEmpty(roundSec.body)) {
			const parsed = Number.parseInt(roundSec.body.replace(/\D/g, ""), 10);
			if (!Number.isNaN(parsed) && parsed >= 1) {
				researchRound = parsed;
			}
		}

		let lastResearchAt: number | undefined;
		const researchAtSec = sections.get("last research at") || sections.get("last research timestamp") || sections.get("last research");
		if (researchAtSec && !isPlaceholderOrEmpty(researchAtSec.body)) {
			const raw = researchAtSec.body.trim();
			const parsed = Number.parseInt(raw, 10);
			if (!Number.isNaN(parsed) && parsed > 0) {
				lastResearchAt = parsed;
			} else {
				const dateParsed = Date.parse(raw);
				if (!Number.isNaN(dateParsed) && dateParsed > 0) {
					lastResearchAt = dateParsed;
				}
			}
		}

		let lastPlanRevisionAt: number | undefined;
		const planRevAtSec = sections.get("last plan revision at") || sections.get("last plan revision timestamp") || sections.get("last plan revision");
		if (planRevAtSec && !isPlaceholderOrEmpty(planRevAtSec.body)) {
			const raw = planRevAtSec.body.trim();
			const parsed = Number.parseInt(raw, 10);
			if (!Number.isNaN(parsed) && parsed > 0) {
				lastPlanRevisionAt = parsed;
			} else {
				const dateParsed = Date.parse(raw);
				if (!Number.isNaN(dateParsed) && dateParsed > 0) {
					lastPlanRevisionAt = dateParsed;
				}
			}
		}

		let reassessmentVersion = 0;
		const reassessVerSec = sections.get("reassessment version");
		if (reassessVerSec && !isPlaceholderOrEmpty(reassessVerSec.body)) {
			const parsed = Number.parseInt(reassessVerSec.body.replace(/\D/g, ""), 10);
			if (!Number.isNaN(parsed) && parsed >= 0) {
				reassessmentVersion = parsed;
			}
		}

		let resolvedReassessmentVersion = 0;
		const resolvedVerSec = sections.get("resolved reassessment version");
		if (resolvedVerSec && !isPlaceholderOrEmpty(resolvedVerSec.body)) {
			const parsed = Number.parseInt(resolvedVerSec.body.replace(/\D/g, ""), 10);
			if (!Number.isNaN(parsed) && parsed >= 0) {
				resolvedReassessmentVersion = parsed;
			}
		}

		let reassessmentRequired = false;
		let reassessmentReason: string | null = null;
		let reassessmentEvidence: string | null = null;

		const statusSec = sections.get("reassessment status") || sections.get("reassessment state");
		if (statusSec && !isPlaceholderOrEmpty(statusSec.body)) {
			if (statusSec.body.toUpperCase().includes("REQUIRED")) {
				reassessmentRequired = true;
				const match = statusSec.body.match(/REQUIRED[^-]*-(.*)$/i);
				if (match && match[1]) {
					reassessmentReason = match[1].trim();
				}
			}
		} else if (reassessmentVersion > resolvedReassessmentVersion) {
			reassessmentRequired = true;
		}

		const evidenceSec = sections.get("reassessment evidence");
		if (evidenceSec && !isPlaceholderOrEmpty(evidenceSec.body)) {
			reassessmentEvidence = evidenceSec.body.trim();
		}

		let lastPlanRevisionsText: string | null = null;
		const revSec = sections.get("plan revisions") || sections.get("plan revision history") || sections.get("revisions");
		if (revSec && !isPlaceholderOrEmpty(revSec.body)) {
			lastPlanRevisionsText = revSec.body.trim();
		}

		const validation = validateResearchPrerequisites(content, planConfidence, true);
		const researchComplete = validation.valid && !reassessmentRequired;
		const researchRequired = !researchComplete;

		let awaitingUserConfirmation = false;
		const currentStatusSec = sections.get("current status") || sections.get("status");
		if (currentStatusSec && !isPlaceholderOrEmpty(currentStatusSec.body)) {
			const bodyLower = currentStatusSec.body.toLowerCase();
			if (bodyLower.includes("plan provisional") || bodyLower.includes("research pending") || bodyLower.includes("confirmation pending") || bodyLower.includes("research complete") || bodyLower.includes("provisional")) {
				awaitingUserConfirmation = true;
			} else if (bodyLower.includes("plan confirmed") || bodyLower.includes("in progress") || bodyLower.includes("done")) {
				awaitingUserConfirmation = false;
			}
		}

		return {
			originalRequest,
			refinements,
			exists: true,
			researchRound,
			researchComplete,
			researchRequired,
			planVersion,
			planConfidence,
			lastResearchAt,
			lastPlanRevisionAt,
			awaitingUserConfirmation,
			reassessmentRequired,
			reassessmentReason,
			reassessmentEvidence,
			reassessmentVersion,
			resolvedReassessmentVersion,
			lastPlanRevisionsText,
		};
	} catch {
		return {
			originalRequest: "",
			refinements: [],
			exists: false,
			researchRound: 1,
			researchComplete: false,
			researchRequired: true,
			planVersion: 1,
			planConfidence: "low",
			reassessmentRequired: false,
			reassessmentReason: null,
			reassessmentEvidence: null,
			reassessmentVersion: 0,
			resolvedReassessmentVersion: 0,
			lastPlanRevisionsText: null,
		};
	}
}

async function loadExistingQuestIntent(slug: string): Promise<{ originalRequest: string; refinements: string[] }> {
	const loaded = await loadExistingQuestEpistemicState(slug);
	return { originalRequest: loaded.originalRequest, refinements: loaded.refinements };
}

function extractChildResultSummary(content: string, name: string): string {
	const sections = parseMarkdownSections(content);
	const lines: string[] = [];

	const goalSec = sections.get("goal");
	if (goalSec && goalSec.body) {
		lines.push(`- **Goal**: ${goalSec.body.trim()}`);
	}

	const understandingSec = sections.get("current understanding");
	if (understandingSec && understandingSec.body && !understandingSec.body.startsWith(">")) {
		lines.push(`- **Established Understanding**:\n${understandingSec.body.trim()}`);
	}

	const findingsSec = sections.get("research findings") || sections.get("in-depth analysis & findings");
	if (findingsSec && findingsSec.body) {
		lines.push(`- **Findings & Discoveries**:\n${findingsSec.body.trim()}`);
	}

	const assumptionsSec = sections.get("key assumptions") || sections.get("assumptions");
	if (assumptionsSec && assumptionsSec.body) {
		lines.push(`- **Assumptions Evaluated**:\n${assumptionsSec.body.trim()}`);
	}

	const rejectedSec = sections.get("rejected approaches");
	if (rejectedSec && rejectedSec.body && !rejectedSec.body.startsWith(">")) {
		lines.push(`- **Rejected Approaches**:\n${rejectedSec.body.trim()}`);
	}

	const reassessSec = sections.get("latest reassessment") || sections.get("reassessment conclusion");
	if (reassessSec && reassessSec.body && !reassessSec.body.startsWith(">")) {
		lines.push(`- **Latest Reassessment Conclusion**:\n${reassessSec.body.trim()}`);
	}

	const decisionsSec = sections.get("decisions made") || sections.get("decisions");
	if (decisionsSec && decisionsSec.body) {
		lines.push(`- **Decisions Made**:\n${decisionsSec.body.trim()}`);
	}

	const filesSec = sections.get("files touched") || sections.get("files modified");
	if (filesSec && filesSec.body) {
		lines.push(`- **Files Touched**:\n${filesSec.body.trim()}`);
	}

	return lines.length > 0 ? lines.join("\n\n") : `- Completed sub-quest ${name}.`;
}

function sendChildReturnParentPrompt(
	pi: ExtensionAPI,
	parentName: string,
	childName: string,
	childSummary: string,
	ctx?: ExtensionContext,
) {
	const directiveText = `⚡ **Sub-Quest '${childName}' Completed — Parent Evaluation Directive**:
Child sub-quest **${childName}** has completed and been archived. You have returned to parent quest **${parentName}**.

**Child Sub-Quest Results & Established Findings**:
${childSummary}

**Parent Evaluation & Resumption Protocol**:
Do not blindly resume the parent's previous Exact Next Action without evaluating whether the child's findings changed anything.
1. **Inspect Child Findings**: Review what sub-quest **${childName}** established.
2. **Determine Impact on Parent**: Determine whether parent assumptions or current plan were affected.
3. **If Affected**:
   - Trigger/enter reassessment (investigate the contradiction or unexpected findings).
   - Perform targeted investigation into affected areas.
   - Revise or re-validate the plan under \`## Plan Revisions\` in \`docs/current/${parentName}.md\`.
   - Update \`docs/current/${parentName}.md\` with \`quest_update_state({ reassessmentComplete: true, reassessmentConclusion: "...", ... })\`.
4. **If Not Affected**:
   - Record the child result under \`## Research Findings\` and mark the sub-quest completed in \`docs/current/${parentName}.md\`.
   - Preserve the existing parent plan and confidence.
   - Save the quest file via \`quest_mark_saved\` or \`quest_update_state\` (do NOT pass \`reassessmentComplete: true\` unless reassessment was actually required).
   - Continue with the justified next action.
5. Proceed autonomously with parent quest execution.`;

	sendInternalAgentMessage(pi, directiveText, "followUp");
}

/** Helper to archive a quest file and update journal state (LIFO stack pop) */
async function archiveQuestFile(name: string, pi: ExtensionAPI, ctx?: ExtensionContext): Promise<{ success: boolean; message: string; dest?: string; nextActive?: string | null; childSummary?: string }> {
	const path = questPath(name);
	if (!(await fileExists(path))) {
		return { success: false, message: `No quest file found at ${path}` };
	}

	let parentSlug: string | null = null;
	let childSummary = "";
	try {
		const content = await readFile(path, "utf8");
		parentSlug = extractParentFromQuest(content);
		childSummary = extractChildResultSummary(content, name);
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
		if (nextActive) {
			const parentLoaded = await loadExistingQuestEpistemicState(nextActive);
			state.prompts = parentLoaded.originalRequest ? [parentLoaded.originalRequest] : [];
			state.refinements = parentLoaded.refinements;
			state.researchRound = parentLoaded.researchRound;
			state.researchComplete = parentLoaded.researchComplete;
			state.researchRequired = parentLoaded.researchRequired;
			state.reassessmentRequired = parentLoaded.reassessmentRequired;
			state.reassessmentReason = parentLoaded.reassessmentReason;
			state.reassessmentEvidence = parentLoaded.reassessmentEvidence;
			state.reassessmentVersion = parentLoaded.reassessmentVersion;
			state.resolvedReassessmentVersion = parentLoaded.resolvedReassessmentVersion;
			state.planVersion = parentLoaded.planVersion;
			state.planConfidence = parentLoaded.planConfidence;
			state.lastPlanRevisionsText = parentLoaded.lastPlanRevisionsText;
			state.lastResearchAt = parentLoaded.lastResearchAt ?? Date.now();
			state.lastPlanRevisionAt = parentLoaded.lastPlanRevisionAt ?? Date.now();
			state.awaitingUserConfirmation = false;
			syncImplementationPermission(state);
		} else {
			state.prompts = [];
			state.refinements = [];
			state.awaitingUserConfirmation = false;
			syncImplementationPermission(state);
		}
	} else {
		state.stack = stack;
	}

	if (nextActive) {
		await verifyAndMarkSaved(pi, ctx, nextActive);
		persist(pi, ctx);
	} else {
		persist(pi, ctx);
	}

	const returnMsg = nextActive ? ` Resumed parent/previous quest '${nextActive}' (LIFO stack).` : "";
	return { success: true, message: `Archived ${path} → ${dest}.${returnMsg}`, dest, nextActive, childSummary };
}

/** Tool allowing the model to explicitly archive the active (or named) quest and trigger auto-compaction. */
function installArchiveTool(pi: ExtensionAPI) {
	const archiveHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		const targetName = slugify(params.questName || params.name || state.active || "");
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
			persist(pi, ctx);
		} else if (res.nextActive) {
			sendChildReturnParentPrompt(pi, res.nextActive, targetName, res.childSummary || `- Completed sub-quest ${targetName}.`, ctx);
		}

		if (ctx.hasUI) ctx.ui.notify(res.message, "info");
		return {
			content: [
				{
					type: "text",
					text: `${res.message}${shouldCompact ? " Context compaction queued for turn end." : ""}`,
				},
			],
			details: { archived: targetName, dest: res.dest, compacted: shouldCompact, nextActive: res.nextActive },
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
		const isExisting = await fileExists(path);

		if (!isExisting) {
			await writeFile(path, QUEST_TEMPLATE(name, goal, parentName, ""), "utf8");
		}
		if (parentName) {
			await linkSubQuestInParent(parentName, name, goal, ctx);
			await verifyAndMarkSaved(pi, ctx, parentName);
		}

		if (switchNow) {
			state.pickerCancelled = false;
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
			const subLoaded = await loadExistingQuestEpistemicState(name);
			if (subLoaded.exists && isExisting) {
				state.prompts = subLoaded.originalRequest ? [subLoaded.originalRequest] : [goal];
				state.refinements = subLoaded.refinements;
				state.researchRound = subLoaded.researchRound;
				state.researchComplete = subLoaded.researchComplete;
				state.researchRequired = subLoaded.researchRequired;
				state.planVersion = subLoaded.planVersion;
				state.planConfidence = subLoaded.planConfidence;
				state.lastPlanRevisionsText = subLoaded.lastPlanRevisionsText;
				state.reassessmentRequired = subLoaded.reassessmentRequired;
				state.reassessmentReason = subLoaded.reassessmentReason;
				state.reassessmentEvidence = subLoaded.reassessmentEvidence;
				state.reassessmentVersion = subLoaded.reassessmentVersion;
				state.resolvedReassessmentVersion = subLoaded.resolvedReassessmentVersion;
				state.lastResearchAt = subLoaded.lastResearchAt ?? Date.now();
				state.lastPlanRevisionAt = subLoaded.lastPlanRevisionAt ?? Date.now();
			} else {
				state.prompts = [goal];
				state.refinements = [];
				state.researchRound = 1;
				state.researchComplete = false;
				state.researchRequired = true;
				state.reassessmentRequired = false;
				state.reassessmentReason = null;
				state.reassessmentEvidence = null;
				state.reassessmentVersion = 0;
				state.resolvedReassessmentVersion = 0;
				state.lastPlanRevisionsText = null;
				state.planVersion = 1;
				state.planConfidence = "low";
				state.lastResearchAt = Date.now();
				state.lastPlanRevisionAt = Date.now();
			}
			state.saveGeneration = null;
			state.lastSavedHash = null;
			state.dirty = false;
			await verifyAndMarkSaved(pi, ctx, name);

			// Subquest Launch Compaction: defer compaction to turn end if threshold is exceeded
			const subLaunchThreshold = getSubquestCompactThreshold();
			const tokens = calculateCurrentTokens(ctx);

			if (subLaunchThreshold > 0 && tokens !== null && tokens >= subLaunchThreshold) {
				state.subquestLaunchCompactionPending = true;
			} else {
				const enterMsg = `Now working on sub-quest **${name}**${parentName ? ` (parent: **${parentName}**)` : ""}. Sub-quest file: \`${path}\`.

**Stated Goal**: ${goal}

**Sub-Quest Iterative Research & Execution Protocol**:
Sub-quests do NOT inherit the parent's conclusions as immutable facts; treat them as context and hypotheses to independently verify.

1. Read \`${path}\` to inspect inherited context and goal.
2. Independently investigate the relevant subsystem, execution paths, and dependencies.
3. Identify assumptions inherited or required, and test high-risk assumptions directly.
4. Formulate a provisional plan, challenge it against potential failure modes, and revise if needed.
5. Update \`${path}\` (Current Understanding, Key Assumptions, Plan, Plan Confidence, Exact Next Action) and call \`quest_mark_saved\`.
6. Autonomously execute implementation and verify with tests without waiting for user confirmation.
7. Upon completion, archive via \`quest_archive()\` to return findings to the parent quest.`;

				sendInternalAgentMessage(pi, enterMsg, "followUp");
			}
			persist(pi, ctx);
			updateUIStatus(ctx);
		}

		const msg = isExisting
			? `Sub-quest '${name}' already exists at \`${path}\`.${parentName ? ` Verified link in parent '${parentName}'.` : ""}${switchNow ? " Switched active quest to this sub-quest." : ""}`
			: `Created sub-quest **${name}** at \`${path}\`${parentName ? ` (parent: **${parentName}**)` : ""}.${switchNow ? " Switched active quest to this sub-quest." : " Kept parent quest active; sub-quest added to tracker."}`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");

		return {
			content: [{ type: "text", text: msg }],
			details: { subquest: name, path, parent: parentName, switched: switchNow },
		};
	};

	pi.registerTool({
		name: "quest_subquest",
		label: "Create Sub-Quest",
		description: "Create or plan a sub-quest for a genuinely separable workstream, distinct architectural subsystem, self-contained investigation, or independent verification boundary. Use switchNow: false during initial quest planning to pre-create planned sub-quests and link them into the parent coordination plan without switching away from the active parent quest, or switchNow: true (default) to create and immediately switch focus to the sub-quest. Creates docs/current/<sub-quest>.md, links it into the parent quest, and records parent reference.",
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

	function getEpistemicSectionBody(updates: Map<string, string>, existingSections: Map<string, MarkdownSection>, canonicalKey: string): string | undefined {
	if (updates.has(canonicalKey)) return updates.get(canonicalKey);
	const aliases = [canonicalKey, ...(SECTION_ALIASES[canonicalKey] || [])];
	for (const alias of aliases) {
		if (updates.has(alias)) return updates.get(alias);
		const sec = existingSections.get(alias);
		if (sec && sec.body) return sec.body;
	}
	return undefined;
}

const updateStateHandler = async (_toolCallId: string, params: any, _signal: any, _onUpdate: any, ctx: ExtensionContext) => {
		const rawName = (params.name || params.questName || "").trim();
		const targetName = slugify(rawName || state.active || "");
		if (!targetName) {
			return {
				content: [{ type: "text", text: "Error: No active quest to update and no quest name provided. Please specify a concise semantic quest name (e.g. name: 'persistent-agent-research')." }],
				details: { error: "no_active_quest" },
			};
		}

		const originalReq = state.pendingRootRequest || (state.prompts && state.prompts.length > 0 ? state.prompts[0] : "");

		if (!state.active || state.pendingRootQuest) {
			state.active = targetName;
			state.pendingRootQuest = false;
			state.questIdentityEstablished = true;
			state.pendingRootRequest = null;
			if (!Array.isArray(state.stack) || state.stack.length === 0) state.stack = [targetName];
			else if (!state.stack.includes(targetName)) state.stack.push(targetName);
			if (originalReq && (!state.prompts || state.prompts.length === 0)) {
				state.prompts = [originalReq];
			}
			persist(pi, ctx);
		}

		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(targetName);
		if (!(await fileExists(path))) {
			await writeFile(path, QUEST_TEMPLATE(targetName, params.goal || "", "", originalReq, state.refinements || []), "utf8");
		}

		try {
			const content = await readFile(path, "utf8");
			const existingSections = parseMarkdownSections(content);
			const updates = new Map<string, string>();

			if (params.goal) {
				updates.set("goal", params.goal);
			}

			if (params.status) {
				updates.set("current status", params.status);
			}

			if (params.understanding || params.currentUnderstanding) {
				const val = params.understanding || params.currentUnderstanding;
				const text = Array.isArray(val) ? val.map((u: string) => (u.startsWith("- ") ? u : `- ${u}`)).join("\n") : String(val);
				updates.set("current understanding", text);
			}

			if (params.assumptions || params.keyAssumptions) {
				const val = params.assumptions || params.keyAssumptions;
				const text = Array.isArray(val) ? val.map((a: string) => (a.startsWith("- [") || a.startsWith("- ") ? a : `- [ ] ${a}`)).join("\n") : String(val);
				updates.set("key assumptions", text);
			}

			if (params.openQuestions || params.uncertainties) {
				const val = params.openQuestions || params.uncertainties;
				const text = Array.isArray(val) ? val.map((q: string) => (q.startsWith("- [") || q.startsWith("- ") ? q : `- [ ] ${q}`)).join("\n") : String(val);
				updates.set("open questions & uncertainties", text);
			}

			const findingsList = params.findings || params.researchFindings || params.importantFindings;
			if (Array.isArray(findingsList) && findingsList.length > 0) {
				const findingsText = findingsList.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
				updates.set("research findings", findingsText);
				updates.set("in-depth analysis & findings", findingsText);
			}

			if (params.plan || params.executionPlan) {
				const val = params.plan || params.executionPlan;
				const text = Array.isArray(val) ? val.map((p: string, i: number) => (/^\d+\./.test(p) ? p : `${i + 1}. ${p}`)).join("\n") : String(val);
				updates.set("plan", text);
				updates.set("detailed multi-stage execution plan", text);
			}

			if (params.planConfidence) {
				const confStr = String(params.planConfidence);
				const reasonStr = params.planConfidenceReason ? `\nReason:\n${params.planConfidenceReason}` : "";
				updates.set("plan confidence", `${confStr}${reasonStr}`);
				const lowerConf = confStr.toLowerCase();
				if (lowerConf.includes("high")) state.planConfidence = "high";
				else if (lowerConf.includes("medium")) state.planConfidence = "medium";
				else if (lowerConf.includes("low")) state.planConfidence = "low";
			}

			if (params.planRevisions || params.revisions) {
				const val = params.planRevisions || params.revisions;
				const text = Array.isArray(val) ? val.map((r: string) => (r.startsWith("- ") ? r : `- ${r}`)).join("\n") : String(val);
				updates.set("plan revisions", text);
				if (text.trim() !== (state.lastPlanRevisionsText || "").trim()) {
					state.planVersion = Math.max(typeof params.planVersion === "number" ? params.planVersion : 0, (state.planVersion || 1) + 1);
					state.lastPlanRevisionAt = Date.now();
					state.lastPlanRevisionsText = text.trim();
				}
			} else if (typeof params.planVersion === "number" && params.planVersion > (state.planVersion || 1)) {
				state.planVersion = params.planVersion;
				state.lastPlanRevisionAt = Date.now();
			}

			if (params.rejectedApproaches) {
				const val = params.rejectedApproaches;
				const text = Array.isArray(val) ? val.map((r: string) => (r.startsWith("- ") ? r : `- ${r}`)).join("\n") : String(val);
				updates.set("rejected approaches", text);
			}

			if (params.reassessmentConclusion) {
				updates.set("latest reassessment", params.reassessmentConclusion);
			}

			if (Array.isArray(params.decisions) && params.decisions.length > 0) {
				const decisionsText = params.decisions.map((d: string) => (d.startsWith("- ") ? d : `- ${d}`)).join("\n");
				updates.set("decisions made", decisionsText);
			}

			if (Array.isArray(params.constraints) && params.constraints.length > 0) {
				const constraintsText = params.constraints.map((c: string) => (c.startsWith("- ") ? c : `- ${c}`)).join("\n");
				updates.set("constraints & rules", constraintsText);
			}

			if (Array.isArray(params.filesExamined) && params.filesExamined.length > 0) {
				const examinedText = params.filesExamined.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
				updates.set("files examined", examinedText);
			}

			const filesTouchedList = params.filesTouched || params.filesModified;
			if (Array.isArray(filesTouchedList) && filesTouchedList.length > 0) {
				const filesText = filesTouchedList.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
				updates.set("files touched", filesText);
			}

			if (params.testStatus) {
				updates.set("test / build status", typeof params.testStatus === "string" ? params.testStatus : JSON.stringify(params.testStatus));
			}

			if (Array.isArray(params.remaining) && params.remaining.length > 0) {
				const remainingText = params.remaining.map((r: string) => (r.startsWith("- [") ? r : `- [ ] ${r}`)).join("\n");
				updates.set("remaining work", remainingText);
			}

			const executionSnapshotVal = params.executionSnapshot || params.snapshot;
			if (executionSnapshotVal) {
				updates.set("execution snapshot", executionSnapshotVal);
			}

			const nextStepVal = params.nextStep || params.nextAction || params.exactNextAction;
			if (nextStepVal) {
				updates.set("exact next action", nextStepVal);
				updates.set("next recommended step", nextStepVal);
			}

			const resumeContextVal = params.resumeContext || params.resumePrompt;
			if (resumeContextVal) {
				updates.set("resume prompt", resumeContextVal);
			}

			let reassessmentTransitionNote = "";
			if (params.reassessmentComplete === true) {
				const conclusionVal = params.reassessmentConclusion || updates.get("latest reassessment") || (existingSections.get("latest reassessment")?.body);
				// Test with provisional markdown
				const provisionalMarkdown = spliceMarkdownSections(content, updates);
				const validation = validateResearchPrerequisites(provisionalMarkdown, params.planConfidence || state.planConfidence, params.allowLowConfidence === true, params.planConfidenceReason);

				if (isPlaceholderOrEmpty(conclusionVal)) {
					state.reassessmentRequired = true;
					state.researchRequired = true;
					state.researchComplete = false;
					state.planConfidence = "low";
					reassessmentTransitionNote = " (Note: reassessmentComplete refused -- requires a non-empty reassessmentConclusion stating what fresh investigation established about the contradiction)";
				} else if (!validation.valid) {
					state.reassessmentRequired = true;
					state.researchRequired = true;
					state.researchComplete = false;
					state.planConfidence = "low";
					reassessmentTransitionNote = ` (Note: reassessmentComplete refused -- replacement epistemic state invalid or missing: [${validation.missingSections.join(", ")}]${validation.confidenceIssue ? `; ${validation.confidenceIssue}` : ""})`;
				} else {
					state.resolvedReassessmentVersion = state.reassessmentVersion || 1;
					state.reassessmentRequired = false;
					state.reassessmentReason = null;
					state.reassessmentEvidence = null;
					state.lastReassessmentPromptAt = 0;
					state.lastReassessmentReason = null;
					state.consecutiveFailures = 0;
					state.researchRequired = false;
					state.researchComplete = true;
					state.lastResearchAt = Date.now();
					state.awaitingUserConfirmation = false;
					if (!Array.isArray(state.confirmedQuests)) state.confirmedQuests = [];
					if (!state.confirmedQuests.includes(targetName)) state.confirmedQuests.push(targetName);
					syncImplementationPermission(state);
					reassessmentTransitionNote = " Reassessment marked complete and resolved.";
				}
			}

			let researchTransitionNote = "";
			if (params.researchComplete === true) {
				if (state.reassessmentRequired) {
					researchTransitionNote = " (Note: researchComplete ignored because reassessment is pending; call with reassessmentComplete: true and reassessmentConclusion to resolve)";
				} else {
					const provisionalMarkdown = spliceMarkdownSections(content, updates);
					const validation = validateResearchPrerequisites(provisionalMarkdown, params.planConfidence || state.planConfidence, params.allowLowConfidence === true, params.planConfidenceReason);
					if (validation.valid) {
						state.researchComplete = true;
						state.researchRequired = false;
						state.lastResearchAt = Date.now();
						const isAlreadyConfirmed = Array.isArray(state.confirmedQuests) && state.confirmedQuests.includes(targetName);
						state.awaitingUserConfirmation = isRootQuest(state) && !isAlreadyConfirmed;
						syncImplementationPermission(state);
					} else {
						state.researchComplete = false;
						state.researchRequired = true;
						syncImplementationPermission(state);
						researchTransitionNote = ` (Note: researchComplete refused -- missing or placeholder: [${validation.missingSections.join(", ")}]${validation.confidenceIssue ? `; ${validation.confidenceIssue}` : ""})`;
					}
				}
			} else if (params.researchComplete === false) {
				state.researchComplete = false;
				state.researchRequired = true;
				syncImplementationPermission(state);
			}

			updates.set("plan version", String(state.planVersion || 1));
			updates.set("research round", String(state.researchRound || 1));
			updates.set("reassessment version", String(state.reassessmentVersion || 0));
			updates.set("resolved reassessment version", String(state.resolvedReassessmentVersion || 0));
			if (state.lastResearchAt) {
				updates.set("last research at", String(state.lastResearchAt));
			}
			if (state.lastPlanRevisionAt) {
				updates.set("last plan revision at", String(state.lastPlanRevisionAt));
			}
			if (state.reassessmentRequired) {
				updates.set("reassessment status", `REQUIRED (v${state.reassessmentVersion || 1}) - ${state.reassessmentReason || "contradiction detected"}`);
			} else {
				updates.set("reassessment status", `RESOLVED (v${state.resolvedReassessmentVersion || 0})`);
			}
			if (state.reassessmentEvidence) {
				updates.set("reassessment evidence", state.reassessmentEvidence);
			}

			const updatedMarkdown = spliceMarkdownSections(content, updates);

			await writeFile(path, updatedMarkdown, "utf8");

			const saveRes = await verifyAndMarkSaved(pi, ctx, targetName);

			return {
				content: [{ type: "text", text: `Successfully updated quest state for **${targetName}** at \`${path}\` (gen #${saveRes.count}, hash: ${saveRes.hash}, plan v${state.planVersion || 1}).${researchTransitionNote}${reassessmentTransitionNote}` }],
				details: { quest: targetName, path, status: params.status, hash: saveRes.hash, generation: saveRes.count, planVersion: state.planVersion, researchComplete: state.researchComplete, reassessmentRequired: state.reassessmentRequired },
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
				understanding: {
					type: "string",
					description: "Core architectural facts and verified execution paths.",
				},
				assumptions: {
					type: "array",
					items: { type: "string" },
					description: "Key assumptions supporting the approach.",
				},
				openQuestions: {
					type: "array",
					items: { type: "string" },
					description: "Material uncertainties to investigate.",
				},
				plan: {
					type: "array",
					items: { type: "string" },
					description: "Multi-stage execution plan steps.",
				},
				planConfidence: {
					type: "string",
					enum: ["low", "medium", "high"],
					description: "Confidence level in the current plan.",
				},
				planConfidenceReason: {
					type: "string",
					description: "Justification for the confidence level (verified execution paths, validated assumptions, remaining uncertainties).",
				},
				reassessmentConclusion: {
					type: "string",
					description: "What the fresh investigation established about the triggering contradiction, whether the previous assumption was validated/invalidated/reformulated, and whether the current plan survived or changed.",
				},
				planRevisions: {
					type: "array",
					items: { type: "string" },
					description: "Record of plan revisions with invalidating evidence.",
				},
				rejectedApproaches: {
					type: "array",
					items: { type: "string" },
					description: "Disproved hypotheses or abandoned approaches.",
				},
				researchComplete: {
					type: "boolean",
					description: "Whether initial research cycle is complete.",
				},
				allowLowConfidence: {
					type: "boolean",
					description: "Allow completing research even if plan confidence is low (requires justification).",
				},
				reassessmentComplete: {
					type: "boolean",
					description: "Whether pending reassessment has been completed and resolved.",
				},
				planVersion: {
					type: "number",
					description: "Version number of the current plan.",
				},
				decisions: {
					type: "array",
					items: { type: "string" },
					description: "Key architectural decisions made.",
				},
				constraints: {
					type: "array",
					items: { type: "string" },
					description: "Constraints and rules to adhere to.",
				},
				filesExamined: {
					type: "array",
					items: { type: "string" },
					description: "List of files examined during research.",
				},
				filesTouched: {
					type: "array",
					items: { type: "string" },
					description: "List of files modified or examined.",
				},
				filesModified: {
					type: "array",
					items: { type: "string" },
					description: "List of files modified.",
				},
				testStatus: {
					type: "string",
					description: "Current build and test status.",
				},
				remaining: {
					type: "array",
					items: { type: "string" },
					description: "List of remaining tasks / checklist items.",
				},
				executionSnapshot: {
					type: "string",
					description: "Comprehensive execution snapshot containing objective, completed, in progress, discoveries, decisions, files, test status, remaining work, and exact next action.",
				},
				exactNextAction: {
					type: "string",
					description: "Concrete next action to be performed immediately by a fresh agent.",
				},
				nextStep: {
					type: "string",
					description: "Next recommended action or step.",
				},
				nextAction: {
					type: "string",
					description: "Next recommended action or step.",
				},
				resumeContext: {
					type: "string",
					description: "Concise briefing giving the next agent iteration complete context.",
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

		const originalReq = state.pendingRootRequest || (state.prompts && state.prompts.length > 0 ? state.prompts[0] : "");
		state.pendingRootQuest = false;
		state.questIdentityEstablished = true;
		state.pendingRootRequest = null;

		await mkdir(QUEST_DIR, { recursive: true });
		const path = questPath(name);
		const futurePath = `${FUTURE_DIR}/${name}.md`;
		
		if (!(await fileExists(path))) {
			if (await fileExists(futurePath)) {
				await rename(futurePath, path);
				if (ctx.hasUI) ctx.ui.notify(`Promoted ${futurePath} → ${path}`, "info");
			} else {
				if (!goal && ctx.hasUI && ctx.mode === "tui") {
					goal = ((await ctx.ui.input("Describe the goal for this quest:")) ?? "").trim();
				}
				if (!goal) {
					goal = name.replace(/-/g, " ");
				}
				await writeFile(path, QUEST_TEMPLATE(name, goal, "", originalReq, state.refinements || []), "utf8");
			}
		}
		await cleanDraftIfExists(name);

		const switching = state.active !== name;
		state.pickerCancelled = false;
		state.active = name;
		if (!Array.isArray(state.stack)) state.stack = [];
		if (!state.stack.includes(name)) {
			state.stack.push(name);
		} else {
			const idx = state.stack.lastIndexOf(name);
			state.stack = state.stack.slice(0, idx + 1);
		}
		const intent = await loadExistingQuestIntent(name);
		if (intent.originalRequest) {
			state.prompts = [intent.originalRequest];
			state.refinements = intent.refinements;
		} else if (switching && state.prompts.length === 0) {
			state.prompts = [goal || name];
			state.refinements = [];
		}
		if (switching) {
			state.saveGeneration = null;
			state.lastSavedHash = null;
			state.dirty = false;
			state.consecutiveFailures = 0;
			state.substantiveTurnsSinceCheckpoint = 0;
			state.lastReassessmentPromptAt = 0;
			state.lastReassessmentReason = null;
			state.lastCheckpointPromptAt = 0;

			const loaded = await loadExistingQuestEpistemicState(name);
			if (loaded.exists) {
				state.researchRound = loaded.researchRound;
				state.researchComplete = loaded.researchComplete;
				state.researchRequired = loaded.researchRequired;
				state.planVersion = loaded.planVersion;
				state.planConfidence = loaded.planConfidence;
				state.lastPlanRevisionsText = loaded.lastPlanRevisionsText;
				state.reassessmentRequired = loaded.reassessmentRequired;
				state.reassessmentReason = loaded.reassessmentReason;
				state.reassessmentEvidence = loaded.reassessmentEvidence;
				state.reassessmentVersion = loaded.reassessmentVersion;
				state.resolvedReassessmentVersion = loaded.resolvedReassessmentVersion;
				state.lastResearchAt = loaded.lastResearchAt ?? Date.now();
				state.lastPlanRevisionAt = loaded.lastPlanRevisionAt ?? Date.now();
				const isTargetConfirmed = Array.isArray(state.confirmedQuests) && state.confirmedQuests.includes(name);
				state.awaitingUserConfirmation = isRootQuest(state) ? (!isTargetConfirmed && (loaded.awaitingUserConfirmation ?? false)) : false;
				syncImplementationPermission(state);
			} else {
				state.researchRound = 1;
				state.researchComplete = false;
				state.researchRequired = true;
				state.reassessmentRequired = false;
				state.reassessmentReason = null;
				state.reassessmentEvidence = null;
				state.reassessmentVersion = 0;
				state.resolvedReassessmentVersion = 0;
				state.lastPlanRevisionsText = null;
				state.planVersion = 1;
				state.planConfidence = "low";
				state.lastResearchAt = Date.now();
				state.lastPlanRevisionAt = Date.now();
				state.awaitingUserConfirmation = false;
				syncImplementationPermission(state);
			}
		}
		if (await fileExists(path)) {
			await verifyAndMarkSaved(pi, ctx, name);
		}
		persist(pi, ctx);

		const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
		const startMsg = `Now working on quest **${name}**. Quest file: \`${path}\`.${goalText}

**Iterative Research, Planning & Falsification Protocol (Turn 1)**:
1. Discover how to build, run, and test the project (e.g. read AGENTS.md, Makefile, scripts).
2. Perform targeted codebase investigation: inspect relevant libraries, module boundaries, data flows, and execution paths.
3. Identify key assumptions and material uncertainties. Specifically investigate the highest-risk assumptions and consider plausible alternative architectures.
4. Formulate a provisional execution plan in \`${path}\`. For any discrete phase taking more than one step, use \`quest_subquest({ goal: '...', switchNow: false })\` to queue it without losing your current focus.
5. Actively challenge the plan: what evidence or test could prove it wrong? If unresolved uncertainties remain, perform a targeted research pass.
6. In Turn 1 of this root/main quest, present your research findings, assumptions evaluated, architectural trade-offs, and revised plan clearly to the user, and ASK FOR USER CONFIRMATION before writing or modifying feature code. Once confirmed, proceed autonomously.

**TDD & Quality Workflow**:
1. For each implementation stage, establish an appropriate verification strategy before implementation. Prefer tests-first when practical, but do not create artificial tests merely to satisfy this workflow.
2. Develop feature -> build -> run -> verify targeted tests.
3. Dynamic Reassessment: Re-evaluate and revise the plan if tests fail or unexpected code paths are uncovered.
4. Support end-of-task user feedback loops and polish iterations until final confirmation.
5. Final Quality Gates: zero build errors/warnings, zero debug artifacts, and full test suite passing with zero errors.`;
		sendInternalAgentMessage(pi, startMsg, "followUp");
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
		handler: withContext(questHandler),
	});

	const questSaveHandler = async (_args: string, ctx: ExtensionContext) => {
		if (!state.active) {
			ctx.ui.notify("No active quest -- use /quest <name> first.", "warning");
			return;
		}
		sendSaveRequest(pi, "Quest-journal: /quest-save -- write a full state snapshot to the active quest file now.");
		state.lastPromptAt = Date.now();
	};

	pi.registerCommand("quest-save", {
		description: "Persist the active quest file now.",
		handler: withContext(questSaveHandler),
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
		if (!Array.isArray(state.refinements)) state.refinements = [];
		state.refinements.push(refinement);
		triggerReassessment(state, `User refinement received: "${refinement.slice(0, 100)}..."`, refinement);
		persist(pi, ctx);

		sendSaveRequest(
			pi,
			`Quest-journal: /quest-refine -- User quest refinement received:\n"${refinement}"\n\nUpdate \`docs/current/${state.active}.md\` now: expand ## Goal if needed, add entry under ## Quest Refinements & User Feedback Loops, update ## Remaining work and ## Test / Build Status, and record any new decisions.`
		);
		state.lastPromptAt = Date.now();
		if (ctx.hasUI) ctx.ui.notify(`Refinement queued for active quest '${state.active}'`, "info");
	};

	pi.registerCommand("quest-refine", {
		description: "Refine the active quest mid-workflow or add post-implementation requirements (e.g. /quest-refine Add edge case handling).",
		handler: withContext(questRefineHandler),
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
		handler: withContext(questDelHandler),
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
		handler: withContext(questEconomyHandler),
	});

	pi.registerCommand("quest-warning", {
		description: "Configure pre-compaction warning margin (e.g. /quest-warning 30k).",
		getArgumentCompletions: warningCompletions,
		handler: withContext(questWarningHandler),
	});

	pi.registerCommand("quest-subquest-threshold", {
		description: "Configure the minimum token threshold for auto-compacting when launching a sub-quest (e.g. /quest-subquest-threshold 40k).",
		getArgumentCompletions: subquestThresholdCompletions,
		handler: withContext(questSubquestThresholdHandler),
	});

	const questStatusHandler = async (_args: string, ctx: ExtensionContext) => {
		if (state.pendingRootQuest) {
			const reqPreview = (state.pendingRootRequest || "").slice(0, 100);
			const line = `[PROVISIONAL ROOT INITIALIZATION] - Research required to establish quest identity and plan. Original request: "${reqPreview}..."`;
			if (ctx.hasUI) ctx.ui.notify(line, "info");
			return line;
		}
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
			if (parentOf.has(slug)) continue;

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
		const isExisting = await fileExists(path);

		if (!isExisting) {
			await writeFile(path, QUEST_TEMPLATE(name, goal, parentName, ""), "utf8");
		}
		if (parentName) {
			await linkSubQuestInParent(parentName, name, goal, ctx);
			await verifyAndMarkSaved(pi, ctx, parentName);
		}

		if (!switchNow) {
			const msg = `Planned sub-quest **${name}** at \`${path}\`${parentName ? ` linked in parent **${parentName}**` : ""}. Kept active quest **${state.active}**.`;
			if (ctx.hasUI) ctx.ui.notify(msg, "info");
			return;
		}

		state.pickerCancelled = false;
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
		const subLoaded = await loadExistingQuestEpistemicState(name);
		if (subLoaded.exists && isExisting) {
			state.prompts = subLoaded.originalRequest ? [subLoaded.originalRequest] : [goal];
			state.refinements = subLoaded.refinements;
			state.researchRound = subLoaded.researchRound;
			state.researchComplete = subLoaded.researchComplete;
			state.researchRequired = subLoaded.researchRequired;
			state.planVersion = subLoaded.planVersion;
			state.planConfidence = subLoaded.planConfidence;
			state.lastPlanRevisionsText = subLoaded.lastPlanRevisionsText;
			state.reassessmentRequired = subLoaded.reassessmentRequired;
			state.reassessmentReason = subLoaded.reassessmentReason;
			state.reassessmentEvidence = subLoaded.reassessmentEvidence;
			state.reassessmentVersion = subLoaded.reassessmentVersion;
			state.resolvedReassessmentVersion = subLoaded.resolvedReassessmentVersion;
			state.lastResearchAt = subLoaded.lastResearchAt ?? Date.now();
			state.lastPlanRevisionAt = subLoaded.lastPlanRevisionAt ?? Date.now();
		} else {
			state.prompts = [goal];
			state.refinements = [];
			state.researchRound = 1;
			state.researchComplete = false;
			state.researchRequired = true;
			state.reassessmentRequired = false;
			state.reassessmentReason = null;
			state.reassessmentEvidence = null;
			state.reassessmentVersion = 0;
			state.resolvedReassessmentVersion = 0;
			state.lastPlanRevisionsText = null;
			state.planVersion = 1;
			state.planConfidence = "low";
			state.lastResearchAt = Date.now();
			state.lastPlanRevisionAt = Date.now();
		}
		state.saveGeneration = null;
		state.lastSavedHash = null;
		state.dirty = false;
		await verifyAndMarkSaved(pi, ctx, name);
		persist(pi, ctx);
		updateUIStatus(ctx);

		const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
		const subquestMsg = `Now working on sub-quest **${name}**${parentName ? ` (parent: **${parentName}**)` : ""}. Sub-quest file: \`${path}\`.${goalText}

**Sub-Quest Iterative Research & Execution Protocol**:
Sub-quests do NOT inherit the parent's conclusions as immutable facts; treat them as context and hypotheses to independently verify.

1. Read \`${path}\` to inspect inherited context and goal.
2. Independently investigate the relevant subsystem, execution paths, and dependencies.
3. Identify assumptions inherited or required, and test high-risk assumptions directly.
4. Formulate a provisional plan, challenge it against potential failure modes, and revise if needed.
5. Update \`${path}\` (Current Understanding, Key Assumptions, Plan, Plan Confidence, Exact Next Action) and call \`quest_mark_saved\`.
6. Autonomously execute implementation and verify with tests without waiting for user confirmation.
7. Upon completion, archive via \`quest_archive()\` to return findings to the parent quest.`;

		sendInternalUserMessage(pi, subquestMsg);
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

function QUEST_TEMPLATE(name: string, goal = "", parent = "", originalRequest = "", refinements: string[] = []): string {
	const parentSec = parent
		? `## Parent Quest\n[[${parent}]]\n`
		: `## Parent Quest\n> If this is a sub-quest, reference the parent quest here (e.g. [[parent-quest-name]]).\n`;

	const requestBody = originalRequest
		? `> ${originalRequest}`
		: `> Paste the verbatim user prompt here (or very faithful summary if truncated). This section MUST stay faithful -- it is enforced by the extension.`;

	const refinementsBody = refinements && refinements.length > 0
		? refinements.map((r) => `- ${r}`).join("\n")
		: `- `;

	return [
		`# Quest: ${name}`,
		``,
		`## Goal`,
		goal ? goal : `> What we are trying to accomplish.`,
		``,
		`## Original request`,
		requestBody,
		`>`,
		``,
		parentSec,
		`## Current Status`,
		`- [ ] research pending · plan provisional · plan confirmed · in progress · blocked · done`,
		``,
		`## Current Understanding`,
		`> Core architectural facts, execution paths, and component boundaries established by research.`,
		`- `,
		``,
		`## Key Assumptions`,
		`> Material assumptions supporting the approach (unverified | validated | invalidated).`,
		`- [ ] `,
		``,
		`## Open Questions & Uncertainties`,
		`> Material uncertainties to resolve through targeted investigation or testing.`,
		`- [ ] `,
		``,
		`## Research Findings`,
		`> Factual discoveries, data flow analysis, and tested hypotheses.`,
		`- `,
		``,
		`## Plan Version`,
		`1`,
		``,
		`## Research Round`,
		`1`,
		``,
		`## Last Research At`,
		`${Date.now()}`,
		``,
		`## Last Plan Revision At`,
		`${Date.now()}`,
		``,
		`## Plan`,
		`> Multi-stage execution plan (treat as provisional until falsification pass completes).`,
		`1. `,
		``,
		`## Plan Confidence`,
		`> low · medium · high (justify confidence based on verified assumptions and evidence).`,
		``,
		`## Plan Revisions`,
		`> Record of plan changes: previous plan -> invalidating evidence -> new finding -> revised plan.`,
		`- Initial plan formulated.`,
		``,
		`## Latest Reassessment`,
		`> Records findings from the most recent fresh investigation when reassessment is triggered.`,
		`- `,
		``,
		`## Rejected Approaches`,
		`> Disproved hypotheses, failed attempts, and why they were abandoned.`,
		`- `,
		``,
		`## Execution Snapshot`,
		``,
		`### Objective`,
		goal ? `> ${goal}` : `> What we are trying to accomplish.`,
		``,
		`### Completed`,
		`- `,
		``,
		`### In Progress`,
		`- `,
		``,
		`### Important Discoveries`,
		`- `,
		``,
		`### Decisions`,
		`- `,
		``,
		`### Constraints`,
		`- `,
		``,
		`### Files Examined`,
		`- `,
		``,
		`### Files Modified`,
		`- `,
		``,
		`### Test / Build Status`,
		`- `,
		``,
		`### Known Problems / Uncertainties`,
		`- `,
		``,
		`### Remaining Work`,
		`- [ ] `,
		``,
		`### Exact Next Action`,
		`> `,
		``,
		`### Resume Context`,
		`> `,
		``,
		`## Decisions Made`,
		`- `,
		``,
		`## Constraints & Rules`,
		`- `,
		``,
		`## Files Touched`,
		`- `,
		``,
		`## Test / Build Status`,
		`- `,
		``,
		`## Sub-Quests`,
		`> Planned sub-quests, follow-ups, or tangent quests linked to this quest.`,
		`- [ ] `,
		``,
		`## Quest Refinements & User Feedback Loops`,
		`> Mid-workflow refinements, post-implementation iterations, and user adjustments.`,
		refinementsBody,
		``,
		`## Remaining Work`,
		`- [ ] `,
		``,
		`## Exact Next Action`,
		`> Most justified immediate action (investigate, verify assumption, revise plan, implement, run tests).`,
		``,
		`## Resume Context`,
		`> Concise epistemic briefing for continuing this quest without repeating solved research.`,
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
			{ key: "original request", title: "Original Request", maxChars: 4000 },
			{ key: "current status", title: "Current Status", maxChars: 2000 },
			{ key: "current understanding", title: "Current Understanding", maxChars: 4000 },
			{ key: "key assumptions", title: "Key Assumptions", maxChars: 3000 },
			{ key: "open questions & uncertainties", title: "Open Questions & Uncertainties", maxChars: 3000 },
			{ key: "plan", title: "Plan", maxChars: 5000 },
			{ key: "plan confidence", title: "Plan Confidence", maxChars: 1000 },
			{ key: "plan revisions", title: "Plan Revisions", maxChars: 3000 },
			{ key: "rejected approaches", title: "Rejected Approaches", maxChars: 3000 },
			{ key: "execution snapshot", title: "Execution Snapshot", maxChars: 8000 },
			{ key: "remaining work", title: "Remaining Work", maxChars: 4000 },
			{ key: "exact next action", title: "Exact Next Action", maxChars: 3000 },
			{ key: "test / build status", title: "Test / Build Status", maxChars: 2000 },
			{ key: "resume prompt", title: "Resume Context", maxChars: 5000 },
		];

		const fallbackSections = [
			{ key: "goal", title: "Goal", maxChars: 800 },
			{ key: "parent quest", title: "Parent Quest", maxChars: 400 },
			{ key: "research findings", title: "Important Findings", maxChars: 3000 },
			{ key: "decisions made", title: "Decisions", maxChars: 3000 },
			{ key: "constraints & rules", title: "Constraints & Rules", maxChars: 1000 },
			{ key: "files examined", title: "Files Examined", maxChars: 1000 },
			{ key: "files touched", title: "Files Modified", maxChars: 2000 },
			{ key: "sub-quests", title: "Sub-Quests", maxChars: 1000 },
			{ key: "quest refinements & user feedback loops", title: "Quest Refinements & User Feedback Loops", maxChars: 2000 },
		];

		const seenTitles = new Set<string>();
		const usedRawSections = new Set<MarkdownSection>();
		const extracted: string[] = [];

		const tryExtract = (target: { key: string; title: string; maxChars: number }) => {
			if (seenTitles.has(target.title)) return;
			const aliases = [target.key, ...(SECTION_ALIASES[target.key] || [])];
			let sec: MarkdownSection | undefined;
			for (const alias of aliases) {
				const found = sections.get(alias);
				if (found && !usedRawSections.has(found)) {
					sec = found;
					break;
				}
			}
			if (sec && sec.body && sec.body.trim() && sec.body.trim() !== "-" && !sec.body.trim().startsWith("> Paste the verbatim user prompt here") && !sec.body.trim().startsWith("> What we are trying to accomplish.")) {
				let body = sec.body.trim();
				if (target.maxChars && body.length > target.maxChars) {
					body = body.slice(0, target.maxChars).trim() + "… [see quest file for full section]";
				}
				seenTitles.add(target.title);
				usedRawSections.add(sec);
				extracted.push(`### ${target.title}\n${body}`);
			}
		};

		for (const target of targetSections) {
			tryExtract(target);
		}

		// If execution snapshot was not present, fall back to legacy individual sections
		if (!seenTitles.has("Execution Snapshot")) {
			for (const fallback of fallbackSections) {
				tryExtract(fallback);
			}
		}

		if (extracted.length === 0) return "";
		return `\n\n# Active Quest Resume Context (from \`${path}\`)\n${extracted.join("\n\n")}`;
	} catch (err: any) {
		logError(`Failed to load resume context from ${path}`, err);
		return "";
	}
}

function getWorkflowInstructions(resumeContext: string): string {
	return `\n\n# MANDATORY QUEST WORKFLOW RULES & QUALITY GATES (STRICTLY ENFORCED)
Mandatory Quest Workflow Rules:
CRITICAL INSTRUCTION: You MUST strictly adhere to these workflow rules and project quality gates on every turn. Do not take shortcuts, do not invent unverified assumptions, and do not bypass verification steps.

When working on quests:
1. **Iterative Research, Provisional Planning & Falsification Protocol (Turn 1 Protocol)**:
   - Before writing or editing feature code, follow the iterative research protocol:
     \`research -> provisional understanding -> provisional plan -> challenge plan -> targeted research -> revised plan -> implementation\`.
   - **Research & Understand**: Establish relevant architecture, library contracts, module boundaries, and execution paths through targeted reading and call tracing.
   - **Discover Actual Problem Structure**: Discover how the problem naturally divides during research rather than inventing an artificial flat list of bullets. If the task breaks down into distinct subsystems, architectural concerns, or separable investigations, identify those workstreams early and map them as sub-quests linked into the parent coordination plan (\`[[subquest-name]]\`).
   - **Identify Assumptions & Uncertainties**: Explicitly list key assumptions your approach relies on and identify unresolved questions. Identify highest-risk assumptions and investigate them specifically.
   - **Formulate Provisional Plan**: Produce a provisional multi-stage plan and record it under \`## Plan\` in \`docs/current/<quest>.md\`. Explicitly treat initial plans as provisional.
   - **Actively Challenge & Falsify the Plan**: Ask what evidence could prove this plan wrong. Inspect relevant tests/code to test critical assumptions. If the plan depends on unresolved uncertainties, perform another targeted research pass.
   - **Stopping Condition**: Research continues until relevant architecture is understood, important execution paths are verified, material assumptions are tested, plausible alternatives are considered, and major uncertainties are resolved or explicitly accepted (do NOT stop based on arbitrary file counts).
   - **Turn 1 Confirmation & Asking User Questions Protocol**:
     - In Turn 1 of a root/main quest (and whenever asking the user for input or confirmation):
       1. First perform research and update the quest file via \`quest_update_state\` during the tool execution phase.
       2. If research revealed natural workstreams, explain the decomposition clearly (e.g. "The problem breaks naturally into [N] independent areas: A, B... I'm separating those into sub-quests because each requires different investigation and can be verified independently.") and create sub-quests with \`quest_subquest({ switchNow: false })\`.
       3. Then present your research findings, key assumptions evaluated, and revised parent plan clearly to the user.
       4. To ask for user confirmation or input: either invoke the \`ask_questions\` tool (with structured options), OR emit your final question in plain text with **ZERO accompanying tool calls**.
       5. **CRITICAL**: NEVER emit tool calls (such as \`quest_update_state\`, \`quest_mark_saved\`, \`edit\`, \`write\`, \`bash\`) in the same turn that you ask a question to the user. Emitting tool calls causes the agent harness to execute the tool and immediately run another turn, blowing past the question without waiting for the user's answer. Once confirmed by the user (and inside child sub-quests), execution is autonomous across turns and tools without requiring manual user slash commands.
2. **Build & Run Discovery**: Discover how to build and run the project before editing code (\`make\`, \`make watch\`, test runners).
3. **Verification Strategy**: For each implementation stage, establish an appropriate verification strategy before implementation. Prefer tests-first when practical, but do not create artificial tests merely to satisfy this workflow.
4. **Iterative Build, Run & Test**: Feature implementation -> build -> run -> verify targeted tests.
5. **Dynamic Reassessment on Contradictory Evidence**:
   - The quest file is external working memory, not an infallible dogma.
   - Whenever execution encounters contradictory evidence (failed tests/commands, unexpected execution paths, undiscovered architecture, unexpected complexity, or user refinements), trigger a targeted reassessment before continuing.
   - Investigate the contradiction, challenge whether the current plan remains valid, record revisions in \`## Plan Revisions\` and \`## Rejected Approaches\`, update \`docs/current/<quest>.md\`, and proceed with the revised Exact Next Action.
6. **Post-Implementation & User Feedback Loops**:
   - Expect and support user polish iterations at the end of a quest or sub-quest.
   - When the user provides feedback, refinements, or tweaks mid-quest or post-implementation, log them under \`## Quest Refinements & User Feedback Loops\`, update acceptance checklists, execute changes, and verify with tests until the user confirms satisfaction.
7. **Quest Completion & Wrap-Up Flow**:
   - **Root / Top-Level Quest Completion**: When all stages, features, and acceptance criteria are completed, restart the test daemon/server and execute the FULL test suite (\`make test\`) to verify zero errors or regressions. Only after the full test suite passes with zero failures, prompt the user via \`ask_questions\` with structured options (Refine anything, Archive quest and auto-compact, Archive quest without auto-compact, Change to manual mode).
   - **Sub-Quest Completion (Autonomous Continuation & Parent Reassessment)**: When finishing a child sub-quest, autonomously archive the sub-quest via \`quest_archive({ compact: boolean })\`. The parent quest receives the child's established findings, evaluates whether parent assumptions changed, updates its plan, and seamlessly resumes execution.
8. **Final Verification & Quality Gates**:
   - Zero compiler errors or warnings.
   - Zero debug artifacts (no leftover console.logs, prints, or scratch code).
   - Full test suite (\`make test\`) must pass with zero errors.

# Autonomous Quest Management (Zero Manual User Commands Needed)
You manage quests autonomously on disk in \`docs/current/<quest>.md\`. The user should NEVER need to type manual slash commands.

1. **Continuous Durable Epistemic Memory**:
   - Treat \`docs/current/<quest>.md\` as your durable working memory and epistemic record.
   - Proactively update it during normal execution whenever expensive-to-reconstruct discoveries, decisions, tested assumptions, plan revisions, or progress occur.
   - Persist: Current Understanding, Key Assumptions, Open Questions, Research Findings, Plan, Plan Confidence, Plan Revisions, Rejected Approaches, Files Touched, Test/Build Status, and Exact Next Action.
   - Criterion: *Would losing the current context force a fresh agent to repeat significant investigation, reconsider a rejected approach, or guess what to do next?* If yes, persist it.
   - Use \`quest_update_state\` or \`edit\` + \`quest_mark_saved\`.

2. **Auto-Initialize New Quest on Substantive Requests (Research-Grounded Formation)**:
   - When a substantive request arrives, the system enters provisional root initialization.
   - Do NOT immediately create a generic quest file with raw prompt slugification.
   - **Initial Investigation & Orientation**: First investigate relevant architecture, execution paths, constraints, and conventions using read/search/bash tools.
   - **Semantic Quest Identity**: Based on what you learned during research, establish a concise, intelligible semantic name for the body of work (e.g. 'persistent-agent-research', 'oauth-login-flow', 'editor-rendering-crash' — NOT words mechanically copied from the user's prompt).
   - **Initialize Durable Quest**: Call \`quest_update_state({ name: "<semantic-name>", goal: "...", understanding: "...", assumptions: [...], openQuestions: [...], findings: [...], plan: [...], planConfidence: "medium"|"high", exactNextAction: "...", researchComplete: true })\`. This creates \`docs/current/<semantic-slug>.md\` populated with your actual research findings, preserves the verbatim user prompt in \`## Original request\`, and validates the epistemic state.
   - **First-Turn Response**: Summarize your research findings, the newly established architectural facts, why the chosen quest identity fits, and the proposed plan to the user.
   - **Turn 1 Confirmation**: Ask for user confirmation (using \`ask_questions\` or plain text with ZERO accompanying tool calls) before editing feature code. Once confirmed, execute autonomously.

3. **Auto-Refine Active Quest on User Feedback**:
   - When the user provides feedback or new requirements while a quest is active, it is automatically captured as a refinement.
   - Reassess whether the refinement changes the current plan, record changes in \`## Quest Refinements & User Feedback Loops\` and \`## Plan Revisions\`, and update the quest file.

4. **Auto-Create Sub-Quests & Meaningful Decomposition (LIFO Stack)**:
   - **Mental Model**: Decompose according to the discovered structure of the problem, not according to the number of bullets in the plan:
     \`research -> discover actual problem structure -> identify independently investigable/executable workstreams -> create sub-quests for useful separable work -> establish parent plan around them -> execute\`.
   - **When to Create Sub-Quests**: During initial research, explicitly look for work that has one or more of these properties:
     * a distinct subsystem, architectural concern, or execution path;
     * a self-contained investigation that could produce durable findings for the parent;
     * a task with its own assumptions and verification strategy;
     * a component that may require several implementation/verification steps;
     * a parallel or weakly coupled workstream;
     * a risky unknown that deserves independent investigation;
     * a side problem that would otherwise pollute the parent quest's context;
     * work that can be completed and then summarized back to the parent.
   - **When NOT to Create Sub-Quests (Avoid Over-Decomposition)**:
     Do NOT create sub-quests for:
     * a trivial one-command task;
     * a tiny edit;
     * an obvious sequential step that does not benefit from independent context;
     * every item in a checklist;
     * every file touched;
     * every test command.
   - **The Golden Criterion**:
     *Would this unit benefit from having its own durable research state, assumptions, findings, execution history, and context boundary?*
     If yes, create a sub-quest (\`quest_subquest({ name: "...", goal: "...", switchNow: false })\`). If no, keep it within the parent quest.
   - **Meaningful Decomposition**: Choose concise, semantic names (e.g. \`auth-callback-flow\`, \`session-persistence\`, \`frontend-auth-state\`).
   - **Allow Sub-Quests to Emerge from Research**: Begin with PROVISIONAL ROOT RESEARCH, discover during investigation how the work naturally divides into separate concerns, create the appropriate sub-quests, and structure the parent coordination plan around them.
   - **Sub-Quest Creation During Initial Planning**: If research reveals multiple genuinely separable workstreams, create sub-quests for them even when there are only two or three. If the work is tightly coupled, keep it within the parent quest even if there are several plan steps. (A plan with >3 distinct phases or complex components serves as a strong heuristic signal to evaluate for decomposition).
   - **Parent Plan Expressed in Terms of Sub-Quests**: The root plan should reference the actual sub-quests (\`[[sub-quest-name]]\`) rather than duplicating all child reasoning:
     \`\`\`text
     ## Plan
     1. Investigate and resolve [[sub-quest-1]]
     2. Investigate and resolve [[sub-quest-2]]
     3. Integrate the resulting changes and verify end-to-end
     \`\`\`
     The parent retains only coordination-level reasoning; detailed reasoning belongs in the child quest. This protects context economy in the parent.
   - **LIFO Execution Model & Context Inheritance**: Sub-quests operate on a LIFO stack. The parent remains the durable owner of the overall objective. The child inherits context as hypotheses to independently verify:
     \`read inherited context -> independently investigate -> identify assumptions -> test high-risk assumptions -> form provisional plan -> challenge plan -> targeted research if needed -> persist plan -> implement -> verify -> archive -> return findings to parent\`.
   - **Epistemic Hard Gates & Autonomous Child Execution**: Sub-quest creation does NOT bypass epistemic enforcement: a child cannot modify project code until its required research state is established in its own quest file. Once research requirements are satisfied, the child proceeds autonomously without a redundant human confirmation.
   - **Sub-Quest Completion**: When finishing a child sub-quest, call \`quest_archive()\` to pop the stack and autonomously return to the parent quest with a child-result summary!

5. **Auto-Archive Upon Completion (LIFO Pop)**:
   - **For Sub-Quests**: Autonomously archive via \`quest_archive({ compact: boolean })\` and return to parent.
   - **For Root Quests**: When finished, prompt user via \`ask_questions\` to refine or archive.

6. **Auto-Compaction & Autonomous Resumption**:
   - Context automatically compacts dynamically when approaching threshold.
   - When context approaches the compaction threshold, an explicit pre-compaction final save directive will instruct you to perform a final exhaustive state save before compaction.
   - **Durable Epistemic Recovery Following ANY Compaction**:
     - Immediately read \`docs/current/<active-quest>.md\` (the single source of truth on disk).
     - Recover Current Understanding, Key Assumptions, Plan, Plan Confidence, and Exact Next Action.
     - Validate whether the current plan is still supported by the recovered evidence. Do not repeat research merely to reconstruct lost context; use the quest file to recover established knowledge. However, if an important assumption remains unverified, tests disagree, or new evidence contradicts the model, re-investigate that specific aspect before continuing.
     - Proceed directly with executing the justified Exact Next Action without waiting for user commands or modal questions.
7. **Faithful User Request**: The \`## Original request\` section MUST remain verbatim.${resumeContext}`;
}

function installWorkflowSystemPrompt(pi: ExtensionAPI) {
	pi.on("before_agent_start", async (event: any, ctx: ExtensionContext) => {
		try {
			const raw = (event as { prompt?: unknown })?.prompt;
			if (typeof raw === "string" && shouldCapturePrompt(raw)) {
				const trimmed = raw.trim().slice(0, PROMPT_MAX_CHARS);

				if (state.active) {
					if (!Array.isArray(state.refinements)) state.refinements = [];
					if (!Array.isArray(state.prompts)) state.prompts = [];

					const isOriginal = state.prompts.length > 0 && state.prompts[0] === trimmed;
					const isLatestRefinement = state.refinements.length > 0 && state.refinements[state.refinements.length - 1] === trimmed;

					if (!isOriginal && !isLatestRefinement) {
						const classification = classifyUserMessage(trimmed);

						if (classification === UserMessageClassification.CONFIRMATION) {
							acceptRootConfirmation(pi, ctx);
						} else if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT) {
							state.refinements.push(trimmed);
							state.prompts.push(trimmed);
							if (state.prompts.length > PROMPT_MAX_COUNT) {
								state.prompts = [state.prompts[0], ...state.prompts.slice(-(PROMPT_MAX_COUNT - 1))];
							}
							if (state.refinements.length > PROMPT_MAX_COUNT) {
								state.refinements = state.refinements.slice(-PROMPT_MAX_COUNT);
							}
							triggerReassessment(state, `User refinement received: "${trimmed.slice(0, 100)}..."`, trimmed);
							persist(pi, ctx);
							updateUIStatus(ctx);
						}
					}
				} else if (state.pendingRootQuest) {
					const classification = classifyUserMessage(trimmed);
					if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT) {
						if (!Array.isArray(state.refinements)) state.refinements = [];
						state.refinements.push(trimmed);
						if (!state.prompts.includes(trimmed)) {
							state.prompts.push(trimmed);
						}
						persist(pi, ctx);
						updateUIStatus(ctx);
					}
				} else if (shouldStartPersistentQuest(trimmed)) {
					await ensureRootQuestForPrompt(pi, ctx, trimmed);
				}
			}

			const awarenessBlock = buildSessionAwarenessBlock(ctx);
			const resumeContext = await loadActiveQuestResumeContext();
			const workflowInstructions = getWorkflowInstructions(resumeContext);

			if (event && typeof event.systemPrompt === "string") {
				return { systemPrompt: `${event.systemPrompt}\n\n${awarenessBlock}${workflowInstructions}` };
			}
		} catch (err: any) {
			logError("Failed in before_agent_start hook", err, ctx);
			return;
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
			if (set.has("quest_journal_mark_saved") || set.has("quest_mark_saved") || set.has("quest_update_state") || state.active || state.pendingRootQuest) {
				return [
					"Never propose anything without doing your homework first: thoroughly investigate codebase architecture, read files, discover build/run commands, and evaluate constraints before proposing plans or code changes.",
					"Research-Grounded Quest Formation: Investigate first to understand the actual problem, establish a short intelligible semantic quest identity, and initialize the durable quest with research findings via quest_update_state.",
					"Turn 1 Confirmation: In turn 1 of any root quest, present research findings, key assumptions tested, architectural trade-offs, and revised plan clearly to the user, and ask for confirmation BEFORE writing code.",
					"Continuous Durable Epistemic Memory: `docs/current/<quest>.md` is your durable working memory and single source of truth on disk. Proactively record understanding, assumptions, plan confidence, plan revisions, and exact next action whenever discoveries occur.",
					"Dynamic Epistemic Re-Investigation: Use the quest file to recover established knowledge without repeating routine research (no unnecessary re-research). Re-investigate whenever new evidence contradicts an assumption, tests fail, or the plan fails to explain observed behavior.",
					"Autonomous Continuation: Following compaction or sub-quest return, read `docs/current/<active-quest>.md`, validate the plan against recovered state, and proceed immediately without user interruption.",
					"Meaningful Sub-Quest Decomposition: Decompose according to the discovered structure of the problem, not arbitrary bullet counts. During research, identify genuinely separable workstreams (distinct subsystems, independent investigations, separate verification boundaries) and create sub-quests (`quest_subquest({ switchNow: false })`) linked into the parent plan (`[[subquest-name]]`). Avoid artificial fragmentation for trivial or tightly coupled steps. Sub-quests independently verify inherited context.",
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
		updateUIStatus(ctx);
	});
	pi.on("session_tree", async (_event: any, ctx: any) => reconstruct(ctx));

	installWorkflowSystemPrompt(pi);
	installToolCallGate(pi);
	installContextListener(pi);
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
