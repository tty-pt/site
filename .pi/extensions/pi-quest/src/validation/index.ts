// HIGH_LEVEL: #modes
// HIGH_LEVEL: #validation — validator archives on PASS, demotes on FAIL.
// SPEC: B1.5 (completion + slim archive).
import { onCompletionClaim } from "../hooks/events";
import { validateThenArchiveOrDemote } from "../domain/quest";
import type { ExtensionAPI } from "../index.ts";

export function installValidation(pi: ExtensionAPI): void {
  onCompletionClaim(pi, validateThenArchiveOrDemote);
}
