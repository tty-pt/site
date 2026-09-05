// HIGH_LEVEL: #interfaces — status bar names the active quest.
// Status text is derived, never stored; commands re-assert it after acting.
import { getState } from "../app/store";
import { DEFAULT_CONFIG, readQuestConfig, type StatusStyle } from "../config";
import type { Phase, QuestState } from "../domain/quest";
import type { PiCtx } from "../hooks/events";

const PHASE_ICONS: Record<Phase, string> = {
  idle: "💤",
  provisional: "🔍",
  drafting: "📝",
  implementing: "🔨",
  validating: "🧪",
  archived: "📦",
};

export function questStatus(state: QuestState, style: StatusStyle = "icon"): string | undefined {
  if (state.qid === null) return undefined;
  if (style === "text") return `${state.phase} ${state.qid}`;
  return `${PHASE_ICONS[state.phase]} ${state.qid}`;
}

let statusStyle: StatusStyle = DEFAULT_CONFIG.statusStyle;

export async function refreshStyle(cwd: string): Promise<void> {
  try {
    statusStyle = (await readQuestConfig(cwd)).statusStyle;
  } catch {
    // Style falls back to default; the bar stays best-effort.
  }
}

export function refreshStatus(ctx: PiCtx): void {
  try {
    ctx.ui.setStatus("pi-quest", questStatus(getState(), statusStyle));
  } catch {
    // Status bar is best-effort.
  }
}
