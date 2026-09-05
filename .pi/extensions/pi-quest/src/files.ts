// HIGH_LEVEL: #storage — qid in all paths; known ids seed monotonic assignment.
// HIGH_LEVEL: #storage — the drafting workspace starts from a template.
import { mkdir, readdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import type { PiCtx } from "./hooks/events";
import { CURRENT_DIR, draftPath, FUTURE_DIR } from "./domain/paths";
import { isQid, type Qid } from "./domain/qid";
import { renderDraftTemplate } from "./views/draft-template";

export async function listKnownQids(cwd: string): Promise<string[]> {
  const qids = new Set<string>();
  try {
    const futures = await readdir(join(cwd, FUTURE_DIR));
    for (const f of futures) {
      if (f.endsWith(".md")) qids.add(f.slice(0, -3));
    }
  } catch {
    // Missing workspace is fine — no known ids.
  }
  try {
    const currents = await readdir(join(cwd, CURRENT_DIR), { withFileTypes: true });
    for (const entry of currents) {
      if (entry.isDirectory()) qids.add(entry.name);
    }
  } catch {
    // Missing workspace is fine — no known ids.
  }
  return [...qids];
}

// Write the draft scaffold for a quest unless one already exists —
// never clobbers agent-authored content. Called at every provisioning
// site so future/<qid>.md exists from the moment the quest starts.
export async function ensureDraftFile(ctx: PiCtx, rawQid: string, name: string, objective: string): Promise<void> {
  if (!isQid(rawQid)) throw new Error(`invalid qid: ${rawQid}`);
  const qid: Qid = rawQid;
  await mkdir(join(ctx.cwd, FUTURE_DIR), { recursive: true });
  try {
    await writeFile(join(ctx.cwd, draftPath(qid)), renderDraftTemplate(name, objective), { flag: "wx" });
  } catch (err) {
    if ((err as { code?: string }).code !== "EEXIST") throw err;
  }
}
