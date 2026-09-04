import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createQuest } from "../../src/domain/quest.ts";
import { fakePi } from "../fake-pi.ts";
import { archiveQuestFiles, renderManifest, renderQuestView, writeViewFiles } from "../../src/views/quest-view.ts";

Deno.test("quest view renders state with a read-only header", () => {
  const view = renderQuestView({ ...createQuest("do it", "abc123"), exactNextAction: "go" });
  check(view.includes("abc123"), "qid rendered");
  check(view.includes("do it"), "objective rendered");
  check(view.includes("transcript is the truth"), "read-only header present");
  const manifest = JSON.parse(renderManifest(createQuest("do it", "abc123"), "COMPLETED", "done well"));
  check(manifest.outcome === "COMPLETED", "outcome recorded");
  check(manifest.summary === "done well", "summary recorded");
  check(typeof manifest.archivedAt === "string", "timestamp recorded");
});

Deno.test("view files and archives land on disk", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-views-"));
  const pi = fakePi();
  const state = createQuest("do it", "abc123");
  const { dir } = await writeViewFiles(cwd, state);
  const view = await readFile(join(dir, "quest.md"), "utf8");
  check(view.includes("abc123"), "view written");
  const zip = await archiveQuestFiles(pi, cwd, state, "FAILED", null);
  check(zip.endsWith("abc123.zip"), "zip path returned");
  check(pi.execCalls.length === 1 && pi.execCalls[0].command === "zip", "zip invoked");
  const manifestRaw = await readFile(join(dir, "manifest.json"), "utf8");
  check(JSON.parse(manifestRaw).outcome === "FAILED", "manifest written");
});
