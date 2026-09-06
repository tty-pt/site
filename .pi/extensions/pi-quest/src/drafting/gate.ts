// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// HIGH_LEVEL: #plan revision — implementation waits while a revision review runs.
// SPEC: B2 (gate table), B2.1 (exemption, agent-visible blocks, INTERNAL_ERROR).
import type { Pi, PiCtx, ToolCallEvent } from "../hooks/events";
import { getState } from "../app/store";
import { classify } from "../utils/classify";
import { decide, reasonText } from "../domain/gates";
import { hasAnyInFlight, hasInFlight } from "../review/tracker";
import { draftPath } from "../domain/paths";

function pathOf(input: Record<string, unknown>): string | undefined {
  const path = input["path"];
  return typeof path === "string" ? path : undefined;
}

// A subagent child session calling tools while one of our reviews runs is
// treated as that reviewer: read-only, verdict as its only channel. The bus
// never reports child session ids, so any in-flight review marks the window.
function reviewerCaller(ctx: PiCtx): boolean {
  const child = ctx.childSessionId;
  return typeof child === "string" && child.length > 0 && hasAnyInFlight();
}

// While a plan-revision review runs, implementing tightens like drafting:
// the agent ends its turn and waits for the verdict instead of building
// against an un-approved plan. Draft saves still supersede; journal and
// questions stay usable. Block-only — reads and verdicts fall through.
function holdForRevisionReview(event: ToolCallEvent): { block: true; reason: string; terminate?: true } | undefined {
  const state = getState();
  if (state.phase !== "implementing" || state.qid === null || !hasInFlight(state.qid)) return undefined;
  const toolClass = classify(event.toolName, event.input);
  if (toolClass === "journal" || toolClass === "ask") return undefined;
  const file = draftPath(state.qid);
  const path = pathOf(event.input);
  if (toolClass === "write" && path !== undefined && (path === file || path.endsWith(`/${file}`))) return undefined;
  return {
    block: true,
    reason: "AWAITING_REVIEW: PLAN_REVISION_REVIEW — Plan revision under re-review — end your turn; the verdict arrives as a new turn. Draft saves still supersede.",
    terminate: true,
  };
}

export function installDraftGate(pi: Pi): void {
  pi.on("tool_call", (event: ToolCallEvent, ctx: PiCtx) => {
    try {
      const held = holdForRevisionReview(event);
      if (held !== undefined) return held;
      const decision = decide(getState(), {
        toolName: event.toolName,
        toolClass: classify(event.toolName, event.input),
        path: pathOf(event.input),
      }, { isReviewerSession: reviewerCaller(ctx) });
      if (decision.allowed) return undefined;
      const verdict: { block: true; reason: string; terminate?: true } = {
        block: true,
        reason: reasonText(decision),
      };
      if (getState().activeReview !== null) verdict.terminate = true;
      return verdict;
    } catch (err) {
      const detail = err instanceof Error ? err.message : String(err);
      return {
        block: true,
        reason: `UNKNOWN: INTERNAL_ERROR — Quest gate faulted (${detail}); state untouched; reads still work; report this.`,
      };
    }
  });
}
