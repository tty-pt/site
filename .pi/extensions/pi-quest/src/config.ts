import { ENV_SEMANTIC_SUMMARY, ENV_THOUGHT_LOGGING, SEMANTIC_SUMMARY_ENABLED_DEFAULT, THOUGHT_LOGGING_ENABLED_DEFAULT } from "./constants.ts";
import { getCachedSettingsJson } from "./utils/cache.ts";
import { join } from "node:path";

function parseBoolEnv(v: string | undefined): boolean | undefined {
	if (v === undefined) return undefined;
	const low = v.trim().toLowerCase();
	if (low === "1" || low === "true" || low === "yes" || low === "on") return true;
	if (low === "0" || low === "false" || low === "no" || low === "off") return false;
	return undefined;
}

function readSettingsFlag(key: string): boolean | undefined {
	try {
		const j = getCachedSettingsJson(join(process.cwd(), ".pi/settings.json"));
		if (!j) return undefined;
		const parts = key.split(".");
		let cur: any = j;
		for (const p of parts) cur = cur?.[p];
		if (typeof cur === "boolean") return cur;
		if (typeof cur === "string") return parseBoolEnv(cur);
	} catch {}
	return undefined;
}

export function isSemanticSummaryEnabled(state?: any): boolean {
	if (state && typeof state.semanticSummaryEnabled === "boolean") return state.semanticSummaryEnabled;
	const env = parseBoolEnv(process.env[ENV_SEMANTIC_SUMMARY]);
	if (env !== undefined) return env;
	const s = readSettingsFlag("pi-quest.semanticSummary.enabled");
	if (s !== undefined) return s;
	return SEMANTIC_SUMMARY_ENABLED_DEFAULT;
}

export function isThoughtLoggingEnabled(state?: any): boolean {
	if (state && typeof state.thoughtLoggingEnabled === "boolean") return state.thoughtLoggingEnabled;
	const env = parseBoolEnv(process.env[ENV_THOUGHT_LOGGING]);
	if (env !== undefined) return env;
	const s = readSettingsFlag("pi-quest.thoughtLogging.enabled");
	if (s !== undefined) return s;
	return THOUGHT_LOGGING_ENABLED_DEFAULT;
}
