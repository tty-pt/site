import { check } from "../check.ts";
import { mkdtempSync, mkdirSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE, recordReviewResult } from "../../src/domain/quest.ts";
import { encodeSnapshot, SNAPSHOT_TYPE } from "../../src/durability/snapshots.ts";
import { killQuest } from "../../src/surface/commands/quest-del.ts";
import { listQuests } from "../../src/surface/commands/quests.ts";
import { resumeQuest, summarizeActive } from "../../src/surface/commands/quest.ts";
import { askHumanTool } from "../../src/surface/tools/ask-human.ts";
import { rebutTool } from "../../src/surface/tools/rebut.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-cmd-"));
}

Deno.test("quest command shows, resumes, and adopts", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  check((await resumeQuest(pi, fakeCtx(cwd), "")).includes("No active quest"), "idle shown");
  const hit = encodeSnapshot(createQuest("found work", "abc123"));
  const resumed = await resumeQuest(pi, fakeCtx(cwd, [{ customType: SNAPSHOT_TYPE, data: hit }]), "abc123");
  check(resumed.includes("abc123") && getState().qid === "abc123", "snapshot resumed");
  check(summarizeActive().includes("provisional"), "summary reflects phase");
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, ".pi", "quest", "future", "dff456.md"), "## Original request\n\nDrafted work.\n");
  const adopted = await resumeQuest(pi, fakeCtx(cwd), "dff456");
  check(adopted.includes("dff456") && getState().phase === "drafting", "draft file adopted");
  check((await resumeQuest(pi, fakeCtx(cwd), "nope")).includes("No quest matching"), "unknown reported");
  replaceState(IDLE_STATE);
});

Deno.test("quests command lists branch, drafts, and archives", async () => {
  replaceState(IDLE_STATE);
  const cwd = tmp();
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, ".pi", "quest", "future", "dff456.md"), "draft");
  mkdirSync(join(cwd, ".pi", "quest", "archive"), { recursive: true });
  writeFileSync(join(cwd, ".pi", "quest", "archive", "old001.zip"), "zip");
  const hit = encodeSnapshot(createQuest("active work", "abc123"));
  replaceState(createQuest("active work", "abc123"));
  const rows = await listQuests(fakeCtx(cwd, [{ customType: SNAPSHOT_TYPE, data: hit }]));
  const text = rows.join("\n");
  check(text.includes("abc123") && text.includes("active"), "branch quest with marker");
  check(text.includes("dff456"), "draft listed");
  check(text.includes("old001"), "archive listed");
  replaceState(IDLE_STATE);
});

Deno.test("quest-del archives the active quest abandoned", async () => {
  const drafting = createDraft(createQuest("doomed", "abc123"), "doomed");
  replaceState({ ...drafting, phase: "implementing" as const });
  const pi = fakePi();
  const out = await killQuest(pi, fakeCtx(tmp()), "");
  check(out.includes("abandoned") && getState().phase === "idle", "killed and cleared");
  replaceState(IDLE_STATE);
  const none = await killQuest(fakePi(), fakeCtx(tmp()), "");
  check(none.includes("No active quest"), "nothing to kill");
});

Deno.test("rebut and ask-human tools answer honestly", async () => {
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  const short = await rebutTool(pi).execute("1", { rebuttal: "no" }, undefined, undefined, ctx);
  check((short.details as { error: string }).error === "rebuttal_too_short", "thin rebuttal refused");
  replaceState(IDLE_STATE);
  const idle = await rebutTool(pi).execute("1", { rebuttal: "substantive evidence here" }, undefined, undefined, ctx);
  check((idle.details as { error: string }).error === "nothing_to_rebut", "idle rebut refused");
  const drafting = createDraft(createQuest("work", "abc123"), "work");
  const reviewed = recordReviewResult(
    { ...drafting, draft: { ...drafting.draft!, contentHash: "h1" } },
    "FAIL",
    "h1",
    "missing auth",
  );
  replaceState(reviewed);
  const round = await rebutTool(pi).execute("1", { rebuttal: "auth is covered in section 3" }, undefined, undefined, ctx);
  check((round.details as { round: number }).round === 1, "round recorded without a draft file on disk");
  replaceState(createQuest("work", "abc123"));
  const asked = await askHumanTool(pi).execute("2", { question: "Which?", default: "this" }, undefined, undefined, ctx);
  check((asked.details as { source: string }).source === "default", "no-UI defaults");
  check((asked.content[0].text ?? "").includes("this"), "default surfaced");
  replaceState(IDLE_STATE);
});
