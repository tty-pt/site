// HIGH_LEVEL: #commands — list all quests with their states and the active marker.
import { readdir } from "node:fs/promises";
import { join } from "node:path";
import { getState } from "../../app/store";
import { ARCHIVE_DIR, CURRENT_DIR, FUTURE_DIR } from "../../domain/paths";
import { decodeSnapshot } from "../../durability/snapshots";
import type { PiCtx } from "../../hooks/events";
import type { QuestState } from "../../domain/quest";

async function listDir(cwd: string, dir: string, stripExt: string): Promise<string[]> {
  try {
    const files = await readdir(join(cwd, dir));
    return files.map((f) => f.endsWith(stripExt) ? f.slice(0, -stripExt.length) : f).filter((f) => f.length > 0);
  } catch {
    return [];
  }
}

export async function questRows(ctx: PiCtx): Promise<string[]> {
  const active = getState();
  const rows: string[] = [];
  const seen = new Set<string>();
  const entries = ctx.sessionManager.getEntries();
  const branch: QuestState[] = [];
  for (const entry of entries) {
    if (entry.customType !== "quest_journal") continue;
    const state = decodeSnapshot(entry.data);
    if (state?.qid && !seen.has(state.qid)) {
      seen.add(state.qid);
      branch.push(state);
    }
  }
  const futures = await listDir(ctx.cwd, FUTURE_DIR, ".md");
  const currents = await listDir(ctx.cwd, CURRENT_DIR, "");
  const archives = await listDir(ctx.cwd, ARCHIVE_DIR, ".zip");
  const mark = (qid: string) => active.qid === qid ? "  ◀ active" : "";
  for (const state of branch) {
    if (state.qid) rows.push(`  ${state.qid} — ${state.phase}${mark(state.qid)}`);
  }
  for (const qid of futures) {
    if (!seen.has(qid)) rows.push(`  ${qid} — drafting (draft file)${mark(qid)}`);
  }
  for (const qid of currents) {
    if (!seen.has(qid)) rows.push(`  ${qid} — current/${mark(qid)}`);
  }
  for (const qid of archives) {
    rows.push(`  ${qid} — archived`);
  }
  return rows.length > 0 ? rows : ["  (none)"];
}

export async function listQuests(ctx: PiCtx): Promise<string[]> {
  const active = getState();
  return [
    `Active: ${active.qid ? `${active.qid} — ${active.phase}` : "(none)"}`,
    "",
    "Quests:",
    ...await questRows(ctx),
  ];
}
