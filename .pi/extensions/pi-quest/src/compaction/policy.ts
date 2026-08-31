import { readFileSync } from "node:fs";
import { join } from "node:path";
import {
	DEFAULT_CEILING_TOKENS,
	DEFAULT_PERCENT,
	DEFAULT_PRE_COMPACT_WARNING_TOKENS,
	DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS,
	DEFAULT_WARNING_PERCENT,
} from "../constants.ts";
import { calculateCurrentTokens } from "../context.ts";
import { logError } from "../messaging.ts";
import { getActiveContext, getState } from "../state.ts";
import { CompactionPressure, ExtensionContext, StoredState } from "../types.ts";
import { parsePercentage, parseTokenAmount } from "../utils.ts";

export function readSettingsEconomyThreshold(ctx?: ExtensionContext): { tokens?: number | null; percent?: number | null } | null {
	const c = getActiveContext(ctx);
	const baseDir = c?.cwd || process.cwd();
	const projectSettings = join(baseDir, ".pi/settings.json");
	for (const p of [projectSettings, "~/.pi/agent/settings.json"]) {
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

function resolveExplicitEconomyThreshold(s: StoredState, contextWindow: number): number | null {
	if (typeof s.economyPercent === "number" && s.economyPercent > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * s.economyPercent) / 100) : DEFAULT_CEILING_TOKENS;
	}
	if (typeof s.economyTokens === "number") {
		return s.economyTokens;
	}
	return null;
}

function resolveEnvEconomyThreshold(contextWindow: number): number | null {
	const envVal = process.env.PI_QUEST_AUTO_COMPACT_TOKENS ?? process.env.QUEST_AUTO_COMPACT_TOKENS;
	if (!envVal) return null;
	const envPct = parsePercentage(envVal);
	if (envPct !== null && envPct > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * envPct) / 100) : DEFAULT_CEILING_TOKENS;
	}
	return parseTokenAmount(envVal);
}

function resolveSettingsEconomyThreshold(contextWindow: number, ctx?: ExtensionContext): number | null {
	const settingsConfig = readSettingsEconomyThreshold(ctx);
	if (!settingsConfig) return null;
	if (typeof settingsConfig.percent === "number" && settingsConfig.percent > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * settingsConfig.percent) / 100) : DEFAULT_CEILING_TOKENS;
	}
	if (typeof settingsConfig.tokens === "number") {
		return settingsConfig.tokens;
	}
	return null;
}

export function getEconomyThreshold(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
	const contextWindow = usage?.contextWindow ?? 0;

	const explicit = resolveExplicitEconomyThreshold(s, contextWindow);
	if (explicit !== null) return explicit;

	const env = resolveEnvEconomyThreshold(contextWindow);
	if (env !== null) return env;

	const settings = resolveSettingsEconomyThreshold(contextWindow, c);
	if (settings !== null) return settings;

	if (contextWindow > 0) {
		const pctValue = Math.round(contextWindow * (DEFAULT_PERCENT / 100));
		return Math.min(pctValue, DEFAULT_CEILING_TOKENS);
	}
	return DEFAULT_CEILING_TOKENS;
}

