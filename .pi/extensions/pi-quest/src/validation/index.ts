// HIGH_LEVEL: #modes
// HIGH_LEVEL: #validation — validator archives on PASS, demotes on FAIL.
// SPEC: B1.5 (completion + slim archive).
import { onSessionStart, onTurnEnd, onUserMessage, type Pi } from "../hooks/events";
import { ensureValidationFlow, handleConfirmInput } from "./flow";

export function installValidation(pi: Pi): void {
  onTurnEnd(pi, (_event, ctx) => {
    void ensureValidationFlow(pi, ctx);
  });
  onSessionStart(pi, (_event, ctx) => {
    void ensureValidationFlow(pi, ctx);
  });
  onUserMessage(pi, (text, ctx) => {
    handleConfirmInput(pi, ctx, text);
  });
}
