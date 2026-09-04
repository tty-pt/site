// HIGH_LEVEL: #surviving — executes effects (A1). ONLY impure choke point besides adapters.
import type { Effect } from "../domain/effects";
import { encodeSnapshot, SNAPSHOT_TYPE, type Snapshot } from "../durability/snapshots";
import type { Pi } from "../hooks/events";
import { getState, replaceState } from "./store";

export interface Ports {
  saveSnapshot(snapshot: Snapshot): void;
  sendSteer(text: string): void;
  notify(text: string): void;
}

export function sendSteer(pi: Pi, text: string): void {
  try {
    pi.sendMessage({ customType: SNAPSHOT_TYPE, content: text }, { deliverAs: "steer" });
  } catch {
    // Notification is best-effort; quest state is already persisted.
  }
}

export function emitNow(pi: Pi): void {
  try {
    interpret([{ kind: "EmitSnapshot" }], {
      saveSnapshot: (snapshot) => {
        pi.appendEntry(SNAPSHOT_TYPE, snapshot);
      },
      sendSteer: () => {},
      notify: () => {},
    });
  } catch {
    // Emit is best-effort; the pending flag retries at the next turn.
  }
}

export function interpret(effects: readonly Effect[], ports: Ports): void {
  for (const effect of effects) {
    switch (effect.kind) {
      case "EmitSnapshot": {
        ports.saveSnapshot(encodeSnapshot(getState()));
        const state = getState();
        replaceState({ ...state, snapshotPending: false });
        break;
      }
      case "Steer": {
        ports.sendSteer(effect.text);
        break;
      }
      case "NotifyUI": {
        ports.notify(effect.text);
        break;
      }
      default:
        break;
    }
  }
}
