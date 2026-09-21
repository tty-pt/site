// HIGH_LEVEL: #modes — kind-aware draft PASS routing: standard quests promote
// to implementing; analysis quests auto-claim straight to validating (D3),
// falling back to a work phase while sub-quests are still running.
import { emitNow } from "../app/interpreter";
import { getState, updateState } from "../app/store";
import type { ApprovedBy } from "../domain/quest";
import { promote, promoteToValidation } from "../domain/transitions";
import { unfinishedChildren } from "../domain/children";
import type { Qid } from "../domain/qid";
import { launchValidation } from "../validation/flow";
import type { Pi, PiCtx } from "../hooks/events";

// Shared by the reviewer-PASS auto-promote path (drafting/reviews.ts) and the
// live-user "go" path. Returns the promotion message to steer/wake — the
// caller's medium differs — or null when the quest is no longer drafting.
export function promoteAfterPass(
  pi: Pi,
  ctx: PiCtx,
  qid: Qid,
  by: ApprovedBy,
): string | null {
  const state = getState();
  if (state.phase !== "drafting" || state.qid !== qid) return null;
  const stateQid = state.qid;
  const how = by === "user" ? 'user "go"' : "reviewer PASS";
  if (state.kind === "analysis") {
    const unfinished = unfinishedChildren(state);
    if (unfinished.length > 0) {
      updateState((s) => promote(s, by));
      emitNow(pi);
      return `Quest ${stateQid} promoted to implementing (${how}) — the analysis stays provisional while sub-quests run. Complete and acknowledge each child, then save to re-review; a live user "go" may also override.`;
    }
    updateState((s) => promoteToValidation(s, by));
    emitNow(pi);
    void launchValidation(pi, ctx, stateQid);
    return `Quest ${stateQid} reached the approved analysis milestone (${how}): on to validation. The doc stays live until the validator concludes.`;
  }
  updateState((s) => promote(s, by));
  emitNow(pi);
  return `Quest ${stateQid} promoted to implementing (${how}). Proceed autonomously from the draft plan.`;
}