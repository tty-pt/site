import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import {
  claimComplete,
  createDraft,
  createQuest,
  IDLE_STATE,
  promote,
} from "../../src/domain/quest.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { implementationFingerprint } from "../../src/review/flow.ts";
import { archiveActiveQuest } from "../../src/surface/tools/archive.ts";
import { applyUpdate } from "../../src/surface/tools/update-state.ts";
import { createChildQuest } from "../../src/surface/tools/subquest.ts";
import { recoverQuest, recoverTool } from "../../src/surface/tools/recover.ts";
import { encodeSnapshot, SNAPSHOT_TYPE } from "../../src/durability/snapshots.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-tools-"));
}

function authored() {
  const s = createDraft(createQuest("req", "abc123"), "thing");
  return { ...s, draft: { ...s.draft!, planAuthored: true } };
}

Deno.test("update creates quests and drafts on disk", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  const created = await applyUpdate(pi, ctx, { objective: "Build the thing." });
  check(created.applied.length === 1 && getState().qid !== null, "quest created");
  const qid = getState().qid!;
  const drafted = await applyUpdate(pi, ctx, { draftName: "thing" });
  check(getState().phase === "drafting", "drafting");
  check(drafted.applied.length === 1, "draft applied");
  const file = await readFile(join(ctx.cwd, draftPath(qid)), "utf8");
  check(file.includes("Build the thing."), "draft template carries objective");
  const empty = await applyUpdate(pi, ctx, {});
  check(empty.applied.length === 0 && !empty.error, "no-op update");
  const missing = await applyUpdate(pi, ctx, { amendment: { change: "x", reasons: "y" } });
  check(missing.error !== undefined, "amendment outside implementing fails honestly");
  replaceState(IDLE_STATE);
});

Deno.test("update claims completion only with no running children", async () => {
  replaceState(promote(authored(), "review"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  const refined = await applyUpdate(pi, ctx, { refinement: "needs retries" });
  check(getState().refinements.length === 1, "refinement recorded");
  check(refined.applied.length === 1, "applied listed");
  replaceState({ ...promote(authored(), "review"), children: [{ qid: "kid001" as Qid, brief: "b", status: "running", findings: null, acknowledged: false }] });
  const blocked = await applyUpdate(pi, ctx, { claimComplete: true });
  check(blocked.error !== undefined && blocked.error.includes("kid001"), "children block claims");
  replaceState(promote(authored(), "review"));
  const claimed = await applyUpdate(pi, ctx, { claimComplete: true });
  check(getState().phase === "validating", "validating");
  check(claimed.applied.length === 1, "claim applied");
  replaceState({
    ...promote(authored(), "review"),
    children: [{ qid: "kid001" as Qid, brief: "b", status: "returned", findings: "done", acknowledged: false }],
  });
  const continued = await applyUpdate(pi, ctx, { continuePast: "kid001" });
  check(getState().children[0].acknowledged, "child explicitly continued past");
  check(continued.applied.length === 1, "continue applied");
  const unknown = await applyUpdate(pi, ctx, { continuePast: "zzz999" });
  check(unknown.error !== undefined, "unknown child rejected");
  replaceState(IDLE_STATE);
});

Deno.test("subquest links children and enforces the depth cap", async () => {
  const parent = { ...authored(), phase: "implementing" as const };
  replaceState(parent);
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  const done = await createChildQuest(pi, ctx, "slice work", "slice", false);
  check(getState().qid === parent.qid, "parent stays without switch");
  check(getState().children.length === 1, "child linked");
  check(pi.appended.some((e) => e.customType === SNAPSHOT_TYPE), "child snapshot emitted");
  const deep = { ...getState(), depth: 3 };
  replaceState(deep);
  let threw = false;
  try {
    await createChildQuest(pi, ctx, "too deep", "deep", false);
  } catch {
    threw = true;
  }
  check(threw, "depth cap enforced");
  replaceState({ ...authored(), phase: "implementing" as const });
  const switched = await createChildQuest(pi, ctx, "slice two", "slice", true);
  check(switched.switched && getState().parentQid === parent.qid, "switch moves to child");
  replaceState(IDLE_STATE);
});

Deno.test("archive enforces PASS for completed and returns children", async () => {
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  replaceState(authored());
  let threw = false;
  try {
    await archiveActiveQuest(pi, ctx, "COMPLETED", null);
  } catch {
    threw = true;
  }
  check(threw, "completed without PASS rejected");
  const claimed = claimComplete({ ...authored(), phase: "implementing" as const });
  const target = implementationFingerprint(claimed);
  replaceState({ ...claimed, lastReview: { verdict: "PASS", target, findings: "clean" } });
  const done = await archiveActiveQuest(pi, ctx, "COMPLETED", "shipped");
  check(done.archivedQid === "abc123", "archived");
  check(getState().phase === "idle", "top-level archive clears");
  const parent = createQuest("parent work", "par001");
  const child = { ...claimComplete({ ...authored(), phase: "implementing" as const }), parentQid: "par001" as Qid };
  const childTarget = implementationFingerprint(child);
  replaceState({ ...child, lastReview: { verdict: "PASS", target: childTarget, findings: "ok" } });
  const entries = [{ customType: SNAPSHOT_TYPE, data: encodeSnapshot(parent) }];
  const returned = await archiveActiveQuest(pi, fakeCtx(ctx.cwd, entries), "COMPLETED", "child done");
  check(returned.returnedToParent === "par001", "returned to parent");
  check(getState().qid === "par001", "parent restored");
  check(getState().children.length === 0, "parent had no children yet");
  replaceState(IDLE_STATE);
});

Deno.test("recover reads the transcript and cold-starts honestly", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const hit = encodeSnapshot(createQuest("found", "abc123"));
  const done = await recoverQuest(fakeCtx(tmp(), [{ customType: SNAPSHOT_TYPE, data: hit }]), null);
  check(done.qid === "abc123" && done.source === "transcript", "transcript hit");
  check(getState().qid === "abc123", "state restored");
  void pi;
  const cold = await recoverQuest(fakeCtx(tmp(), [{ customType: "other", data: {} }]), "zzz999");
  check(cold.qid === null, "unknown qid finds nothing");
  replaceState(IDLE_STATE);
});

Deno.test("recover tool orients the agent", async () => {
  replaceState(IDLE_STATE);
  const hit = encodeSnapshot(createDraft(createQuest("found", "abc123"), "found"));
  const out = await recoverTool(fakePi()).execute(
    "1",
    { qid: "abc123" },
    undefined,
    undefined,
    fakeCtx(tmp(), [{ customType: SNAPSHOT_TYPE, data: hit }]),
  );
  const text = String(out.content[0].text ?? "");
  check(text.includes("drafting"), "phase oriented");
  check(text.includes("future/abc123.md"), "draft path oriented");
  check((out.details as { phase: string }).phase === "drafting", "phase in details");
  replaceState(IDLE_STATE);
});
