// HIGH_LEVEL: #modes
// HIGH_LEVEL: #implementing — unrestricted by construction.
// SPEC: B1.4, B1.7 (advisory setbacks), B1.8 (amendments).
import { onSetback } from "../hooks/events";
import { recordAdvisoryNote } from "../domain/quest";
import type { ExtensionAPI } from "../index.ts";

export function installImplementing(pi: ExtensionAPI): void {
  onSetback(pi, recordAdvisoryNote);
}
