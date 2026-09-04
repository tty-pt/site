// HIGH_LEVEL: #modes
// HIGH_LEVEL: #validation — validator archives on PASS, demotes on FAIL.
// SPEC: B1.5 (completion + slim archive).
import type { ExtensionAPI } from "../index.ts";

export function installValidation(_pi: ExtensionAPI): void {
  // S3: completion claim runs the validator here.
}
