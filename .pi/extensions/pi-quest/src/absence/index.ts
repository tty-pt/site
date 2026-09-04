// HIGH_LEVEL: #working together
// HIGH_LEVEL: #human absence — ask-with-default + timeout race.
// SPEC: B1.9.
import { onUserMessage } from "../hooks/events";
import { applyLateAnswer } from "../domain/quest";
import type { ExtensionAPI } from "../index.ts";

export function installHumanAbsence(pi: ExtensionAPI): void {
  onUserMessage(pi, applyLateAnswer);
}
