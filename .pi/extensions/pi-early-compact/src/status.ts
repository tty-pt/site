import {
  buildPressure,
  resolveThreshold,
  resolveWarningThreshold,
} from "./policy.ts";
import { loadConfig, readSettingsHints, type EarlyCompactConfig } from "./config.ts";
import type { UltraCtx, UltraPi } from "./trigger.ts";

const STATUS_KEY = "pi-early-compact";

const BALL = "●";

export function installStatusFooter(pi: UltraPi, loadConfigFile?: () => EarlyCompactConfig): void {
  pi.on("turn_end", (_e, ctx) => {
    const cfg = loadConfigFile?.() ?? loadConfig();
    if (!cfg.enabled) {
      setStatus(ctx, undefined);
      return;
    }
    const usage = ctx.getContextUsage?.();
    if (!usage || usage.tokens == null || usage.contextWindow <= 0) {
      setStatus(ctx, undefined);
      return;
    }
    const settings = readSettingsHints();
    const resolved = resolveThreshold({ config: cfg, usage }, undefined, settings);
    const warningThreshold = resolveWarningThreshold(
      { config: cfg, usage, threshold: resolved },
      undefined,
      settings,
    );
    const report = buildPressure(usage, resolved.threshold, warningThreshold);

    const kind =
      report.pressure === "critical" ? "error" : report.pressure === "warning" ? "warning" : "info";
    setStatus(ctx, BALL, kind);
  });
}

function setStatus(ctx: UltraCtx, text: string | undefined, kind?: "info" | "warning" | "error"): void {
  if (!ctx.hasUI || !ctx.ui?.setStatus) return;
  try {
    ctx.ui.setStatus(
      STATUS_KEY,
      text === undefined ? undefined : ctx.ui.theme?.fg ? ctx.ui.theme.fg(kind ?? "info", text) : text,
    );
  } catch {
    // Best-effort: a stale ctx never breaks the turn.
  }
}