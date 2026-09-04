import { check } from "../check.ts";
import { getState, replaceState } from "../../src/app/store.ts";
import { handleDraftEdit } from "../../src/drafting/edits.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { Qid } from "../../src/domain/qid.ts";

const QID = "abc123" as Qid;

function drafting() {
  return createDraft(createQuest("req", QID), "thing");
}

Deno.test("draft edit records the hash and flags pending", () => {
  replaceState(drafting());
  check(handleDraftEdit(draftPath(QID), "hash-1"), "changed");
  check(getState().draft?.contentHash === "hash-1", "hash recorded");
  check(getState().snapshotPending, "edit flags pending");
  replaceState(IDLE_STATE);
});

Deno.test("draft edit ignores identical saves and other paths", () => {
  replaceState(drafting());
  check(handleDraftEdit(draftPath(QID), "hash-1"), "first change");
  check(!handleDraftEdit(draftPath(QID), "hash-1"), "identical save ignored");
  check(!handleDraftEdit(".pi/quest/future/other.md", "hash-2"), "other path ignored");
  check(getState().draft?.contentHash === "hash-1", "hash kept");
  replaceState(IDLE_STATE);
});

Deno.test("draft edit without a quest is inert", () => {
  replaceState(IDLE_STATE);
  check(!handleDraftEdit(draftPath(QID), "hash-1"), "idle ignored");
  check(getState().phase === "idle", "idle untouched");
});
