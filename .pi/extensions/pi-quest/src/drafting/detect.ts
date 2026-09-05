// HIGH_LEVEL: #quest creation — created automatically when the user requests
// and no quest is active. The request becomes the initial objective and scope.
import { getState, replaceState } from "../app/store";
import { emitNow, sendSteer } from "../app/interpreter";
import { createQuest } from "../domain/quest";
import { nextQid } from "../domain/qid";
import { ensureDraftFile, listKnownQids } from "../files";
import type { Pi, PiCtx } from "../hooks/events";
import { onUserMessage } from "../hooks/events";
import { classifyUserMessage } from "../subquests/triage";

export async function detectSubstantiveRequest(pi: Pi, ctx: PiCtx, text: string): Promise<boolean> {
  if (getState().qid !== null) return false;
  const trimmed = text.trim();
  if (trimmed.startsWith("/")) return false;
  const kind = classifyUserMessage(trimmed);
  const substantive = kind === "refinement" || (kind === "question" && trimmed.length >= 250);
  if (!substantive) return false;
  const qid = nextQid(Date.now() / 1000, await listKnownQids(ctx.cwd));
  replaceState(createQuest(trimmed, qid));
  await ensureDraftFile(ctx, qid, qid, trimmed);
  emitNow(pi);
  sendSteer(
    pi,
    `New quest ${qid} opened as provisional: "${trimmed.slice(0, 200)}". Flow: (1) investigate, record findings via quest_update_state; ` +
      `(2) create the draft via quest_update_state {draftName} — the scaffold is already at .pi/quest/future/${qid}.md, no need to create directories; ` +
      `(3) edit ONLY that file's ## Implementation Plan. Never write .pi/quest/current/ — it renders at archive.`,
  );
  return true;
}

export function watchNewRequests(pi: Pi): void {
  onUserMessage(pi, (text, ctx) => {
    void detectSubstantiveRequest(pi, ctx, text);
  });
}
