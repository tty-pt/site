// HIGH_LEVEL: #working together
// HIGH_LEVEL: #sub-quests — stack, links, returns, completion gating.
// SPEC: B1.10.
import { onChildReturn } from "../hooks/events";
import { recordChildFindings } from "../domain/quest";
import type { ExtensionAPI } from "../index.ts";

export function installSubQuests(pi: ExtensionAPI): void {
  onChildReturn(pi, recordChildFindings);
}
