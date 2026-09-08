import {
  formatTokens,
  isPctInput,
  parsePct,
  parseTokens,
  resolveThreshold,
  resolveWarningThreshold,
} from "./policy.ts";
import {
  defaultConfig,
  loadConfig,
  readSettingsHints,
  resetConfigFile,
  saveConfig,
  type EarlyCompactConfig,
} from "./config.ts";
import type { UltraCtx, UltraPi } from "./trigger.ts";

export interface CommandDeps {
  loadConfigFile?: () => EarlyCompactConfig;
  saveConfigFile?: (cfg: EarlyCompactConfig) => void;
  resetConfigFileFn?: () => void;
}

export function installCommand(pi: UltraPi, deps?: Partial<CommandDeps>): void {
  pi.registerCommand("early-compact", {
    description:
      "Configure early-compaction: threshold as percentage or tokens, optional warning margin, mid-run toggle. Usage: /early-compact [pct%|tokens] [warningTokens], /early-compact default, /early-compact off, /early-compact midrun on|off",
    getArgumentCompletions: (prefix) =>
      ["75%", "60%", "40%", "300k", "30k", "default", "off", "on", "midrun on", "midrun off", "midrun show"]
        .filter((c) => c.startsWith(prefix))
        .map((value) => ({ value, label: value })),
    handler: (args, ctx) => {
      const fullDeps = deps ?? {};
      handleCommand(args.trim(), ctx, fullDeps);
    },
  });
}

function current(usage: { tokens: number | null; contextWindow: number }, cfg: EarlyCompactConfig): {
  pct: string;
  threshold: string;
  warning: string;
  tier: string;
} {
  const settings = readSettingsHints();
  const resolved = resolveThreshold({ config: cfg, usage }, undefined, settings);
  const warningThreshold = resolveWarningThreshold(
    { config: cfg, usage, threshold: resolved },
    undefined,
    settings,
  );
  const windowStr = usage.contextWindow > 0 ? formatTokens(usage.contextWindow) : "?";
  const pctStr = usage.tokens != null && usage.contextWindow > 0
    ? `${Math.round((usage.tokens / usage.contextWindow) * 100)}%`
    : "?";
  const tierStr = cfg.adaptive ? `adaptive (window ${windowStr})` : "manual";
  return {
    pct: pctStr,
    threshold: formatTokens(resolved.threshold),
    warning: `${formatTokens(warningThreshold)}`,
    tier: tierStr,
  };
}

function statusMsg(cfg: EarlyCompactConfig, ctx: UltraCtx): string {
  const usage = ctx.getContextUsage?.() ?? { tokens: null, contextWindow: 0 };
  const s = current(usage, cfg);
  const state = cfg.enabled
    ? cfg.thresholdPct !== null
      ? `threshold ${cfg.thresholdPct}%`
      : cfg.thresholdTokens !== null
        ? `threshold ${formatTokens(cfg.thresholdTokens)}`
        : `auto (${s.tier})`
    : "disabled";
  return `early-compact: ${state} · current ${s.pct} (compact at ${s.threshold}, warn at ${s.warning}) · midrun ${cfg.midRunCompact ? "on" : "off"} · usage /early-compact <pct%|tokens> [warning], default, off, midrun on|off`;
}

interface Handler {
  load(): EarlyCompactConfig;
  save(cfg: EarlyCompactConfig): void;
  reset(): void;
  notify(msg: string, kind?: "info" | "warning" | "error"): void;
}

function applyKeyword(
  lower: string,
  cfg: EarlyCompactConfig,
  h: Handler,
  ctx: UltraCtx,
): boolean {
  if (lower === "off" || lower === "disable" || lower === "disabled" || lower === "0") {
    h.save({ ...cfg, enabled: false });
    h.notify("early-compact: auto-compaction disabled. Pi's built-in compaction still applies.", "info");
    return true;
  }
  if (lower === "on" || lower === "enable" || lower === "enabled") {
    h.save({ ...cfg, enabled: true });
    h.notify("early-compact: enabled.", "info");
    return true;
  }
  if (lower === "default" || lower === "reset") {
    h.reset();
    h.notify(statusMsg(defaultConfig(), ctx));
    return true;
  }
  if (lower === "adaptive" || lower === "auto") {
    const next = { ...cfg, adaptive: true, thresholdPct: null, thresholdTokens: null };
    h.save(next);
    h.notify(statusMsg(next, ctx));
    return true;
  }
  if (lower === "manual") {
    const next = { ...cfg, adaptive: false, thresholdPct: null, thresholdTokens: null };
    h.save(next);
    h.notify(statusMsg(next, ctx));
    return true;
  }
  return false;
}

