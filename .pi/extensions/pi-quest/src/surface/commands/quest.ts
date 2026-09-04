// HIGH_LEVEL: #commands — resume a quest or drafting phase, or show the active quest.
import { readFile, readdir } from "node:fs/promises";
import { join } from "node:path";
import { getState, replaceState } from "../../app/store";
import { emitNow } from "../../app/interpreter";
import { createDraft, createQuest } from "../../domain/quest";
import { isQid } from "../../domain/qid";
import { FUTURE_DIR } from "../../domain/paths";
import { newestSnapshot, newestSnapshotFor } from "../../durability/snapshots";
import type { Pi, PiCtx } from "../../hooks/events";

export function summarizeActive(): string {
  const state = getState();
  if (state.qid === null) return "No active quest.";
  const kids = state.children.length > 0
    ? ` Children: ${state.children.map((c) => `${c.qid}=${c.status}`).join(", ")}.`
    : "";
  const review = state.activeReview ? ` Review ${state.activeReview.kind} running.` : "";
  return `Quest ${state.qid} — phase ${state.phase}.${review}${kids} ${state.exactNextAction}`;
}

async function adoptDraftFile(pi: Pi, ctx: PiCtx, qid: string): Promise<string> {
  let text: string;
  try {
    text = await readFile(join(ctx.cwd, FUTURE_DIR, `${qid}.md`), "utf8");
  } catch {
    return `No quest ${qid}: no snapshot and no draft file.`;
  }
  const objective = text.match(/^##\s+Original request\s*\n+(.+?)(?:\n##\s|\n*$)/ims)?.[1]?.trim() ||
    text.split("\n")[0] ||
    qid;
  replaceState(createDraft(createQuest(objective, qid), qid));
  emitNow(pi);
  return `Resumed draft ${qid}: ${summarizeActive()}`;
}

export async function resumeQuest(pi: Pi, ctx: PiCtx, rawArg: string): Promise<string> {
  const arg = rawArg.trim();
  if (arg === "") return summarizeActive();
  const entries = ctx.sessionManager.getEntries();
  if (isQid(arg)) {
    const hit = newestSnapshotFor(entries, arg);
    if (hit) {
      replaceState(hit);
      return `Resumed quest ${arg}: ${summarizeActive()}`;
    }
    return adoptDraftFile(pi, ctx, arg);
  }
  const active = getState();
  if (active.qid !== null && (active.name === arg || active.qid === arg)) {
    return summarizeActive();
  }
  try {
    const files = await readdir(join(ctx.cwd, FUTURE_DIR));
    const match = files.find((f) => f.replace(/\.md$/, "") === arg || f === `${arg}.md`);
    if (match) return adoptDraftFile(pi, ctx, match.replace(/\.md$/, ""));
  } catch {
    // Directory scan is best-effort.
  }
  const newest = newestSnapshot(entries);
  if (newest?.name === arg && newest.qid) {
    replaceState(newest);
    return `Resumed quest ${newest.qid}: ${summarizeActive()}`;
  }
  return `No quest matching "${arg}".`;
}
