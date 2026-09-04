// HIGH_LEVEL: #surviving — in-memory current state; reconstruct populates.
import { IDLE_STATE, type QuestState } from "../domain/quest";

let current: QuestState = IDLE_STATE;

export function getState(): QuestState {
  return current;
}

export function replaceState(next: QuestState): void {
  current = next;
}

export function updateState(fn: (state: QuestState) => QuestState): QuestState {
  current = fn(current);
  return current;
}
