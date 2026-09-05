import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths";
import { maybeBootDraftReview } from "../../src/drafting/reviews.ts";
import { cancelReview, hasInFlight } from "../../src/review/tracker.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

const QID = "abc123" as Qid;

function thinDraft(cwd: string): void {
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, draftPath(QID)), "## Requirements\n- one thing\n");
}

Deno.test("thin drafts boot a review without thresholds", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-thin-"));
  thinDraft(cwd);
  const base = createDraft(createQuest("work", QID), "work");
  replaceState({ ...base, draft: { ...base.draft!, planAuthored: true } });
  await maybeBootDraftReview(fakePi(), fakeCtx(cwd));
  await new Promise((r) => setTimeout(r, 50));
  check(hasInFlight(QID), "every mutation boots, mature or not");
  cancelReview(QID);
  replaceState(IDLE_STATE);
});
