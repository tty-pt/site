// HIGH_LEVEL: #validation — editing the quest doc from implementing signals
// completion in-file; the touch boots the validator exactly like drafting
// saves boot the draft reviewer — in-turn, so the reviewer actually spawns.
import { getState, updateState } from "../app/store";
import { emitNow, sendSteer } from "../app/interpreter";
import { unfinishedChildren } from "../domain/children";
import { draftPath } from "../domain/paths";
import { parseDocStatus } from "../domain/quest-doc";
import { claimComplete } from "../domain/quest";
import { hashContent } from "../drafting/reviews";
import type { Pi, PiCtx, ToolResultEvent } from "../hooks/events";
import { onToolResult } from "../hooks/events";
import { hasInFlight } from "../review/tracker";
import { readQuestDoc } from "../quest-doc";
import { ensureValidationFlow } from "../validation/flow";
import { maybeBootPlanRevisionReview } from "./plan-revision";

async function handleImplementingDocTouch(pi: Pi, ctx: PiCtx, event: ToolResultEvent): Promise<void> {
  if (event.isError) return;
  if (event.toolName !== "edit" && event.toolName !== "write") return;
  const rawPath = event.input["path"];
  if (typeof rawPath !== "string") return;
  const state = getState();
  if (state.phase !== "implementing" || state.qid === null || state.draft === null) return;
  if (!rawPath.endsWith(draftPath(state.qid))) return;
  const content = await readQuestDoc(ctx, state.qid);
  if (content === null) return;
  if (!parseDocStatus(content).complete) {
    await maybeBootPlanRevisionReview(pi, ctx, "unreviewed quest-doc edit");
    return;
  }
  const qid = state.qid;
  updateState((s) =>
    s.draft === null ? s : { ...s, draft: { ...s.draft, contentHash: hashContent(content) }, snapshotPending: true }
  );
  if (unfinishedChildren(getState()).length > 0) {
    sendSteer(pi, `Quest ${qid} marked complete in the quest doc, but unfinished children block validation — continue past them before claiming completion.`);
    return;
  }
  if (hasInFlight(qid)) return;
  updateState((s) => claimComplete(s));
  emitNow(pi);
  sendSteer(pi, `Quest ${qid} marked complete in the quest doc. Validator booting against the approved plan.`);
  await ensureValidationFlow(pi, ctx);
}

export function watchImplementingDocBoot(pi: Pi): void {
  onToolResult(pi, (event, eventCtx) => {
    void handleImplementingDocTouch(pi, eventCtx, event);
  });
}