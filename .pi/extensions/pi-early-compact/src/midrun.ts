import { resolveThreshold, resolveWarningThreshold } from "./policy.ts";
import { loadConfig, readSettingsHints, type EarlyCompactConfig } from "./config.ts";
import {
  isSoftCompactionError,
  notifySafe,
  setStatusSafe,
  type TriggerDeps,
  type UltraCtx,
  type UltraPi,
} from "./trigger.ts";

// Mid-run compaction: unlike the input preflight (which runs while the agent is
// idle), this fires during an active run. Manual compact aborts the running
// turn, so we never await it inside the context handler (awaiting deadlocks:
// compact() -> abort() -> waitForIdle() blocks on the very run that is stuck
// awaiting this handler). Instead we fire-and-forget; the turn settles at the
// boundary, and once compaction finished we queue a standalone continuation via
// sendMessage({triggerTurn:true}) so the task resumes on the fresh context.
const CONTINUE_MESSAGE =
  "pi-early-compact compacted the context mid-run. Continue the current task.";

export function installMidRunGuard(deps: TriggerDeps): void {
  const { pi } = deps;
  let sessionGeneration = 0;
  let armed = true;
  let inFlight: Promise<Error | null> | null = null;

  const readConfig = () => deps.loadConfigFile?.() ?? loadConfig();

  pi.on("session_start", (_e, ctx) => {
    sessionGeneration++;
    armed = true;
    setStatusSafe(ctx, undefined);
  });

  pi.on("session_shutdown", () => {
    sessionGeneration++;
    armed = true;
  });

  pi.on("context", (_event, ctx) => {
    // The context event only fires during runs; signal/compact are belt-and-
    // braces so a host that reuses the handler never acts while idle or unable.
    if (!ctx.signal || !ctx.compact) return;

    const cfg = readConfig();
    if (!cfg.enabled || !cfg.midRunCompact) return;

    const usage = ctx.getContextUsage?.();
    if (!usage || usage.tokens == null || usage.contextWindow <= 0) return;

    const settings = readSettingsHints();
    const resolved = resolveThreshold({ config: cfg, usage }, undefined, settings);
    const warningThreshold = resolveWarningThreshold(
      { config: cfg, usage, threshold: resolved },
      undefined,
      settings,
    );

    if (usage.tokens < resolved.threshold) {
      // Below the compact line: re-arm only once usage falls under the warning
      // band, so each crossing compacts at most once (hysteresis).
      if (usage.tokens < warningThreshold) armed = true;
      return;
    }
    if (!armed) return;

    const gen = sessionGeneration;
    armed = false;

    if (!inFlight) {
      inFlight = runCompaction(ctx, gen, cfg, pi, () => sessionGeneration).finally(() => {
        inFlight = null;
      });
    }
    void inFlight.then((error) => {
      if (gen !== sessionGeneration) return;
      if (error && !isSoftCompactionError(error)) {
        notifySafe(ctx, `Early compact failed mid-run: ${error.message}. Run paused at the turn boundary — resubmit when ready.`, "error");
      }
    });
  });
}

function queueResume(pi: UltraPi, cfg: EarlyCompactConfig, gen: number, currentGen: () => number): void {
  if (!cfg.continueAfterCompact || typeof pi.sendMessage !== "function") return;
  if (gen !== currentGen()) return;
  pi.sendMessage(
    { customType: "pi-early-compact", content: CONTINUE_MESSAGE, display: true },
    { deliverAs: "steer", triggerTurn: true },
  );
}

// Wrapper around ctx.compact that resumes the run on success and on soft errors
// (the abort already happened, so keeping the run alive beats leaving it dead).
// Hard errors leave the run paused so pi's own compaction stays the safest net.
function runCompaction(
  ctx: UltraCtx,
  gen: number,
  cfg: EarlyCompactConfig,
  pi: UltraPi,
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
        onComplete: () => {
          try {
            if (gen === currentGen()) setStatusSafe(ctx, undefined);
            queueResume(pi, cfg, gen, currentGen);
          } catch {
            // Best-effort; the compact itself already succeeded.
          }
          resolve(null);
        },
        onError: (error) => {
          const soft = isSoftCompactionError(error);
          if (soft) queueResume(pi, cfg, gen, currentGen);
          resolve(error);
        },
      });
    } catch (error) {
      resolve(error instanceof Error ? error : new Error(String(error)));
    }
  });
}