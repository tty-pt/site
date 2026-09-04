// HIGH_LEVEL: #modes
// HIGH_LEVEL: #implementing — unrestricted by construction.
// SPEC: B1.4, B1.7 (advisory setbacks), B1.8 (amendments).
import { getState, updateState } from "../app/store";
import { sendSteer } from "../app/interpreter";
import { recordAdvisoryNote } from "../domain/quest";
import { onToolResult, type Pi } from "../hooks/events";
import { detectSetback } from "./setbacks";

export function installImplementing(pi: Pi): void {
  onToolResult(pi, (event) => {
    try {
      if (getState().phase !== "implementing") return;
      const hit = detectSetback(event);
      if (!hit) return;
      updateState((s) => recordAdvisoryNote(s, hit.reason, hit.evidence));
      sendSteer(pi, `Setback recorded (advisory, nothing blocked): ${hit.reason}`);
    } catch {
      // Passive handler: never break the agent.
    }
  });
}
