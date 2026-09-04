// HIGH_LEVEL: #surviving
// HIGH_LEVEL: #durability — snapshot store + reconstruct.
// SPEC: B1.6, snapshot contract §4.1.
import { onSessionStart, onTurnBoundary } from "../hooks/events";
import { injectQuestContext } from "./injection";
import { emitSnapshot, reconstruct } from "./snapshots";
import type { ExtensionAPI } from "../index.ts";

export function installDurability(pi: ExtensionAPI): void {
  onSessionStart(pi, reconstruct);
  onTurnBoundary(pi, emitSnapshot);
  injectQuestContext(pi);
}
