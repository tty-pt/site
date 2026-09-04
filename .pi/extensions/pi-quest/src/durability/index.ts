// HIGH_LEVEL: #surviving
// HIGH_LEVEL: #durability — snapshot store + reconstruct.
// SPEC: B1.6, snapshot contract §4.1.
import type { ExtensionAPI } from "../index.ts";

export function installDurability(_pi: ExtensionAPI): void {
  // S1: snapshot codec, emit, reconstruct, and per-turn injection.
}
