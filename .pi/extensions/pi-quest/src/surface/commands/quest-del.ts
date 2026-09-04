// HIGH_LEVEL: #commands — archive (kill) the current or named quest.
import { getState } from "../../app/store";
import type { Pi, PiCtx } from "../../hooks/events";
import { archiveActiveQuest } from "../tools/archive";
import { resumeQuest, summarizeActive } from "./quest";

export async function killQuest(pi: Pi, ctx: PiCtx, rawArg: string): Promise<string> {
  const arg = rawArg.trim();
  if (arg !== "") {
    const active = getState();
    if (active.qid !== arg && active.name !== arg) {
      const resumed = await resumeQuest(pi, ctx, arg);
      if (getState().qid === null) return resumed;
    }
  }
  const state = getState();
  if (state.qid === null) return "No active quest to archive.";
  try {
    const done = await archiveActiveQuest(pi, ctx, "ABANDONED", "Killed via /quest-del.");
    return `Quest ${done.archivedQid} archived (abandoned, ${done.zipPath}). ${summarizeActive()}`;
  } catch (err) {
    const detail = err instanceof Error ? err.message : String(err);
    return `Archive failed: ${detail}`;
  }
}
