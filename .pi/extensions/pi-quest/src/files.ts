// HIGH_LEVEL: #storage — qid in all paths; known ids seed monotonic assignment.
import { readdir } from "node:fs/promises";
import { join } from "node:path";
import { CURRENT_DIR, FUTURE_DIR } from "./domain/paths";

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
