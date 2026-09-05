// HIGH_LEVEL: #working together
// HIGH_LEVEL: #human absence — ask-with-default + timeout race.
// SPEC: B1.9.
import { readQuestConfig } from "../config";
import { onSessionStart, onUserMessage, type Pi } from "../hooks/events";
import { noteLateAnswer } from "./ask";
import { installObservedQuestions, refreshAskingTools } from "./observed";

export function installHumanAbsence(pi: Pi): void {
  onSessionStart(pi, (_event, ctx) => {
    readQuestConfig(ctx.cwd).then(refreshAskingTools, () => {});
  });
  onUserMessage(pi, (text) => {
    noteLateAnswer(pi, text);
  });
  installObservedQuestions(pi);
}
