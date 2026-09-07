// HIGH_LEVEL: #modes
// HIGH_LEVEL: #validation — validator archives on PASS, demotes on FAIL.
// HIGH_LEVEL: #validation — retouching the quest doc while validating boots
// a fresh validator in-turn; the rebaselined hash stales any prior PASS.
// SPEC: B1.5 (completion + slim archive).
import { getState, updateState } from "../app/store";
import { draftPath } from "../domain/paths";
import { hashContent } from "../drafting/reviews";
import type { Pi, PiCtx, ToolResultEvent } from "../hooks/events";
import { onSessionStart, onToolResult, onTurnEnd, onUserMessage } from "../hooks/events";
import { readQuestDoc } from "../quest-doc";
import { ensureValidationFlow, handleConfirmInput } from "./flow";

async function onValidationDocTouch(pi: Pi, ctx: PiCtx, event: ToolResultEvent): Promise<void> {
  if (event.isError) return;
  if (event.toolName !== "edit" && event.toolName !== "write") return;
  const rawPath = event.input["path"];
  if (typeof rawPath !== "string") return;
  const state = getState();
  if (state.phase !== "validating" || state.qid === null) return;
  if (!rawPath.endsWith(draftPath(state.qid))) return;
  const content = await readQuestDoc(ctx, state.qid);
  if (content === null) return;
  updateState((s) =>
    s.draft === null ? s : { ...s, draft: { ...s.draft, contentHash: hashContent(content) }, snapshotPending: true }
  );
  await ensureValidationFlow(pi, ctx);
}

export function installValidation(pi: Pi): void {
  onTurnEnd(pi, (_event, ctx) => {
    void ensureValidationFlow(pi, ctx);
  });
  onSessionStart(pi, (_event, ctx) => {
    void ensureValidationFlow(pi, ctx);
  });
  onToolResult(pi, (event, eventCtx) => {
    void onValidationDocTouch(pi, eventCtx, event);
  });
  onUserMessage(pi, (text, ctx) => {
    void handleConfirmInput(pi, ctx, text);
  });
}