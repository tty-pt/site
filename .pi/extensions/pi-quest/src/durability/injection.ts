// HIGH_LEVEL: #surviving — state re-read before every reply.
// SPEC: B1.6 (pending flag visible to the agent).
import type { Pi } from "../hooks/events";
import { getState } from "../app/store";
import type { QuestState } from "../domain/quest";
import { draftPath } from "../domain/paths";
import { SNAPSHOT_TYPE } from "./snapshots";

function contextLine(state: QuestState): string {
  const id = state.qid ?? "none";
  const pending = state.snapshotPending ? " (snapshot pending)" : "";
  const review = state.activeReview === null
    ? ""
    : ` Review ${state.activeReview.kind} running on ${state.activeReview.target}.`;
  const draft = state.phase === "drafting" && state.qid !== null
    ? ` Draft file: ${draftPath(state.qid)}.`
    : "";
  return `Quest ${id} — phase ${state.phase}${pending}.${review}${draft} ${state.exactNextAction}`;
}

export function injectQuestContext(pi: Pi): void {
  pi.on("before_agent_start", () => {
    try {
      const state = getState();
      if (state.phase === "idle") return undefined;
      return { message: { customType: SNAPSHOT_TYPE, content: contextLine(state) } };
    } catch {
      return undefined;
    }
  });
}
