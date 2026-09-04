import { getActiveContext } from "../state.ts";
import { ExtensionContext } from "../types.ts";
import { logEvent } from "./core.ts";
import { QuestLogContext, QuestLogEventType } from "./types.ts";

export function logErrorMessage(err: unknown): string {
  if (err && typeof err === "object" && "message" in err) {
    const m = (err as { message?: unknown }).message;
    if (typeof m === "string" && m.length > 0) return m;
  }
  return "unknown";
}

function notifyLoggingFailure(
  ctx: ExtensionContext | undefined,
  err: unknown,
): void {
  try {
    const c = getActiveContext(ctx);
    if (c?.hasUI && typeof c.ui?.notify === "function") {
      c.ui.notify(
        `Quest Journal Warning: log call failed (${logErrorMessage(err)})`,
        "warning",
      );
    }
  } catch {
    // Surface only; never let the error-path itself throw.
  }
}

/**
 * Log an event, but if the underlying logging call throws, surface the failure
 * to the UI instead of silently swallowing it. This replaces the pervasive
 * `try { logEvent(...) } catch {}` pattern (#29).
 *
 * The fallback only reaches the UI via notify — it never re-enters `logEvent`
 * (which would risk recursive logging failures).
 */
export function tryLog(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
  ctx?: ExtensionContext,
): void {
  try {
    logEvent(type, message, context, ctx);
  } catch (err) {
    notifyLoggingFailure(ctx, err);
  }
}
