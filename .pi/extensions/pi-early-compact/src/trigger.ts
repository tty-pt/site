import {
  resolveThreshold,
  resolveWarningThreshold,
  type ContextUsage,
} from "./policy.ts";
import { loadConfig, readSettingsHints, type EarlyCompactConfig } from "./config.ts";

// Structural subset of pi's ExtensionContext: only what the trigger needs,
// capability-detected at runtime so pi-early-compact never imports a pi
// package and degrades gracefully when a capability is absent.
export interface UltraCtx {
  hasUI: boolean;
  mode: string;
  signal?: AbortSignal; // present only while an agent run is active
  getContextUsage?(): ContextUsage | undefined;
  compact?(opts: {
    customInstructions?: string;
    onComplete?: () => void;
    onError?: (error: Error) => void;
  }): void;
  ui?: {
    notify?(message: string, type?: "info" | "warning" | "error"): void;
    setStatus?(key: string, text: string | undefined): void;
    theme?: { fg?(color: string, text: string): string };
  };
}

export interface InputEventStub {
  text: string;
  images?: unknown[];
  streamingBehavior?: unknown;
}

export type InputHandlerResult =
  | "continue"
  | "handled"
  | { action: "continue" }
  | { action: "handled" }
  | void
  | Promise<"continue" | "handled" | { action: "continue" } | { action: "handled" } | void>;

export interface UltraPi {
  on(event: "input", handler: (e: InputEventStub, ctx: UltraCtx) => InputHandlerResult): void;
  on(event: "session_start", handler: (e: unknown, ctx: UltraCtx) => void): void;
  on(event: "session_shutdown", handler: (e?: unknown, ctx?: UltraCtx) => void): void;
  on(event: "turn_end", handler: (e: unknown, ctx: UltraCtx) => void): void;
  on(event: "context", handler: (e: { messages: unknown[] }, ctx: UltraCtx) => void): void;
  sendMessage?(message: UltraSendMessage, options?: UltraSendMessageOptions): void;
  registerCommand(
    name: string,
    options: {
      description?: string;
      getArgumentCompletions?: (prefix: string) => Array<{ value: string; label: string }>;
      handler: (args: string, ctx: UltraCtx) => Promise<void> | void;
    },
  ): void;
}

export interface UltraSendMessage {
  customType?: string;
  content?: string;
  display?: boolean;
  details?: string;
}

export interface UltraSendMessageOptions {
  deliverAs?: "steer" | "followUp" | "nextTurn";
  triggerTurn?: boolean;
}

// Marker Pi's compactor hook uses to opt a compaction into pi-vcc's
// deterministic (zero-LLM) compactor even when overrideDefaultCompaction is
// false. Value mirrors pi-vcc's own PI_VCC_COMPACT_INSTRUCTION; kept local so
// pi-early-compact stays standalone and never imports pi-vcc.
export const PI_VCC_COMPACT_INSTRUCTION = "__pi_vcc__";

const SOFT_COMPACT_ERRORS = ["Nothing to compact", "Already compacted"];

// Resume-safe compaction outcomes. These never trim context, so the running task
// can safely resume on the (unchanged) context instead of hard-pausing:
// - pi-vcc opted out / cancelled (nothing trimmed)
// - the built-in LLM summarizer failed to produce a complete summary (e.g. it hit
//   its output token cap) — pi-core only writes a compaction entry for a COMPLETED
//   summary, so on an incomplete one nothing is committed and the branch is intact.
const RESUME_SAFE_ERRORS = [
  "Compaction cancelled",
  "Nothing to compact",
  "Already compacted",
  "pi-vcc",
  "token cap",
  "summary is incomplete",
  "Summarization aborted",
];

export function isSoftCompactionError(error: Error): boolean {
  return SOFT_COMPACT_ERRORS.some((message) => error.message.includes(message));
}

// A compaction that came back without committing a trimmed context: a pi-vcc opt
// out / cancellation, or a failed (incomplete) summarization where nothing was
// written. In every case the run can safely continue or be automatically resumed.
export function isResumeSafeError(error: Error): boolean {
  return RESUME_SAFE_ERRORS.some((message) => error.message.includes(message));
}

export interface TriggerDeps {
  pi: UltraPi;
  loadConfigFile?: () => EarlyCompactConfig;
  compactOpts?: {
    failOnNoCapability?: boolean; // tests force hard failures to exercise the path
  };
}

