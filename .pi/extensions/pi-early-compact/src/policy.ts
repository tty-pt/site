import {
  DEFAULT_ADAPTIVE_TIERS,
  DEFAULT_PCT,
  DEFAULT_WARNING_DELTA_PCT,
  MAX_CONTEXT_PCT,
  type AdaptiveTier,
  type EarlyCompactConfig,
} from "./config.ts";

export interface ContextUsage {
  tokens: number | null;
  contextWindow: number;
}

export type Pressure = "none" | "warning" | "critical";

export interface PressureReport {
  pressure: Pressure;
  tokens: number;
  threshold: number;
  warningThreshold: number;
  fraction: number;
}

export interface ResolvedThreshold {
  threshold: number;
  thresholdPct: number;
  tierApplied: boolean;
}

export function isPctInput(raw: string): boolean {
  return /^\s*\d+(\.\d+)?\s*%\s*$/.test(raw);
}

export function parsePct(raw: string): number | null {
  const m = /^\s*(\d+(?:\.\d+)?)\s*%\s*$/.exec(raw);
  if (!m) return null;
  const v = Number(m[1]);
  if (!Number.isFinite(v) || v <= 0 || v >= MAX_CONTEXT_PCT) return null;
  return v;
}

export function parseTokens(raw: string): number | null {
  const m = /^\s*(\d+(?:\.\d+)?)\s*(k)?\s*$/i.exec(raw);
  if (!m) return null;
  const mult = m[2] ? 1_000 : 1;
  const v = Number(m[1]) * mult;
  if (!Number.isFinite(v) || v < 0) return null;
  return v;
}

export function formatTokens(tokens: number): string {
  if (tokens >= 1_000_000) return `${(tokens / 1_000_000).toFixed(1).replace(/\.0$/, "")}M`;
  if (tokens >= 1_000) return `${(tokens / 1_000).toFixed(0)}k`;
  return `${tokens}`;
}

// Adaptive curve: anchors (window, compact-pct) are interpolated linearly in
// percent space. A `tokens`-typed anchor converts to percent at its own
// window (tokens / upTo * 100). Windows below the first anchor clamp to its
// percent; above the last anchor clamp to the last percent — the threshold
// then simply scales with the window (no absolute ceiling).
export function selectTierPct(contextWindow: number, tiers: ReadonlyArray<AdaptiveTier> = DEFAULT_ADAPTIVE_TIERS): number {
  if (tiers.length === 0) return DEFAULT_PCT;
  if (contextWindow <= 0) return tierPctAt(tiers[0]);
  if (contextWindow <= tiers[0].upTo) return tierPctAt(tiers[0]);
  const last = tiers[tiers.length - 1];
  if (contextWindow >= last.upTo) return tierPctAt(last);
  for (let i = 0; i + 1 < tiers.length; i++) {
    const a = tiers[i];
    const b = tiers[i + 1];
    if (contextWindow >= a.upTo && contextWindow <= b.upTo) {
      const span = b.upTo - a.upTo;
      if (span <= 0) return tierPctAt(a);
      const t = (contextWindow - a.upTo) / span;
      return tierPctAt(a) * (1 - t) + tierPctAt(b) * t;
    }
  }
  return tierPctAt(last);
}

function tierPctAt(tier: AdaptiveTier): number {
  if (tier.pct !== undefined) return tier.pct;
  if (tier.tokens !== undefined && tier.upTo > 0) return (tier.tokens / tier.upTo) * 100;
  return DEFAULT_PCT;
}

// Unknown-window fallback: derive a placeholder threshold from the first
// anchor (pct_first / 100 x upTo_first) so status stays sane without usage.
export function fallbackThreshold(tiers: ReadonlyArray<AdaptiveTier> = DEFAULT_ADAPTIVE_TIERS): number {
  if (tiers.length === 0) return Math.round((DEFAULT_PCT * 100_000) / 100);
  const first = tiers[0];
  const pct = tierPctAt(first);
  return Math.round((pct * first.upTo) / 100);
}

