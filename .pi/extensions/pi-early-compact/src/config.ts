import { mkdirSync, readFileSync, renameSync, rmSync, writeFileSync } from "node:fs";
import { join } from "node:path";
import { homedir } from "node:os";

// pi-early-compact is a standalone extension: it configures how EARLY pi
// compacts (the trigger policy). It never performs the trim itself and never
// reaches into pi-vcc — compaction is always delegated to pi's ctx.compact(),
// which fires session_before_compact; whoever handles that (pi's default
// compactor, or pi-vcc when its override is enabled) does the actual cut.
// That is the compatibility-without-dependency contract held throughout this
// package.

export const DEFAULT_PCT = 50;
export const DEFAULT_WARNING_DELTA_PCT = 10;
export const MAX_CONTEXT_PCT = 90;

// An anchor on the adaptive curve: at a window of `upTo` tokens the compact
// threshold is either `pct` percent of the window or an absolute `tokens`
// amount. Exactly one of the two must be present.
export interface AdaptiveTier {
  upTo: number;
  pct?: number;
  tokens?: number;
}

export const DEFAULT_ADAPTIVE_TIERS: ReadonlyArray<AdaptiveTier> = [
  { upTo: 250_000, pct: 75 },
  { upTo: 500_000, pct: 60 },
  { upTo: 1_000_000, pct: 40 },
];

export interface EarlyCompactConfig {
  enabled: boolean;
  adaptive: boolean;
  midRunCompact: boolean;
  continueAfterCompact: boolean;
  thresholdPct: number | null;
  thresholdTokens: number | null;
  warningPct: number | null;
  warningTokens: number | null;
  warningMarginTokens: number | null;
}

export function defaultConfig(): EarlyCompactConfig {
  return {
    enabled: true,
    adaptive: true,
    midRunCompact: true,
    continueAfterCompact: true,
    thresholdPct: null,
    thresholdTokens: null,
    warningPct: null,
    warningTokens: null,
    warningMarginTokens: null,
  };
}

export function resolveConfigFile(): string {
  const base = process.env.PI_EARLY_COMPACT_CONFIG_PATH;
  if (base) return base;
  return join(homedir(), ".pi", "agent", "pi-early-compact.json");
}

function validPct(value: unknown): number | null {
  if (typeof value === "number" && Number.isFinite(value) && value > 0 && value <= 100) {
    return value;
  }
  return null;
}

function validTokens(value: unknown): number | null {
  if (typeof value === "number" && Number.isFinite(value) && value >= 0) {
    return value;
  }
  return null;
}

function coerce(raw: Record<string, unknown>, fallback: EarlyCompactConfig): EarlyCompactConfig {
  return {
    enabled: typeof raw.enabled === "boolean" ? raw.enabled : fallback.enabled,
    adaptive: typeof raw.adaptive === "boolean" ? raw.adaptive : fallback.adaptive,
    midRunCompact: typeof raw.midRunCompact === "boolean" ? raw.midRunCompact : fallback.midRunCompact,
    continueAfterCompact: typeof raw.continueAfterCompact === "boolean" ? raw.continueAfterCompact : fallback.continueAfterCompact,
    thresholdPct: validPct(raw.thresholdPct) ?? fallback.thresholdPct,
    thresholdTokens: validTokens(raw.thresholdTokens) ?? fallback.thresholdTokens,
    warningPct: validPct(raw.warningPct) ?? fallback.warningPct,
    warningTokens: validTokens(raw.warningTokens) ?? fallback.warningTokens,
    warningMarginTokens: validTokens(raw.warningMarginTokens) ?? fallback.warningMarginTokens,
  };
}

export function loadConfig(cfgFile?: string): EarlyCompactConfig {
  const file = cfgFile ?? resolveConfigFile();
  try {
    const raw = JSON.parse(readFileSync(file, "utf8"));
    if (raw && typeof raw === "object") return coerce(raw, defaultConfig());
  } catch {
    // Missing or corrupt config falls back to defaults; the extension is
    // always loadable-and-graceful.
  }
  return defaultConfig();
}

export interface SettingsHints {
  thresholdPct: number | null;
  thresholdTokens: number | null;
  warningPct: number | null;
  warningTokens: number | null;
  warningMarginTokens: number | null;
  tiers: AdaptiveTier[] | null;
}

export function emptySettingsHints(): SettingsHints {
  return {
    thresholdPct: null,
    thresholdTokens: null,
    warningPct: null,
    warningTokens: null,
    warningMarginTokens: null,
    tiers: null,
  };
}

function pctOrTokens(raw: unknown, out: SettingsHints): void {
  if (raw === null || raw === undefined) return;
  const asPct = parsePercentValue(raw);
  if (asPct !== null) {
    out.thresholdPct = asPct;
    return;
  }
  const asTokens = parseIntegerValue(raw);
  if (asTokens !== null && asTokens > 0) out.thresholdTokens = asTokens;
}

