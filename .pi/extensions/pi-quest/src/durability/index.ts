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
import { refreshStatus, refreshStyle } from "./status";
import { IDLE_STATE, type QuestState } from "../domain/quest";
import { SNAPSHOT_TYPE, reconstruct } from "./snapshots";

export { noteDraftUpdated, questStatus, refreshStatus, stopBlink } from "./status";

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
  _sessionsDir?: string,
): Promise<QuestState> {
  // Fresh sessions start idle: no auto-adopt. The user starts a new quest,
  // asks inference to recover one, or picks via /quest.
  return reconstruct(entries);
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

export function installDurability(pi: Pi): void {
  onSessionStart(pi, (_event, ctx) => {
    loadFromTranscript(() => ctx.sessionManager.getEntries());
    void refreshStyle(ctx.cwd);
    refreshStatus(ctx);
  });
  onTurnStart(pi, (_event, ctx) => {
    if (!bootstrapped) loadFromTranscript(() => ctx.sessionManager.getEntries());
    void refreshStyle(ctx.cwd);
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
