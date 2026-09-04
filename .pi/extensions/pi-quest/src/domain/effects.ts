// HIGH_LEVEL: #surviving
// HIGH_LEVEL: #drafting
// HIGH_LEVEL: #modes
// HIGH_LEVEL: #validation
// HIGH_LEVEL: #working together
// HIGH_LEVEL: #storage
// SPEC: closed effect set, REBUILD_PLAN §5 — a tenth effect is a design review.
import type { Qid } from "./qid";
import type { ApprovedBy, ArchivedOutcome, QuestState, ReviewKind } from "./quest";

export type Effect =
  | { kind: "EmitSnapshot" }
  | { kind: "Steer"; text: string }
  | { kind: "NotifyUI"; text: string }
  | { kind: "LaunchReview"; qid: Qid; review: ReviewKind; target: string }
  | { kind: "CancelReview"; qid: Qid }
  | { kind: "Promote"; qid: Qid; approvedBy: ApprovedBy }
  | { kind: "Demote"; qid: Qid }
  | { kind: "Archive"; qid: Qid; outcome: ArchivedOutcome }
  | { kind: "Render"; qid: Qid };

export type QuestEvent =
  | { type: "SessionStarted" }
  | { type: "TurnStarted" }
  | { type: "TurnEnded" }
  | { type: "StateMutated" };

export interface Reduced {
  state: QuestState;
  effects: Effect[];
}

export function reduce(state: QuestState, event: QuestEvent): Reduced {
  switch (event.type) {
    case "SessionStarted":
    case "TurnStarted":
      return { state, effects: [] };
    case "TurnEnded":
    case "StateMutated":
      if (!state.snapshotPending) return { state, effects: [] };
      return { state, effects: [{ kind: "EmitSnapshot" }] };
  }
}
