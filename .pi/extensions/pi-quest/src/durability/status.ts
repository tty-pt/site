// HIGH_LEVEL: #interfaces — status bar names the active quest, [F2] always shown dim, flashing bright on draft updates.
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
    ctx.ui.setStatus("pi-quest", renderStatus(getState(), statusStyle));
  } catch {
    // Status bar is best-effort.
  }
}

const PLAN_HINT = "[^Q]";
const BLINK_INTERVAL_MS = 500;
const BLINK_WINDOW_MS = 6000;

// SGR styling only (\x1b escapes, never raw control bytes): faint for the
// steady state like native footer text, bright white for blink-on frames.
// Both footers pass escapes through sanitize and truncate ANSI-aware.
const DIM = "\x1b[2m";
const BRIGHT = "\x1b[97m";
const RESET = "\x1b[0m";

export interface BlinkCadence {
  intervalMs: number;
  windowMs: number;
}

let brightPhase = false;
let blinkUi: PiCtx["ui"] | undefined;
let blinkInterval: ReturnType<typeof setInterval> | undefined;
let blinkStop: ReturnType<typeof setTimeout> | undefined;

function renderStatus(state: QuestState, style: StatusStyle): string | undefined {
  const base = questStatus(state, style);
  if (base === undefined) return base;
  const framed = `${base} ${PLAN_HINT}`;
  return brightPhase ? `${BRIGHT}${framed}${RESET}` : `${DIM}${framed}${RESET}`;
}

// Exported for test isolation: pending blink timers outlive a single test.
export function stopBlink(): void {
  if (blinkInterval !== undefined) clearInterval(blinkInterval);
  if (blinkStop !== undefined) clearTimeout(blinkStop);
  blinkInterval = undefined;
  blinkStop = undefined;
  brightPhase = false;
}

function assertStatus(ui: PiCtx["ui"]): void {
  ui.setStatus("pi-quest", renderStatus(getState(), statusStyle));
}

// Bright-white [F2] flash for one window after a content-changing draft
// save, relaxing back to dim. Rapid saves restart the window; timers
// always self-clear.
export function noteDraftUpdated(
  ctx: PiCtx,
  cadence: BlinkCadence = { intervalMs: BLINK_INTERVAL_MS, windowMs: BLINK_WINDOW_MS },
): void {
  try {
    stopBlink();
    blinkUi = ctx.ui;
    brightPhase = true;
    refreshStatus(ctx);
    blinkInterval = setInterval(() => {
      try {
        brightPhase = !brightPhase;
        if (blinkUi !== undefined) assertStatus(blinkUi);
      } catch {
        // Status bar is best-effort.
      }
    }, cadence.intervalMs);
    blinkStop = setTimeout(() => {
      const ui = blinkUi;
      stopBlink();
      if (ui === undefined) return;
      try {
        assertStatus(ui);
      } catch {
        // Status bar is best-effort.
      }
    }, cadence.windowMs);
  } catch {
    // Draft feedback is passive; never break the agent.
  }
}
