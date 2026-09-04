// HIGH_LEVEL: #tools (main agent) — quest_recover.
// HIGH_LEVEL: #surviving — recovery across sessions is pi-quest's own scan.
import { homedir } from "node:os";
import { readdir, readFile, stat } from "node:fs/promises";
import { join } from "node:path";
import { getState, replaceState } from "../../app/store";
import { IDLE_STATE } from "../../domain/quest";
import { decodeSnapshot, newestSnapshot, newestSnapshotFor } from "../../durability/snapshots";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { textResult } from "./reply";

const SIBLING_SCAN_FILES = 60;
const SIBLING_SCAN_TAIL_LINES = 400;

function snapshotFromLine(line: string, qid: string | null): ReturnType<typeof decodeSnapshot> {
  let parsed: unknown;
  try {
    parsed = JSON.parse(line);
  } catch {
    return null;
  }
  if (typeof parsed !== "object" || parsed === null) return null;
  const record = parsed as Record<string, unknown>;
  if (record["customType"] !== "quest_journal") return null;
  const state = decodeSnapshot(record["data"]);
  if (state === null || state.qid === null) return null;
  if (qid !== null && state.qid !== qid) return null;
  return state;
}

async function scanSiblingSessions(qid: string | null): Promise<ReturnType<typeof decodeSnapshot>> {
  let base: string;
  try {
    base = join(homedir(), ".pi", "agent", "sessions");
    const projects = await readdir(base);
    const files: Array<{ path: string; mtime: number }> = [];
    for (const project of projects) {
      let entries: string[];
      try {
        entries = await readdir(join(base, project));
      } catch {
        continue;
      }
      for (const name of entries) {
        if (!name.endsWith(".jsonl")) continue;
        const path = join(base, project, name);
        try {
          const info = await stat(path);
          files.push({ path, mtime: info.mtimeMs });
        } catch {
          // Unreadable files are skipped.
        }
      }
    }
    files.sort((a, b) => b.mtime - a.mtime);
    for (const file of files.slice(0, SIBLING_SCAN_FILES)) {
      let text: string;
      try {
        text = await readFile(file.path, "utf8");
      } catch {
        continue;
      }
      const lines = text.split("\n").filter((l) => l.trim() !== "").slice(-SIBLING_SCAN_TAIL_LINES);
      for (let i = lines.length - 1; i >= 0; i -= 1) {
        const state = snapshotFromLine(lines[i], qid);
        if (state !== null) return state;
      }
    }
  } catch {
    // No session store reachable — recovery falls through to cold start.
  }
  return null;
}

export async function recoverQuest(ctx: PiCtx, qid: string | null): Promise<{ qid: string | null; source: string }> {
  const entries = ctx.sessionManager.getEntries();
  const target = qid ?? getState().qid;
  if (target !== null) {
    const onBranch = newestSnapshotFor(entries, target);
    if (onBranch !== null) {
      replaceState({ ...onBranch, snapshotPending: false });
      return { qid: onBranch.qid, source: "transcript" };
    }
    const sibling = await scanSiblingSessions(target);
    if (sibling !== null) {
      replaceState({ ...sibling, snapshotPending: false });
      return { qid: sibling.qid, source: "sibling session" };
    }
    return { qid: null, source: `no snapshot for ${target}` };
  }
  const newest = newestSnapshot(entries) ?? await scanSiblingSessions(null);
  if (newest?.qid) {
    replaceState({ ...newest, snapshotPending: false });
    return { qid: newest.qid, source: "transcript" };
  }
  replaceState(IDLE_STATE);
  return { qid: null, source: "cold start" };
}

export function recoverTool(_pi: Pi): PiToolSpec {
  return {
    name: "quest_recover",
    label: "Recover Quest",
    description: "Rebuild quest state from the transcript, including earlier sessions. Runs automatically when state is absent; callable directly.",
    parameters: {
      type: "object",
      properties: {
        qid: { type: "string", description: "Quest id to recover. Defaults to the active quest, then newest." },
      },
      additionalProperties: false,
    },
    execute: async (_toolCallId, params, _signal, _onUpdate, ctx) => {
      const qid = typeof params["qid"] === "string" ? params["qid"] as string : null;
      const done = await recoverQuest(ctx, qid);
      if (done.qid) return textResult(`Recovered quest ${done.qid} from ${done.source}.`, { qid: done.qid, source: done.source });
      return textResult(`Recovery found nothing (${done.source}).`, { source: done.source });
    },
  };
}
