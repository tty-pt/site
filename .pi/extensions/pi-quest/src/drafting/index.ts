// HIGH_LEVEL: #modes
// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// SPEC: B1.2-B1.3, B2 (gate table), B2.1 (draft-file exemption).
import type { Pi } from "../hooks/events";
import { watchNewRequests } from "./detect";
import { installDraftGate } from "./gate";
import { watchDraftEdits, watchGoInput } from "./reviews";

export function installDrafting(pi: Pi): void {
  installDraftGate(pi);
  watchNewRequests(pi);
  watchDraftEdits(pi);
  watchGoInput(pi);
}
