import { check } from "../check.ts";
import { mkdirSync, mkdtempSync, writeFileSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { loadQuestState } from "../../src/durability/index.ts";
import { encodeSnapshot, SNAPSHOT_TYPE } from "../../src/durability/snapshots.ts";
import { installCommands } from "../../src/surface/commands/index.ts";
import { collectCandidates, pickQuest } from "../../src/surface/commands/pick.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-pick-"));
}

function draftCwd(cwd: string, qid: string, body = "## Original request\n\nDrafted work.\n"): void {
  mkdirSync(join(cwd, ".pi", "quest", "future"), { recursive: true });
  writeFileSync(join(cwd, ".pi", "quest", "future", `${qid}.md`), body);
}

function sessionsDir(withStates: Array<{ qid: string; objective: string; archived?: boolean }>): string {
  const root = mkdtempSync(join(tmpdir(), "pi-quest-pick-sess-"));
  mkdirSync(join(root, "proj"), { recursive: true });
  const lines = withStates.map((s) => {
    const state = s.archived
      ? { ...createQuest(s.objective, s.qid), phase: "archived" as const }
      : createQuest(s.objective, s.qid);
    return JSON.stringify({ customType: SNAPSHOT_TYPE, data: encodeSnapshot(state) });
  });
  writeFileSync(join(root, "proj", "sess.jsonl"), lines.join("\n"));
  return root;
}

Deno.test("candidates merge branch, siblings, and drafts; archived excluded", async () => {
  replaceState(IDLE_STATE);
  const cwd = tmp();
  draftCwd(cwd, "dff456");
  const sess = sessionsDir([
    { qid: "old001", objective: "sibling work" },
    { qid: "arc999", objective: "archived work", archived: true },
  ]);
  const branch = encodeSnapshot(createQuest("branch work", "abc123"));
  const found = await collectCandidates(fakeCtx(cwd, [{ customType: SNAPSHOT_TYPE, data: branch }]), sess);
  const qids = found.map((c) => c.qid).sort();
  check(qids.join(",") === "abc123,dff456,old001", `merged without archived: ${qids.join(",")}`);
  check(found.every((c) => c.label.includes(c.qid)), "labels name their quest");
  replaceState(IDLE_STATE);
});

Deno.test("candidates prefer newest per qid and skip archived revisions", async () => {
  replaceState(IDLE_STATE);
  const cwd = tmp();
  const sess = sessionsDir([]);
  const older = encodeSnapshot(createQuest("first try", "abc123"));
  const archived = encodeSnapshot({ ...createQuest("gave up", "abc123"), phase: "archived" as const });
  const found = await collectCandidates(
    fakeCtx(cwd, [
      { customType: SNAPSHOT_TYPE, data: older },
      { customType: SNAPSHOT_TYPE, data: archived },
    ]),
    sess,
  );
  check(found.length === 1 && found[0].label.includes("first try"), "archived revision skipped");
  replaceState(IDLE_STATE);
});

Deno.test("pick orients when nothing is known", async () => {
  replaceState(IDLE_STATE);
  const out = await pickQuest(fakePi(), fakeCtx(tmp()), sessionsDir([]));
  check(out.includes("No active quest") && getState().qid === null, "idle explained, state untouched");
  replaceState(IDLE_STATE);
});

Deno.test("pick auto-resumes a lone candidate", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  draftCwd(cwd, "dff456");
  const out = await pickQuest(pi, fakeCtx(cwd), sessionsDir([]));
  check(out.includes("dff456") && getState().qid === "dff456", "lone draft adopted without dialog");
  replaceState(IDLE_STATE);
});

Deno.test("pick offers several candidates through select", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  draftCwd(cwd, "dff456");
  const sess = sessionsDir([{ qid: "old001", objective: "sibling work" }]);
  const cands = await collectCandidates(fakeCtx(cwd), sess);
  check(cands.length === 2, "two candidates");
  const target = cands.find((c) => c.qid === "old001")?.label ?? "";
  const ctx = { ...fakeCtx(cwd), hasUI: true, ui: { ...fakeCtx(cwd).ui, select: async () => target } };
  const out = await pickQuest(pi, ctx, sess);
  check(out.includes("old001") && getState().qid === "old001", "selection resumed");
  replaceState(IDLE_STATE);
});

Deno.test("pick cancellation and headless runs change nothing", async () => {
  replaceState(IDLE_STATE);
  const cwd = tmp();
  draftCwd(cwd, "dff456");
  const sess = sessionsDir([{ qid: "old001", objective: "sibling work" }]);
  const cancelled = await pickQuest(fakePi(), { ...fakeCtx(cwd), hasUI: true }, sess);
  check(cancelled.includes("cancelled") && getState().qid === null, "cancel leaves state alone");
  const headless = await pickQuest(fakePi(), fakeCtx(cwd), sess);
  check(headless.includes("old001") && headless.includes("/quest <qid>") && getState().qid === null, "headless lists");
  replaceState(IDLE_STATE);
});

Deno.test("pick keeps the active quest and never prompts", async () => {
  replaceState(createQuest("current work", "abc123"));
  let asked = false;
  const ctx = { ...fakeCtx(tmp()), hasUI: true, ui: { ...fakeCtx(tmp()).ui, select: async () => { asked = true; return undefined; } } };
  const out = await pickQuest(fakePi(), ctx, sessionsDir([]));
  check(out.includes("abc123") && !asked, "summary without dialog");
  replaceState(IDLE_STATE);
});

Deno.test("bare /quest refreshes the status bar", async () => {
  replaceState(IDLE_STATE);
  const home = Deno.env.get("HOME");
  Deno.env.set("HOME", mkdtempSync(join(tmpdir(), "pi-quest-pick-home-")));
  try {
  const pi = fakePi();
  installCommands(pi);
  const quest = pi.commands.find((c) => c.name === "quest");
  check(quest !== undefined, "quest command registered");
  const cwd = tmp();
  draftCwd(cwd, "dff456");
  const statuses: Array<string | undefined> = [];
  const notes: string[] = [];
  const ctx = {
    ...fakeCtx(cwd),
    ui: {
      ...fakeCtx(cwd).ui,
      notify: (m: string) => notes.push(m),
      setStatus: (_key: string, text: string | undefined) => statuses.push(text),
    },
  };
  await quest?.options.handler("", ctx);
  check(getState().qid === "dff456", "lone candidate adopted through the command");
  check(statuses.length === 1 && statuses[0]?.includes("dff456") === true, "status bar re-asserted");
  check(notes.length === 1 && notes[0].includes("dff456"), "toast reports the quest");
  replaceState(IDLE_STATE);
  } finally {
    if (home === undefined) Deno.env.delete("HOME");
    else Deno.env.set("HOME", home);
  }
});

Deno.test("fresh boot stays idle despite sibling snapshots", async () => {
  const sess = sessionsDir([{ qid: "old001", objective: "sibling work" }]);
  const state = await loadQuestState([], sess);
  check(state.qid === null && state.phase === "idle", "no auto-adopt on fresh boot");
});