export function clampToMaxContextPct(pct: number): number {
  if (!Number.isFinite(pct)) return MAX_CONTEXT_PCT;
  return Math.min(pct, MAX_CONTEXT_PCT);
}

// Threshold resolution mirrors pi-quest v1's economy ordering:
// explicit (config) > env > settings.json > adaptive curve / default.
export interface ThresholdInputs {
  config: EarlyCompactConfig;
  usage: Pick<ContextUsage, "contextWindow">;
  settings?: {
    thresholdPct: number | null;
    thresholdTokens: number | null;
    tiers: ReadonlyArray<AdaptiveTier> | null;
  };
}

interface ThresholdPick {
  pct: number | null;
  tokens: number | null;
}

function thresholdPick(cfg: EarlyCompactConfig, env: NodeJS.ProcessEnv | undefined, settings: ThresholdInputs["settings"]): ThresholdPick {
  if (cfg.thresholdPct !== null || cfg.thresholdTokens !== null) {
    return { pct: cfg.thresholdPct, tokens: cfg.thresholdTokens };
  }
  const envPct = configPctFromEnv(env);
  if (envPct !== null) return { pct: envPct, tokens: null };
  if (settings?.thresholdPct ?? settings?.thresholdTokens) {
    return { pct: settings.thresholdPct, tokens: settings.thresholdTokens };
  }
  return { pct: null, tokens: null };
}

export function resolveThreshold(
  inputs: ThresholdInputs,
  env?: NodeJS.ProcessEnv,
  settings?: ThresholdInputs["settings"],
): ResolvedThreshold {
  const cfg = inputs.config;
  const window = inputs.usage.contextWindow;
  const pick = thresholdPick(cfg, env, settings);

  if (pick.pct !== null) {
    const effectivePct = clampToMaxContextPct(pick.pct);
    if (window > 0) {
      return makeResolved(effectivePct, Math.round((window * effectivePct) / 100), false);
    }
    return makeResolved(effectivePct, Math.round((effectivePct * 100_000) / 100), false);
  }

  if (pick.tokens !== null) {
    return makeResolved(tokenToPct(pick.tokens, window), pick.tokens, false);
  }

  if (!cfg.adaptive) {
    const effectivePct = clampToMaxContextPct(DEFAULT_PCT);
    if (window > 0) {
      return makeResolved(effectivePct, Math.round((window * effectivePct) / 100), false);
    }
    return makeResolved(effectivePct, Math.round((DEFAULT_PCT * 100_000) / 100), false);
  }

  const tiers = settings?.tiers ?? DEFAULT_ADAPTIVE_TIERS;
  const tierPct = window > 0 ? selectTierPct(window, tiers) : DEFAULT_PCT;
  const effectivePct = clampToMaxContextPct(tierPct);
  if (window > 0) {
    return makeResolved(effectivePct, Math.round((window * effectivePct) / 100), true);
  }
  return makeResolved(effectivePct, fallbackThreshold(tiers), true);
}

function tokenToPct(tokens: number, window: number): number {
  if (window <= 0) return DEFAULT_PCT;
  return Math.min((tokens / window) * 100, MAX_CONTEXT_PCT);
}

function configPctFromEnv(env?: NodeJS.ProcessEnv): number | null {
  const raw = env?.PI_EARLY_COMPACT_THRESHOLD;
  if (!raw) return null;
  const trimmed = raw.trim();
  if (isPctInput(trimmed)) return parsePct(trimmed);
  const tokens = parseTokens(trimmed);
  if (tokens !== null && tokens > 0) return (tokens / 100_000) * 100; // tokens interpreted against a 100k reference for env shorthand
  return null;
}

function makeResolved(pct: number, threshold: number, tierApplied: boolean): ResolvedThreshold {
  return { threshold, thresholdPct: pct, tierApplied };
}