export function installPreflight(deps: TriggerDeps): void {
  const { pi } = deps;
  let sessionGeneration = 0;
  let inFlight: Promise<Error | null> | null = null;

  const readConfig = () => deps.loadConfigFile?.() ?? loadConfig();

  pi.on("session_start", (_e, ctx) => {
    sessionGeneration++;
    const status = ctx.ui?.setStatus;
    try {
      if (status) status("pi-early-compact", undefined);
    } catch {
      // Best-effort; a stale ctx never breaks session startup.
    }
  });

  pi.on("session_shutdown", () => {
    sessionGeneration++;
  });

  pi.on("input", async (event, ctx) => {
    // Messages queued during an active run (steer/followUp) cannot be
    // preflighted: ctx.compact() would abort the running agent.
    if (event.streamingBehavior !== undefined) return { action: "continue" };

    const cfg = readConfig();
    if (!cfg.enabled) return { action: "continue" };

    const usage = ctx.getContextUsage?.();
    if (!usage || usage.tokens == null || usage.contextWindow <= 0) return { action: "continue" };

    const settings = readSettingsHints();
    const thresholdResolved = resolveThreshold({ config: cfg, usage }, undefined, settings);
    const warningThreshold = resolveWarningThreshold(
      { config: cfg, usage, threshold: thresholdResolved },
      undefined,
      settings,
    );

    const projected = projectTokens(usage, event);
    if (projected < thresholdResolved.threshold) return { action: "continue" };

    const gen = sessionGeneration;

    if (!inFlight) {
      inFlight = compactAndWait(ctx, gen, () => sessionGeneration).finally(() => {
        inFlight = null;
      });
    }
    const error = await inFlight;
    if (gen !== sessionGeneration) return { action: "continue" };

    if (error) {
      if (isSoftCompactionError(error)) {
        notifySafe(ctx, `Early compact skipped: ${error.message}. Sending prompt anyway.`, "warning");
      } else if (error.message.includes("No compact capability")) {
        // The host exposes no compact() — preflight cannot help; let the
        // prompt flow and rely on pi's own compaction as the final safety net.
        return { action: "continue" };
      } else {
        notifySafe(ctx, `Early compact failed: ${error.message}. Prompt not sent — resubmit when ready.`, "error");
        return { action: "handled" };
      }
    }
    return { action: "continue" };
  });
}

function projectTokens(
  usage: ContextUsage,
  event: { text: string; images?: unknown[] },
): number {
  const textTokens = estimateTokens(event.text);
  const imageTokens = (event.images?.length ?? 0) > 0 ? Math.round(usage.contextWindow * 0.01) : 0;
  return (usage.tokens ?? 0) + textTokens + imageTokens;
}

// Deterministic, dependency-free token projection: ~4 chars/token for code-dominated
// prompts, with a fixed per-turn overhead. pi's own usage.tokens drives the current
// pressure; this only estimates the delta a new prompt will add.
export function estimateTokens(text: string): number {
  if (text.length === 0) return 0;
  return Math.ceil(text.length / 4) + 40;
}

function compactAndWait(
  ctx: UltraCtx,
  gen: number,
  currentGen: () => number,
): Promise<Error | null> {
  return new Promise<Error | null>((resolve) => {
    const compact = ctx.compact;
    if (!compact) {
      resolve(new Error("No compact capability on this ctx"));
      return;
    }
    try {
      compact({
        customInstructions: PI_VCC_COMPACT_INSTRUCTION,
        onComplete: () => {
          try {
            if (gen === currentGen()) setStatusSafe(ctx, undefined);
          } catch {
            // Best-effort cleanup.
          }
          resolve(null);
        },
        onError: (error) => {
          resolve(error);
        },
      });
    } catch (error) {
      resolve(error instanceof Error ? error : new Error(String(error)));
    }
  });
}

// Color text through the host theme, degrading to plain text when the theme
// lacks the color (a throw from fg must never drop the indicator or break
// the turn). Theme color sets vary; "info" is commonly missing.
export function themeColor(ctx: UltraCtx, color: string, text: string): string {
  const fg = ctx.ui?.theme?.fg;
  if (!fg) return text;
  try {
    return fg(color, text);
  } catch {
    return text;
  }
}

export function setStatusSafe(ctx: UltraCtx, text: string | undefined, kind: "info" | "warning" | "error" = "info"): void {
  if (!ctx.hasUI || !ctx.ui?.setStatus) return;
  try {
    ctx.ui.setStatus(
      "pi-early-compact",
      text === undefined ? undefined : kind === "info" ? text : themeColor(ctx, kind, text),
    );
  } catch {
    // A stale ctx never breaks the prompt flow.
  }
}

export function notifySafe(ctx: UltraCtx, text: string, kind: "info" | "warning" | "error"): void {
  try {
    ctx.ui?.notify?.(text, kind);
  } catch {
    // Best-effort UI.
  }
}