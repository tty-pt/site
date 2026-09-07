// HIGH_LEVEL: #validation — the quest doc's ## Status section is the in-file
// completion signal; implementing update-state and demotes keep it honest.
// Shared file I/O for the quest document (domain namespaces the pure string
// parsing); best-effort by design — a failed status write never breaks the
// agent or blocks a transition already taken in state.
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { updateState } from "./app/store";
import { hashContent } from "./drafting/reviews";
import { draftPath } from "./domain/paths";
import { upsertDocStatus } from "./domain/quest-doc";
import type { Qid } from "./domain/qid";
import type { PiCtx } from "./hooks/events";

export async function readQuestDoc(ctx: PiCtx, qid: Qid): Promise<string | null> {
  try {
    return await readFile(join(ctx.cwd, draftPath(qid)), "utf8");
  } catch {
    return null;
  }
}

// Write (or refresh) the quest doc's ## Status section and rebaseline the
// content binding to the new disk hash, so the implementation fingerprint
// and the file stay in sync. Never throws; missing docs are left alone.
export async function setDocStatus(
  ctx: PiCtx,
  qid: Qid,
  phase: string,
  complete: boolean,
): Promise<void> {
  const content = await readQuestDoc(ctx, qid);
  if (content === null) return;
  const updated = upsertDocStatus(content, phase, complete);
  if (updated === content) return;
  try {
    await writeFile(join(ctx.cwd, draftPath(qid)), updated, "utf8");
  } catch {
    return;
  }
  const hash = hashContent(updated);
  updateState((s) =>
    s.draft === null
      ? s
      : { ...s, draft: { ...s.draft, contentHash: hash }, snapshotPending: true }
  );
}