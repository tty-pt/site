// HIGH_LEVEL: #working together
// HIGH_LEVEL: #sub-quests — stack, links, returns, completion gating.
// SPEC: B1.10.
import { getState, updateState } from "../app/store";
import { sendSteer } from "../app/interpreter";
import { recordRefinement } from "../domain/quest";
import { onUserMessage, type Pi } from "../hooks/events";
import { triageInQuestRequest } from "./scope";
import { classifyUserMessage } from "./triage";

export function installSubQuests(pi: Pi): void {
  onUserMessage(pi, (text) => {
    try {
      const state = getState();
      if (state.qid === null || state.phase === "idle" || state.phase === "archived") return;
      const trimmed = text.trim();
      if (trimmed.startsWith("/") || trimmed.length < 20) return;
      const triaged = triageInQuestRequest(state.qid, classifyUserMessage(trimmed));
      if (triaged === null) return;
      updateState((s) => recordRefinement(s, trimmed));
      sendSteer(pi, triaged.steer);
    } catch {
      // Passive handler: never break the agent.
    }
  });
}
