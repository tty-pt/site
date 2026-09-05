// HIGH_LEVEL: #surviving — snapshot store + reconstruct.
// SPEC: B1.6, snapshot contract §4.1.
import { reduce } from "../domain/effects";
import { getState, replaceState } from "../app/store";
import { interpret, sendSteer as deliverSteer, type Ports } from "../app/interpreter";
import {
  onBeforeCompact,
  onSessionStart,
  onTurnEnd,
  onTurnStart,
  type Pi,
  type PiCtx,
  type TranscriptEntry,
} from "../hooks/events";
import { injectQuestContext } from "./injection";
import { scanSiblingSessions } from "./siblings";
import { IDLE_STATE, type QuestState } from "../domain/quest";
import { SNAPSHOT_TYPE, reconstruct } from "./snapshots";

let bootstrapped = false;
let booting = false;

function ports(pi: Pi, ctx: PiCtx): Ports {
  return {
    saveSnapshot: (snapshot) => {
      pi.appendEntry(SNAPSHOT_TYPE, snapshot);
    },
    sendSteer: (text) => {
      deliverSteer(pi, text);
    },
    notify: (text) => {
      try {
        ctx.ui.notify(text, "info");
      } catch {
        // Notification is best-effort.
      }
    },
  };
}

export async function loadQuestState(
  entries: readonly TranscriptEntry[],
  sessionsDir?: string,
): Promise<QuestState> {
  const branch = reconstruct(entries);
  if (branch.phase !== "idle" || branch.qid !== null) return branch;
  return (await scanSiblingSessions(null, sessionsDir)) ?? IDLE_STATE;
}

async function bootFromTranscript(getEntries: () => readonly TranscriptEntry[]): Promise<void> {
  if (booting) return;
  booting = true;
  try {
    replaceState(await loadQuestState(getEntries()));
    bootstrapped = true;
  } catch {
    // Stay on current state; turn_start retries, injection stays IDLE-safe.
  } finally {
    booting = false;
  }
}

function loadFromTranscript(getEntries: () => readonly TranscriptEntry[]): void {
  void bootFromTranscript(getEntries);
}

function refreshStatus(ctx: PiCtx): void {
  try {
    const state = getState();
    ctx.ui.setStatus("pi-quest", state.qid === null ? undefined : `${state.qid} ${state.phase}`);
  } catch {
    // Status bar is best-effort.
  }
}

export function installDurability(pi: Pi): void {
  onSessionStart(pi, (_event, ctx) => {
    loadFromTranscript(() => ctx.sessionManager.getEntries());
    refreshStatus(ctx);
  });
  onTurnStart(pi, (_event, ctx) => {
    if (!bootstrapped) loadFromTranscript(() => ctx.sessionManager.getEntries());
  });
  onTurnEnd(pi, (_event, ctx) => {
    try {
      const reduced = reduce(getState(), { type: "TurnEnded" });
      replaceState(reduced.state);
      interpret(reduced.effects, ports(pi, ctx));
      refreshStatus(ctx);
    } catch {
      // Emit is best-effort; the pending flag keeps the data for next turn.
    }
  });
  onBeforeCompact(pi, (_event, ctx) => {
    try {
      interpret([{ kind: "EmitSnapshot" }], ports(pi, ctx));
    } catch {
      // Compaction proceeds; the snapshot is retried next turn.
    }
  });
  injectQuestContext(pi);
}
