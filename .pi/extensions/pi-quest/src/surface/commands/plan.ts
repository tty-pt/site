// HIGH_LEVEL: #commands — F2 opens the floating plan viewer.
// HIGH_LEVEL: #surface — on-demand inspection, zero inference.
// SPEC: B1.3 (draft stays inspectable), B1.8 (amendments stay inspectable).
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { getState } from "../../app/store";
import { draftPath } from "../../domain/paths";
import type { PiCtx } from "../../hooks/events";
import { openPlanViewer, OVERLAY_FRAME } from "../../views/plan-overlay";

const TOAST_BODY_MAX = 800;

function notify(ctx: PiCtx, text: string): void {
  try {
    ctx.ui.notify(text, "info");
  } catch {
    // Notification is best-effort.
  }
}

export async function viewActivePlan(ctx: PiCtx): Promise<void> {
  const state = getState();
  if (state.qid === null) {
    notify(ctx, "No active quest.");
    return;
  }
  const qid = state.qid;
  const file = draftPath(qid);
  let content: string;
  try {
    content = await readFile(join(ctx.cwd, file), "utf8");
  } catch {
    notify(ctx, `Quest ${qid} has no draft file yet (${file}).`);
    return;
  }
  const title = `Quest ${qid} — plan (${file})`;
  if (ctx.mode !== "tui" || typeof ctx.ui.custom !== "function") {
    const excerpt = content.slice(0, TOAST_BODY_MAX);
    notify(ctx, `${title}\n${excerpt}`);
    return;
  }
  try {
    const cols = process.stdout.columns ?? 120;
    const rows = process.stdout.rows ?? 40;
    // Full-size box (cols − 2 × rows − 2, even offsets), nudged one cell
    // inside both axes so a 1-cell margin frames it on all sides. The list
    // body is budgeted so the worst-case frame (body + OVERLAY_FRAME chrome)
    // lands exactly at maxHeight: pi only trims an overlay above that bound,
    // so the bottom border can never be amputated.
    const width = cols - 2;
    const rowBudget = Math.max(OVERLAY_FRAME + 1, rows - 2);
    const visible = Math.max(1, rowBudget - OVERLAY_FRAME);
    await ctx.ui.custom<void>(async (_tui, theme, _kb, done) => {
      return openPlanViewer(title, content, theme, () => done(), { visible, width });
    }, {
      overlay: true,
      overlayOptions: { width, maxHeight: rowBudget, anchor: "top-left", offsetX: 1, offsetY: 1 },
    });
  } catch {
    // The viewer is best-effort; the draft file remains the source.
  }
}
