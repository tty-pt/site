// HIGH_LEVEL: #surviving — recovery across sessions is pi-quest's own scan.
// Reads transcript JSONL only: newest-first, bounded, stops at first match.
// No compaction logic and no vcc contact — session files are append-only input.
import { homedir } from "node:os";
import { readdir, readFile, stat } from "node:fs/promises";
import { join } from "node:path";
import { decodeSnapshot, SNAPSHOT_TYPE } from "./snapshots";
import type { QuestState } from "../domain/quest";

const SCAN_FILES = 60;
const SCAN_TAIL_LINES = 400;

function snapshotFromLine(line: string, qid: string | null): QuestState | null {
  let parsed: unknown;
  try {
    parsed = JSON.parse(line);
  } catch {
    return null;
  }
  if (typeof parsed !== "object" || parsed === null) return null;
  const record = parsed as Record<string, unknown>;
  if (record["customType"] !== SNAPSHOT_TYPE) return null;
  const state = decodeSnapshot(record["data"]);
  if (state === null || state.qid === null) return null;
  if (qid !== null && state.qid !== qid) return null;
  return state;
}

async function sessionFiles(sessionsDir: string): Promise<Array<{ path: string; mtime: number }>> {
  const files: Array<{ path: string; mtime: number }> = [];
  const projects = await readdir(sessionsDir);
  for (const project of projects) {
    let entries: string[];
    try {
      entries = await readdir(join(sessionsDir, project));
    } catch {
      continue;
    }
    for (const name of entries) {
      if (!name.endsWith(".jsonl")) continue;
      const path = join(sessionsDir, project, name);
      try {
        const info = await stat(path);
        files.push({ path, mtime: info.mtimeMs });
      } catch {
        // Unreadable files are skipped.
      }
    }
  }
  files.sort((a, b) => b.mtime - a.mtime);
  return files.slice(0, SCAN_FILES);
}

export async function scanSiblingSessions(
  qid: string | null,
  sessionsDir: string = join(homedir(), ".pi", "agent", "sessions"),
): Promise<QuestState | null> {
  let files: Array<{ path: string; mtime: number }>;
  try {
    files = await sessionFiles(sessionsDir);
  } catch {
    return null;
  }
  for (const file of files) {
    let text: string;
    try {
      text = await readFile(file.path, "utf8");
    } catch {
      continue;
    }
    const lines = text.split("\n").filter((l) => l.trim() !== "").slice(-SCAN_TAIL_LINES);
    for (let i = lines.length - 1; i >= 0; i -= 1) {
      const state = snapshotFromLine(lines[i], qid);
      if (state !== null) return state;
    }
  }
  return null;
}
