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

function draftFile(cwd: string): void {
  const content = [
    "## Requirements",
    "- first requirement",
    "- second requirement",
    "",
    "## Implementation Plan",
    "Do the work in order.",
    "",
  ].join("\n");
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, draftPath(QID)), content);
}

function childState(deviated: boolean) {
  const base = createDraft(createQuest("child work", QID, "par001" as Qid), "child");
  const drafting = { ...base, draft: { ...base.draft!, planAuthored: true } };
  if (!deviated) return drafting;
  return { ...drafting, refinements: ["parent asked for retries"] };
}

Deno.test("clean children skip the draft reviewer", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-clean-child-"));
  draftFile(cwd);
  replaceState(childState(false));
  await maybeBootDraftReview(fakePi(), fakeCtx(cwd));
  check(!hasInFlight(QID), "no review booted for a clean child");
  replaceState(IDLE_STATE);
});

Deno.test("deviated children take the full review loop", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-dev-child-"));
  draftFile(cwd);
  replaceState(childState(true));
  await maybeBootDraftReview(fakePi(), fakeCtx(cwd));
  check(hasInFlight(QID), "deviation boots a review");
  cancelReview(QID);
  replaceState(IDLE_STATE);
});

Deno.test("root quests always boot when thresholds pass", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-root-"));
  draftFile(cwd);
  const base = createDraft(createQuest("root work", QID), "root");
  replaceState({ ...base, draft: { ...base.draft!, planAuthored: true } });
  await maybeBootDraftReview(fakePi(), fakeCtx(cwd));
  check(hasInFlight(QID), "root boots regardless of deviation");
  cancelReview(QID);
  replaceState(IDLE_STATE);
});
