import { join } from "node:path";
import { readFileSync } from "node:fs";
import { getCachedSettingsJson } from "../utils/cache.ts";
import {
	DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS,
} from "../constants.ts";
import { getActiveContext, getState } from "../state.ts";
import { CompactionPressure, ExtensionContext, StoredState } from "../types.ts";
import { parseTokenAmount } from "../utils.ts";

// --- Minimal settings helpers retained for subquest launch threshold ---
function getProjectJson(ctx?: ExtensionContext){const c=getActiveContext(ctx);const p=join((c?.cwd||process.cwd()),".pi/settings.json");return getCachedSettingsJson(p);}
function getHomeJson(){try{const p="~/.pi/agent/settings.json".replace(/^~/,process.env.HOME||"");const r=readFileSync(p,"utf8");return JSON.parse(r)}catch{return null}}

export function getSettingsCeiling(_ctx?: ExtensionContext): number {
	// @deprecated periodic checkpoint does not use token ceiling
	return 400_000;
}

export function readSettingsSubquestThreshold(ctx?: ExtensionContext): number | null {
	const pj=getProjectJson(ctx);
	if(pj){
		const piQ = pj["pi-quest"] ?? pj.piQuest;
		if(piQ){const v=piQ?.subquestCompactTokens??piQ?.subquestThreshold;const parsed=parseTokenAmount(v,DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);if(parsed!==null)return parsed}
		const v=pj?.questJournal?.subquestCompactTokens??pj?.questJournal?.subquestThreshold??pj?.compaction?.subquestTokens;const parsed=parseTokenAmount(v,DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);if(parsed!==null)return parsed
	}
	const hj=getHomeJson();
	if(hj){
		const piQ = hj["pi-quest"] ?? hj.piQuest;
		if(piQ){const v=piQ?.subquestCompactTokens??piQ?.subquestThreshold;const parsed=parseTokenAmount(v,DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);if(parsed!==null)return parsed}
		const v=hj?.questJournal?.subquestCompactTokens??hj?.questJournal?.subquestThreshold??hj?.compaction?.subquestTokens;const parsed=parseTokenAmount(v,DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);if(parsed!==null)return parsed
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

// --- Deprecated stubs — periodic checkpoint replaces compaction pressure ---
// Kept for backwards-compat imports; always return neutral values.

export function getSettingsMaxPct(): number { return 90; }
export function getSettingsWarningDelta(): number { return 10; }
export function getSettingsAdaptiveTiers(): null { return null; }
export function isAdaptiveDisabled(): boolean { return true; }
export function clampToModelLimit(raw: number): number { return raw; }
export function adaptiveEconomyPct(): number { return 50; }
export function adaptiveWarningPct(): number { return 40; }
export function readSettingsEconomyThreshold(): null { return null; }
export function readSettingsWarningThreshold(): null { return null; }
export function readSettingsWarningMargin(): null { return null; }
export function getEconomyThreshold(): number { return 0; }
export function getWarningThreshold(): number { return 0; }
export function getWarningMargin(): number { return 0; }
export function getCompactionPressure(): {
	pressure: CompactionPressure;
	tokens: number | null;
	threshold: number;
	warningThreshold: number;
	warningMargin: number;
	fraction: number;
} {
	return {
		pressure: CompactionPressure.NONE,
		tokens: null,
		threshold: 0,
		warningThreshold: 0,
		warningMargin: 0,
		fraction: 0,
	};
}
