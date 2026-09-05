import { check } from "../check.ts";
import { existsSync, mkdtempSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { createQuest } from "../../src/domain/quest.ts";
import { draftPath, questDir } from "../../src/domain/paths.ts";
import type { Qid } from "../../src/domain/qid.ts";
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
  await mkdir(join(cwd, ".pi/quest/future"), { recursive: true });
  await writeFile(join(cwd, draftPath("abc123" as Qid)), "draft stays until archive", "utf8");
  const zip = await archiveQuestFiles(pi, cwd, state, "FAILED", "nope");
  check(zip.endsWith("abc123.zip"), "zip path returned");
  check(pi.execCalls.length === 1 && pi.execCalls[0].command === "zip", "zip invoked");
  check(!existsSync(join(cwd, questDir("abc123" as Qid))), "current dir removed after zip");
  check(!existsSync(join(cwd, draftPath("abc123" as Qid))), "future draft removed after zip");
});

Deno.test("archived view records the outcome, not the pre-archive phase", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-views-"));
  const pi = fakePi();
  pi.exec = ((_command: string, _args: string[]) => {
    return Promise.resolve({ stdout: "", stderr: "", code: 0 });
  }) as typeof pi.exec;
  const state = { ...createQuest("do it", "abc123"), phase: "implementing" as const };
  let captured = "";
  const origExec = pi.exec.bind(pi);
  pi.exec = (async (command: string, args: string[], options?: { cwd?: string }) => {
    const res = await origExec(command, args, options);
    try {
      captured = await readFile(join(options?.cwd ?? cwd, "quest.md"), "utf8");
    } catch {
      captured = "";
    }
    return res;
  }) as typeof pi.exec;
  await archiveQuestFiles(pi, cwd, state, "ABANDONED", "discarded");
  check(captured.includes("Phase: archived"), "archived phase rendered");
  check(captured.includes("Archived: ABANDONED"), "outcome rendered");
});
