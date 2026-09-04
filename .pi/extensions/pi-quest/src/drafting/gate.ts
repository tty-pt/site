// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// SPEC: B2 (gate table), B2.1 (exemption, agent-visible blocks, INTERNAL_ERROR).
import type { Pi, PiCtx, ToolCallEvent } from "../hooks/events";
import { getState } from "../app/store";
import { classify } from "../utils/classify";
import { decide, reasonText } from "../domain/gates";
import { isReviewerSession } from "../review/tracker";

function pathOf(input: Record<string, unknown>): string | undefined {
  const path = input["path"];
  return typeof path === "string" ? path : undefined;
}

function reviewerCaller(ctx: PiCtx): boolean {
  const child = ctx.childSessionId;
  return typeof child === "string" && child.length > 0 && isReviewerSession(child);
}

export function installDraftGate(pi: Pi): void {
  pi.on("tool_call", (event: ToolCallEvent, ctx: PiCtx) => {
    try {
      const decision = decide(getState(), {
        toolName: event.toolName,
        toolClass: classify(event.toolName, event.input),
        path: pathOf(event.input),
      }, { isReviewerSession: reviewerCaller(ctx) });
      if (decision.allowed) return undefined;
      return { block: true, reason: reasonText(decision) };
    } catch (err) {
      const detail = err instanceof Error ? err.message : String(err);
      return {
        block: true,
        reason: `UNKNOWN: INTERNAL_ERROR — Quest gate faulted (${detail}); state untouched; reads still work; report this.`,
      };
    }
  });
}
