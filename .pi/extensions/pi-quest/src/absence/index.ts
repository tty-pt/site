// HIGH_LEVEL: #working together
// HIGH_LEVEL: #human absence — ask-with-default + timeout race.
// SPEC: B1.9.
import { onUserMessage, type Pi } from "../hooks/events";
import { noteLateAnswer } from "./ask";
import { installObservedQuestions } from "./observed";

export function installHumanAbsence(pi: Pi): void {
  onUserMessage(pi, (text) => {
    noteLateAnswer(pi, text);
  });
  installObservedQuestions(pi);
}
