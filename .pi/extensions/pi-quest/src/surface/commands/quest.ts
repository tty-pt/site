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

export async function listDraftQids(cwd: string): Promise<string[]> {
  try {
    const files = await readdir(join(cwd, FUTURE_DIR));
    return files.filter((f) => f.endsWith(".md")).map((f) => f.replace(/\.md$/, ""));
  } catch {
    // Directory scan is best-effort.
    return [];
  }
}

export async function readDraftObjective(cwd: string, qid: string): Promise<string | null> {
  try {
    const text = await readFile(join(cwd, FUTURE_DIR, `${qid}.md`), "utf8");
    return text.match(/^##\s+Original request\s*\n+(.+?)(?:\n##\s|\n*$)/ims)?.[1]?.trim() ||
      text.split("\n")[0] ||
      qid;
  } catch {
    return null;
  }
}

async function adoptDraftFile(pi: Pi, ctx: PiCtx, qid: string): Promise<string> {
  const objective = await readDraftObjective(ctx.cwd, qid);
  if (objective === null) return `No quest ${qid}: no snapshot and no draft file.`;
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
  const drafts = await listDraftQids(ctx.cwd);
  const match = drafts.find((qid) => qid === arg);
  if (match !== undefined) return adoptDraftFile(pi, ctx, match);
  const newest = newestSnapshot(entries);
  if (newest?.name === arg && newest.qid) {
    replaceState(newest);
    return `Resumed quest ${newest.qid}: ${summarizeActive()}`;
  }
  return `No quest matching "${arg}".`;
}
