import {
  AUTONOMOUS_SUBQUEST_DURING_DRAFTING_DEFAULT,
  ENV_AQM_SUBQUEST_DRAFT,
  ENV_REVIEW_LOCK_STALE_MS,
  ENV_RETRY_DELIVER_AS,
  ENV_RETRY_MAX_TURNS,
  ENV_SEMANTIC_SUMMARY,
  ENV_THOUGHT_LOGGING,
  RETRY_DELIVER_AS_DEFAULT,
  RETRY_MAX_TURNS_DEFAULT,
  REVIEW_LOCK_STALE_MS_DEFAULT,
  SEMANTIC_SUMMARY_ENABLED_DEFAULT,
  THOUGHT_LOGGING_ENABLED_DEFAULT,
} from "./constants.ts";
import { getCachedSettingsJson } from "./utils/cache.ts";
import { join } from "node:path";

function parseBoolEnv(v: string | undefined): boolean | undefined {
  if (v === undefined) return undefined;
  const low = v.trim().toLowerCase();
  if (low === "1" || low === "true" || low === "yes" || low === "on")
    return true;
  if (low === "0" || low === "false" || low === "no" || low === "off")
    return false;
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
  if (state && typeof state.semanticSummaryEnabled === "boolean")
    return state.semanticSummaryEnabled;
  const env = parseBoolEnv(process.env[ENV_SEMANTIC_SUMMARY]);
  if (env !== undefined) return env;
  const s = readSettingsFlag("pi-quest.semanticSummary.enabled");
  if (s !== undefined) return s;
  return SEMANTIC_SUMMARY_ENABLED_DEFAULT;
}

export function isThoughtLoggingEnabled(state?: any): boolean {
  if (state && typeof state.thoughtLoggingEnabled === "boolean")
    return state.thoughtLoggingEnabled;
  const env = parseBoolEnv(process.env[ENV_THOUGHT_LOGGING]);
  if (env !== undefined) return env;
  const s = readSettingsFlag("pi-quest.thoughtLogging.enabled");
  if (s !== undefined) return s;
  return THOUGHT_LOGGING_ENABLED_DEFAULT;
}

export function isAutonomousSubquestDuringDraftingEnabled(
  state?: any,
): boolean {
  if (state && typeof state.autonomousSubquestDuringDrafting === "boolean")
    return state.autonomousSubquestDuringDrafting;
  const env = parseBoolEnv(process.env[ENV_AQM_SUBQUEST_DRAFT]);
  if (env !== undefined) return env;
  const s = readSettingsFlag("pi-quest.aqm.subquestDuringDrafting.enabled");
  if (s !== undefined) return s;
  return AUTONOMOUS_SUBQUEST_DURING_DRAFTING_DEFAULT;
}

export function getReviewLockStaleMs(): number {
  const envRaw = process.env[ENV_REVIEW_LOCK_STALE_MS];
  if (envRaw !== undefined) {
    const n = parseInt(envRaw, 10);
    if (Number.isFinite(n)) return Math.min(300_000, Math.max(1_000, n));
  }
  try {
    const j = getCachedSettingsJson(join(process.cwd(), ".pi/settings.json"));
    if (j) {
      const raw =
        j["pi-quest"]?.reviewLock?.staleMs ?? j["pi-quest.reviewLock.staleMs"];
      const n =
        typeof raw === "string"
          ? parseInt(raw, 10)
          : typeof raw === "number"
            ? raw
            : NaN;
      if (Number.isFinite(n)) return Math.min(300_000, Math.max(1_000, n));
    }
  } catch {}
  return REVIEW_LOCK_STALE_MS_DEFAULT;
}

export function getRetryMaxTurns(state?: any): number {
  const st = state?.retryMaxTurns;
  if (typeof st === "number" && Number.isFinite(st)) return clampRetry(st);
  const envRaw = process.env[ENV_RETRY_MAX_TURNS];
  if (envRaw !== undefined) {
    const n = parseInt(envRaw, 10);
    if (Number.isFinite(n)) return clampRetry(n);
  }
  try {
    const j = getCachedSettingsJson(join(process.cwd(), ".pi/settings.json"));
    if (j) {
      const raw =
        j["pi-quest"]?.retry?.maxTurns ?? j["pi-quest.retry.maxTurns"];
      const n =
        typeof raw === "string"
          ? parseInt(raw, 10)
          : typeof raw === "number"
            ? raw
            : NaN;
      if (Number.isFinite(n)) return clampRetry(n);
    }
  } catch {}
  return RETRY_MAX_TURNS_DEFAULT;
}

export function getRetryDeliverAs(state?: any): "steer" | "nextTurn" {
  const st = state?.retryDeliverAs;
  if (st === "steer" || st === "nextTurn") return st;
  const envRaw = (process.env[ENV_RETRY_DELIVER_AS] || "")
    .trim()
    .toLowerCase() as "steer" | "nextTurn";
  if (envRaw === "steer" || envRaw === "nextTurn") return envRaw;
  try {
    const j = getCachedSettingsJson(join(process.cwd(), ".pi/settings.json"));
    if (j) {
      const raw = j["pi-quest"]?.retry?.deliverAs;
      if (raw === "steer" || raw === "nextTurn") return raw;
    }
  } catch {}
  return RETRY_DELIVER_AS_DEFAULT;
}

function clampRetry(n: number): number {
  if (!Number.isFinite(n)) return RETRY_MAX_TURNS_DEFAULT;
  return Math.min(50, Math.max(0, Math.floor(n)));
}