export function readSettingsSubquestThreshold(ctx?: ExtensionContext): number | null {
	const c = getActiveContext(ctx);
	const baseDir = c?.cwd || process.cwd();
	const projectSettings = join(baseDir, ".pi/settings.json");
	for (const p of [projectSettings, "~/.pi/agent/settings.json"]) {
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

export function getSubquestCompactThreshold(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	if (typeof s.subquestCompactTokens === "number") {
		return s.subquestCompactTokens;
	}
	const envVal = process.env.PI_QUEST_SUBQUEST_COMPACT_TOKENS ?? process.env.QUEST_SUBQUEST_COMPACT_TOKENS;
	const parsedEnv = parseTokenAmount(envVal, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
	if (parsedEnv !== null) return parsedEnv;

	const parsedSettings = readSettingsSubquestThreshold(c);
	if (parsedSettings !== null) return parsedSettings;

	return DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS;
}

export function readSettingsWarningThreshold(ctx?: ExtensionContext): { tokens?: number | null; percent?: number | null; marginTokens?: number | null } | null {
	const c = getActiveContext(ctx);
	const baseDir = c?.cwd || process.cwd();
	const projectSettings = join(baseDir, ".pi/settings.json");
	for (const p of [projectSettings, "~/.pi/agent/settings.json"]) {
		try {
			const resolved = p.startsWith("~") ? p.replace(/^~/, process.env.HOME || "") : p;
			const raw = readFileSync(resolved, "utf8");
			const json = JSON.parse(raw);
			const pctVal = json?.questJournal?.warningPercent ?? json?.compaction?.warningPercent;
			if (pctVal !== undefined && pctVal !== null) {
				const pct = parsePercentage(pctVal);
				if (pct !== null) return { percent: pct };
			}
			const val =
				json?.questJournal?.preCompactWarningTokens ??
				json?.questJournal?.warningTokens ??
				json?.compaction?.warningMarginTokens ??
				json?.compaction?.warningTokens;
			if (val !== undefined && val !== null) {
				const pct = parsePercentage(val);
				if (pct !== null) return { percent: pct };
				const tokens = parseTokenAmount(val);
				if (tokens !== null) return { marginTokens: tokens };
			}
		} catch (err: any) {
			logError(`Failed reading warning threshold from ${p}`, err);
		}
	}
	return null;
}

export function readSettingsWarningMargin(ctx?: ExtensionContext): number | null {
	const config = readSettingsWarningThreshold(ctx);
	if (!config) return null;
	if (typeof config.marginTokens === "number") return config.marginTokens;
	if (typeof config.tokens === "number") return config.tokens;
	return null;
}

function resolveExplicitWarningThreshold(s: StoredState, contextWindow: number, threshold: number): number | null {
	if (typeof s.warningPercent === "number" && s.warningPercent > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * s.warningPercent) / 100) : Math.max(0, threshold - DEFAULT_PRE_COMPACT_WARNING_TOKENS);
	}
	if (typeof s.warningTokens === "number" && s.warningTokens > 0) {
		return s.warningTokens;
	}
	if (typeof s.warningMarginTokens === "number" && s.warningMarginTokens > 0) {
		return Math.max(0, threshold - s.warningMarginTokens);
	}
	return null;
}

function resolveEnvWarningThreshold(contextWindow: number, threshold: number): number | null {
	const envPctVal = process.env.PI_QUEST_WARNING_PERCENT ?? process.env.QUEST_WARNING_PERCENT;
	if (envPctVal) {
		const envPct = parsePercentage(envPctVal);
		if (envPct !== null && envPct > 0) {
			return contextWindow > 0 ? Math.round((contextWindow * envPct) / 100) : Math.max(0, threshold - DEFAULT_PRE_COMPACT_WARNING_TOKENS);
		}
	}

	const envVal =
		process.env.PI_QUEST_PRE_COMPACT_WARNING_TOKENS ??
		process.env.QUEST_PRE_COMPACT_WARNING_TOKENS ??
		process.env.PI_QUEST_WARNING_TOKENS ??
		process.env.QUEST_WARNING_TOKENS;
	if (!envVal) return null;

	const envPct = parsePercentage(envVal);
	if (envPct !== null && envPct > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * envPct) / 100) : Math.max(0, threshold - DEFAULT_PRE_COMPACT_WARNING_TOKENS);
	}

	const parsedTokens = parseTokenAmount(envVal);
	if (parsedTokens !== null && parsedTokens > 0) {
		return Math.max(0, threshold - parsedTokens);
	}
	return null;
}

function resolveSettingsWarningThreshold(contextWindow: number, threshold: number, ctx?: ExtensionContext): number | null {
	const settingsConfig = readSettingsWarningThreshold(ctx);
	if (!settingsConfig) return null;
	if (typeof settingsConfig.percent === "number" && settingsConfig.percent > 0) {
		return contextWindow > 0 ? Math.round((contextWindow * settingsConfig.percent) / 100) : Math.max(0, threshold - DEFAULT_PRE_COMPACT_WARNING_TOKENS);
	}
	if (typeof settingsConfig.tokens === "number" && settingsConfig.tokens > 0) {
		return settingsConfig.tokens;
	}
	if (typeof settingsConfig.marginTokens === "number" && settingsConfig.marginTokens > 0) {
		return Math.max(0, threshold - settingsConfig.marginTokens);
	}
	return null;
}

export function getWarningThreshold(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	const usage = typeof c?.getContextUsage === "function" ? c.getContextUsage() : undefined;
	const contextWindow = usage?.contextWindow ?? 0;
	const threshold = getEconomyThreshold(c, s);

	const explicit = resolveExplicitWarningThreshold(s, contextWindow, threshold);
	if (explicit !== null) return explicit;

	const env = resolveEnvWarningThreshold(contextWindow, threshold);
	if (env !== null) return env;

	const settings = resolveSettingsWarningThreshold(contextWindow, threshold, c);
	if (settings !== null) return settings;

	if (contextWindow > 0) {
		const pctValue = Math.round(contextWindow * (DEFAULT_WARNING_PERCENT / 100));
		return Math.min(pctValue, Math.max(0, threshold - 1));
	}
	return Math.max(0, threshold - DEFAULT_PRE_COMPACT_WARNING_TOKENS);
}

export function getWarningMargin(ctx?: ExtensionContext, targetState?: StoredState): number {
	const c = getActiveContext(ctx);
	const s = targetState || getState(c);
	const threshold = getEconomyThreshold(c, s);
	const warningThreshold = getWarningThreshold(c, s);
	return Math.max(0, threshold - warningThreshold);
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
	const warningThreshold = getWarningThreshold(c, s);
	const warningMargin = Math.max(0, threshold - warningThreshold);

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