// Read legacy/companion threshold hints from pi's settings files so the "c"
// ordering matches pi-quest v1: config file > env > settings.json > default.
// Only numeric primitives are honored; the files may be JSON or .jsonc.
export function readSettingsHints(): SettingsHints {
  const out = emptySettingsHints();
  const candidates = [
    process.env.PI_EARLY_COMPACT_SETTINGS_PATH,
    join(process.cwd(), ".pi", "settings.json"),
    join(homedir(), ".pi", "agent", "settings.json"),
  ].filter((p): p is string => Boolean(p));
  for (const file of candidates) {
    const raw = tryReadJson(file);
    if (!raw || typeof raw !== "object") continue;
    const node = (raw as Record<string, unknown>)["pi-early-compact"] ??
      (raw as Record<string, unknown>)["pi_early_compact"] ??
      (raw as Record<string, unknown>)["compaction"];
    if (node && typeof node === "object") {
      const n = node as Record<string, unknown>;
      const threshold = n["threshold"] ?? n["economyTokens"] ?? n["autoCompactTokens"];
      pctOrTokens(threshold, out);
      const wpct = n["warningPercent"] ?? n["warningPct"];
      const parsedW = parsePercentValue(wpct);
      if (parsedW !== null) out.warningPct = parsedW;
      const wtoks = n["warningTokens"] ?? n["warningMarginTokens"] ?? n["preCompactWarningTokens"];
      const parsedWt = parseIntegerValue(wtoks);
      if (parsedWt !== null && parsedWt > 0) out.warningTokens = parsedWt;
      const parsedTiers = parseTiers(n["tiers"]);
      if (parsedTiers !== null && parsedTiers.length > 0) out.tiers = parsedTiers;
    }
  }
  return out;
}

function parseTiers(raw: unknown): AdaptiveTier[] | null {
  if (!Array.isArray(raw)) return null;
  const tiers: AdaptiveTier[] = [];
  for (const entry of raw) {
    if (!entry || typeof entry !== "object") return null;
    const e = entry as Record<string, unknown>;
    const upTo = parseIntegerValue(e["upTo"]);
    if (upTo === null || upTo <= 0) return null;
    const pct = parsePercentValue(e["pct"]);
    const tokens = parseIntegerValue(e["tokens"]);
    if (pct !== null && tokens === null) {
      tiers.push({ upTo, pct });
    } else if (pct === null && tokens !== null && tokens > 0) {
      tiers.push({ upTo, tokens });
    } else {
      return null; // exactly one pct/tokens required
    }
  }
  tiers.sort((a, b) => a.upTo - b.upTo);
  return tiers;
}

function tryReadJson(file: string): unknown {
  try {
    return JSON.parse(readFileSync(file, "utf8"));
  } catch {
    return null; // missing, corrupt, or a .jsonc that pi's own loader tolerates
  }
}

function parsePercentValue(raw: unknown): number | null {
  if (typeof raw === "number" && Number.isFinite(raw) && raw > 0 && raw < 100) return raw;
  if (typeof raw === "string") {
    const m = /^\s*(\d+(?:\.\d+)?)\s*%\s*$/.exec(raw);
    if (m) {
      const v = Number(m[1]);
      if (v > 0 && v < 100) return v;
    }
  }
  return null;
}

function parseIntegerValue(raw: unknown): number | null {
  if (typeof raw === "number" && Number.isFinite(raw) && raw > 0) return Math.round(raw);
  if (typeof raw === "string") {
    const m = /^\s*(\d+)\s*(k)?\s*$/i.exec(raw);
    if (m) return Math.round(Number(m[1]) * (m[2] ? 1000 : 1));
  }
  return null;
}

export function saveConfig(config: EarlyCompactConfig, cfgFile?: string): void {
  const file = cfgFile ?? resolveConfigFile();
  const dir = file.slice(0, Math.max(file.lastIndexOf("/"), 0));
  if (dir.length > 0) mkdirSync(dir, { recursive: true });

  let existing: Record<string, unknown> = {};
  try {
    const raw = JSON.parse(readFileSync(file, "utf8"));
    if (raw && typeof raw === "object") existing = raw as Record<string, unknown>;
  } catch {
    // Start clean when the current file is missing or corrupt.
  }

  const merged: Record<string, unknown> = {
    ...existing,
    enabled: config.enabled,
    adaptive: config.adaptive,
    midRunCompact: config.midRunCompact,
    continueAfterCompact: config.continueAfterCompact,
  };
  if (config.thresholdPct !== null) merged.thresholdPct = config.thresholdPct;
  else delete merged.thresholdPct;
  if (config.thresholdTokens !== null) merged.thresholdTokens = config.thresholdTokens;
  else delete merged.thresholdTokens;
  if (config.warningPct !== null) merged.warningPct = config.warningPct;
  else delete merged.warningPct;
  if (config.warningTokens !== null) merged.warningTokens = config.warningTokens;
  else delete merged.warningTokens;
  if (config.warningMarginTokens !== null) merged.warningMarginTokens = config.warningMarginTokens;
  else delete merged.warningMarginTokens;

  const tempFile = `${file}.${process.pid}.${Date.now()}.tmp`;
  writeFileSync(tempFile, `${JSON.stringify(merged, null, 2)}\n`, "utf8");
  try {
    renameSync(tempFile, file);
  } catch (error) {
    try {
      rmSync(tempFile, { force: true });
    } catch {
      // Best-effort cleanup; the rename error matters more.
    }
    throw error;
  }
}

export function resetConfigFile(cfgFile?: string): void {
  try {
    rmSync(cfgFile ?? resolveConfigFile(), { force: true });
  } catch {
    // Removing the file degrades to defaults on next load.
  }
}