function applyMidRun(parts: string[], cfg: EarlyCompactConfig, h: Handler): boolean {
  if (parts[0].toLowerCase() !== "midrun") return false;
  const arg = parts[1]?.toLowerCase();
  if (arg === "on" || arg === "enable") {
    h.save({ ...cfg, midRunCompact: true });
    h.notify("early-compact: mid-run compaction enabled — compacts at the next turn boundary during a run.", "info");
    return true;
  }
  if (arg === "off" || arg === "disable") {
    h.save({ ...cfg, midRunCompact: false });
    h.notify("early-compact: mid-run compaction disabled — compacts only before the next prompt.", "info");
    return true;
  }
  if (arg === undefined || arg === "show") {
    const autoContinue = cfg.midRunCompact && cfg.continueAfterCompact ? " (auto-continue)" : "";
    h.notify(`early-compact: mid-run ${cfg.midRunCompact ? "on" : "off"}${autoContinue} · usage /early-compact midrun on|off`, "info");
    return true;
  }
  h.notify("Usage: /early-compact midrun on|off|show", "warning");
  return true;
}

function applyThreshold(parts: string[], cfg: EarlyCompactConfig, h: Handler): boolean {
  const firstPct = parsePct(parts[0]);
  const firstTokens = firstPct === null ? parseTokens(parts[0]) : null;

  if (parts.length === 1 && firstPct !== null) {
    h.save({ ...cfg, thresholdPct: firstPct, thresholdTokens: null });
    h.notify(`early-compact: threshold ${firstPct}%`, "info");
    return true;
  }
  if (parts.length === 1 && firstTokens !== null) {
    h.save({ ...cfg, thresholdTokens: firstTokens, thresholdPct: null });
    h.notify(`early-compact: threshold ${formatTokens(firstTokens)}`, "info");
    return true;
  }
  if (parts.length >= 2) {
    if (firstPct !== null) {
      const warning = parseTokens(parts[1]);
      if (warning !== null && warning > 0) {
        h.save({ ...cfg, thresholdPct: firstPct, thresholdTokens: null, warningMarginTokens: warning });
        h.notify(`early-compact: threshold ${firstPct}%, warning margin ${formatTokens(warning)}`, "info");
        return true;
      }
    }
    if (firstTokens !== null) {
      const warning = parseTokens(parts[1]);
      if (warning !== null && warning > 0) {
        h.save({ ...cfg, thresholdTokens: firstTokens, thresholdPct: null, warningMarginTokens: warning });
        h.notify(`early-compact: threshold ${formatTokens(firstTokens)}, warning margin ${formatTokens(warning)}`, "info");
        return true;
      }
    }
    h.notify("Usage: /early-compact <pct%|tokens> [warning]", "warning");
    return true;
  }
  return false;
}

function handleCommand(args: string, ctx: UltraCtx, deps: CommandDeps): void {
  const h: Handler = {
    load: deps.loadConfigFile ?? loadConfig,
    save: deps.saveConfigFile ?? saveConfig,
    reset: deps.resetConfigFileFn ?? resetConfigFile,
    notify: (msg, kind = "info") => {
      try {
        ctx.ui?.notify?.(msg, kind);
      } catch {
        // Best-effort UI.
      }
    },
  };
  const cfg = h.load();

  if (args === "") {
    h.notify(statusMsg(cfg, ctx));
    return;
  }

  const lower = args.toLowerCase();
  if (applyKeyword(lower, cfg, h, ctx)) return;

  const parts = args.split(/\s+/).filter((p) => p.length > 0);
  if (applyMidRun(parts, cfg, h)) return;
  if (applyThreshold(parts, cfg, h)) return;

  h.notify(`Cannot parse "${args}". Try /early-compact 75% , /early-compact 300k 30k , default , off`, "warning");
}