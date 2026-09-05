import { check } from "../check.ts";
import { mkdirSync, writeFileSync, mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createQuest } from "../../src/domain/quest.ts";
import { loadQuestState } from "../../src/durability/index.ts";
import { scanSiblingSessions } from "../../src/durability/siblings.ts";
import { encodeSnapshot, SNAPSHOT_TYPE } from "../../src/durability/snapshots.ts";

function fixture(): string {
  const root = mkdtempSync(join(tmpdir(), "pi-quest-siblings-"));
  mkdirSync(join(root, "proj"), { recursive: true });
  const lines = [
    JSON.stringify({ type: "user", text: "hi" }),
    "not json at all",
    JSON.stringify({ customType: "other", data: {} }),
    JSON.stringify({ customType: SNAPSHOT_TYPE, data: encodeSnapshot(createQuest("old work", "old001")) }),
    JSON.stringify({ customType: SNAPSHOT_TYPE, data: encodeSnapshot(createQuest("new work", "new002")) }),
  ];
  writeFileSync(join(root, "proj", "sess.jsonl"), lines.join("\n"));
  return root;
}

Deno.test("sibling scan finds the newest quest snapshot", async () => {
  const root = fixture();
  const any = await scanSiblingSessions(null, root);
  check(any?.qid === "new002", "newest wins");
  const one = await scanSiblingSessions("old001", root);
  check(one?.qid === "old001", "qid filter applies");
  const missing = await scanSiblingSessions("zzz999", root);
  check(missing === null, "unknown qid finds nothing");
  const nowhere = await scanSiblingSessions(null, join(root, "absent"));
  check(nowhere === null, "missing store returns null");
});

Deno.test("boot prefers the branch, then siblings, then cold start", async () => {
  const root = fixture();
  const branchHit = encodeSnapshot(createQuest("branch work", "brn003"));
  const branch = await loadQuestState([{ customType: SNAPSHOT_TYPE, data: branchHit }], root);
  check(branch.qid === "brn003", "branch wins without touching siblings");
  const sibling = await loadQuestState([], root);
  check(sibling.qid === "new002", "empty branch falls back to siblings");
  const cold = await loadQuestState([], join(root, "absent"));
  check(cold.phase === "idle" && cold.qid === null, "nothing anywhere cold-starts");
});