// Default warning margin: the compact PCT minus a delta, expressed as the
// absolute gap in tokens between warning and compact thresholds.
export function resolveWarningTokens(resolved: ResolvedThreshold, thresholdPct: number, window: number): number {
  if (window <= 0) return Math.max(0, resolved.threshold - 30_000);
  const warningPct = Math.max(1, thresholdPct - DEFAULT_WARNING_DELTA_PCT);
  return Math.max(0, Math.round(window * (thresholdPct - warningPct) / 100));
}

export interface WarningInputs {
  config: EarlyCompactConfig;
  usage: Pick<ContextUsage, "contextWindow">;
  threshold: ResolvedThreshold;
  settings?: {
    warningPct: number | null;
    warningTokens: number | null;
    warningMarginTokens: number | null;
  };
}

export function resolveWarningThreshold(
  inputs: WarningInputs,
  env?: NodeJS.ProcessEnv,
  settings?: WarningInputs["settings"],
): number {
  const cfg = inputs.config;
  const window = inputs.usage.contextWindow;
  const { threshold, thresholdPct } = inputs.threshold;

  if (cfg.warningPct !== null && cfg.warningPct > 0) {
    const wp = clampToMaxContextPct(cfg.warningPct);
    if (wp < thresholdPct) return window > 0 ? Math.round((window * wp) / 100) : threshold;
    return Math.max(0, threshold - resolveWarningTokens(inputs.threshold, thresholdPct, window));
  }
  if (cfg.warningTokens !== null && cfg.warningTokens > 0) {
    return Math.min(cfg.warningTokens, threshold - 1 >= 0 ? threshold - 1 : threshold);
  }
  if (cfg.warningMarginTokens !== null && cfg.warningMarginTokens > 0) {
    return Math.max(0, threshold - cfg.warningMarginTokens);
  }

  const envMargin = env?.PI_EARLY_COMPACT_WARNING_MARGIN_TOKENS;
  if (envMargin) {
    const parsed = parseTokens(envMargin);
    if (parsed !== null && parsed > 0) return Math.max(0, threshold - parsed);
  }

  const envPct = env?.PI_EARLY_COMPACT_WARNING_PCT;
  if (envPct) {
    const parsed = parsePct(envPct);
    if (parsed !== null && parsed > 0) {
      const wp = clampToMaxContextPct(parsed);
      if (wp < thresholdPct) return window > 0 ? Math.round((window * wp) / 100) : threshold;
    }
  }

  if (settings?.warningMarginTokens && settings.warningMarginTokens > 0) {
    return Math.max(0, threshold - settings.warningMarginTokens);
  }
  if (settings?.warningTokens && settings.warningTokens > 0) {
    return Math.min(settings.warningTokens, threshold - 1 >= 0 ? threshold - 1 : threshold);
  }
  if (settings?.warningPct && settings.warningPct > 0) {
    const wp = clampToMaxContextPct(settings.warningPct);
    if (wp < thresholdPct) return window > 0 ? Math.round((window * wp) / 100) : threshold;
  }

  return Math.max(0, threshold - resolveWarningTokens(inputs.threshold, thresholdPct, window));
}

export function buildPressure(
  usage: Pick<ContextUsage, "tokens">,
  threshold: number,
  warningThreshold: number,
): PressureReport {
  const tokens = usage.tokens ?? 0;
  if (tokens >= threshold) {
    const span = Math.max(1, threshold - warningThreshold);
    return {
      pressure: "critical",
      tokens,
      threshold,
      warningThreshold,
      fraction: 1 + (tokens - threshold) / span,
    };
  }
  if (tokens >= warningThreshold) {
    const span = Math.max(1, threshold - warningThreshold);
    return {
      pressure: "warning",
      tokens,
      threshold,
      warningThreshold,
      fraction: (tokens - warningThreshold) / span,
    };
  }
  return { pressure: "none", tokens, threshold, warningThreshold, fraction: 0 };
}