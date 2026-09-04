// HIGH_LEVEL: #modes
// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// SPEC: B1.2-B1.3, B2 (gate table), B2.1 (draft-file exemption).
import { installDraftGate } from "./gate";
import { onDraftEdit } from "../hooks/events";
import { supersedeReviewThenBootFresh } from "../review/tracker";
import type { ExtensionAPI } from "../index.ts";

export function installDrafting(pi: ExtensionAPI): void {
  installDraftGate(pi);
  onDraftEdit(pi, supersedeReviewThenBootFresh);
}
