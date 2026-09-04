// HIGH_LEVEL: #modes
// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// SPEC: B1.2-B1.3, B2 (gate table), B2.1 (draft-file exemption).
import type { ExtensionAPI } from "../index.ts";

export function installDrafting(_pi: ExtensionAPI): void {
  // S1 wires the gate + exemption; S2 wires edit detection + review loop.
}
