// HIGH_LEVEL: #working together — a new request during an active quest is
// interpreted in context. Borderline cases stay in context: recording is
// deterministic, routing stays agent judgment via the quest tools.
// SPEC: B1.2 scope rules (bias to keep).
import type { MessageKind } from "./triage";

export interface Triage {
  record: boolean;
  steer: string;
}

export function triageInQuestRequest(qid: string, kind: MessageKind): Triage | null {
  if (kind !== "refinement") return null;
  return {
    record: true,
    steer: `Recorded as a refinement on quest ${qid}; it will feed the validator at completion. ` +
      `If it changes the plan, record an amendment via quest_update_state; ` +
      `if it is separable work, spawn it with quest_subquest; ` +
      `if it is a separate quest instead, finish or archive this one first, then restate it.`,
  };
}
