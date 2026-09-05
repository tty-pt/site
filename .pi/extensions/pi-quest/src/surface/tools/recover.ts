// HIGH_LEVEL: #tools (main agent) — quest_recover.
// HIGH_LEVEL: #surviving — recovery across sessions is pi-quest's own scan.
import { getState, replaceState } from "../../app/store";
import { IDLE_STATE } from "../../domain/quest";
import { draftPath } from "../../domain/paths";
import { newestSnapshot, newestSnapshotFor } from "../../durability/snapshots";
import { scanSiblingSessions } from "../../durability/siblings";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { textResult } from "./reply";

export async function recoverQuest(ctx: PiCtx, qid: string | null): Promise<{ qid: string | null; source: string }> {
  const entries = ctx.sessionManager.getEntries();
  const target = qid ?? getState().qid;
  if (target !== null) {
    const onBranch = newestSnapshotFor(entries, target);
    if (onBranch !== null) {
      replaceState({ ...onBranch, snapshotPending: false });
      return { qid: onBranch.qid, source: "transcript" };
    }
    const sibling = await scanSiblingSessions(target);
    if (sibling !== null) {
      replaceState({ ...sibling, snapshotPending: false });
      return { qid: sibling.qid, source: "sibling session" };
    }
    return { qid: null, source: `no snapshot for ${target}` };
  }
  const newest = newestSnapshot(entries) ?? await scanSiblingSessions(null);
  if (newest?.qid) {
    replaceState({ ...newest, snapshotPending: false });
    return { qid: newest.qid, source: "transcript" };
  }
  replaceState(IDLE_STATE);
  return { qid: null, source: "cold start" };
}

export function recoverTool(_pi: Pi): PiToolSpec {
  return {
    name: "quest_recover",
    label: "Recover Quest",
    description: "Rebuild quest state from the transcript, including earlier sessions. Runs automatically when state is absent; callable directly.",
    parameters: {
      type: "object",
      properties: {
        qid: { type: "string", description: "Quest id to recover. Defaults to the active quest, then newest." },
      },
      additionalProperties: false,
    },
    execute: async (_toolCallId, params, _signal, _onUpdate, ctx) => {
      const qid = typeof params["qid"] === "string" ? params["qid"] as string : null;
      const done = await recoverQuest(ctx, qid);
      if (done.qid) {
        const state = getState();
        const draft = state.phase === "drafting" && state.qid !== null
          ? ` Draft file: ${draftPath(state.qid)}.`
          : "";
        return textResult(
          `Recovered quest ${done.qid} from ${done.source} (phase ${state.phase}).${draft} ${state.exactNextAction}`,
          { qid: done.qid, source: done.source, phase: state.phase },
        );
      }
      return textResult(`Recovery found nothing (${done.source}).`, { source: done.source });
    },
  };
}
