import { check } from "../check.ts";
import { existsSync, mkdtempSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { stageDir } from "../../src/domain/paths.ts";
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
import { stopBlink } from "../../src/durability/index.ts";
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
  try {
  const created = await applyUpdate(pi, ctx, { objective: "Build the thing." });
  check(created.applied.length === 1 && getState().qid !== null, "quest created");
  const qid = getState().qid!;
  const scaffold = await readFile(join(ctx.cwd, draftPath(qid)), "utf8");
  check(scaffold.includes("Build the thing."), "scaffold pre-created at provisioning");
  const drafted = await applyUpdate(pi, ctx, { draftName: "thing" });
  check(getState().phase === "drafting", "drafting");
  check(drafted.applied.length === 2, "draft applied plus thin nudge");
  check(drafted.applied.some((a) => a.includes("created thin")), "thin draft nudged");
  const file = await readFile(join(ctx.cwd, draftPath(qid)), "utf8");
  check(file.includes("Build the thing."), "draft template carries objective");
  const empty = await applyUpdate(pi, ctx, {});
  check(empty.applied.length === 0 && !empty.error, "no-op update");
  const missing = await applyUpdate(pi, ctx, { amendment: { change: "x", reasons: "y" } });
  check(missing.error !== undefined, "amendment outside implementing fails honestly");
  const planDraft = await applyUpdate(pi, ctx, { plan: "Do step one, then step two." });
  check(getState().draft?.planAuthored === true, "plan marks authored");
  check(planDraft.applied.some((a) => a.includes("plan recorded")), "plan applied");
  const onDisk = await readFile(join(ctx.cwd, draftPath(getState().qid!)), "utf8");
  check(onDisk.includes("Do step one, then step two."), "plan spliced into the draft file");
  const samePlan = await applyUpdate(pi, ctx, { plan: "Do step one, then step two." });
  check(samePlan.error !== undefined, "identical plan refused");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("plan writes after promotion redirect to amendment", async () => {
  replaceState(promote(authored(), "review"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  try {
    const refused = await applyUpdate(pi, ctx, { plan: "Do more things." });
    check(refused.error !== undefined, "plan refused outside drafting");
    check((refused.error ?? "").includes("amendment"), "refusal names the amendment channel");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
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

Deno.test("creation pre-creates the child scaffold and never clobbers", async () => {
  const parent = { ...authored(), phase: "implementing" as const };
  replaceState(parent);
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  const done = await createChildQuest(pi, ctx, "slice work", "slice", false);
  const childFile = await readFile(join(ctx.cwd, draftPath(done.childQid)), "utf8");
  check(childFile.includes("slice work"), "child scaffold pre-created");
  replaceState(IDLE_STATE);
  const cwd = tmp();
  const ctx2 = fakeCtx(cwd);
  try {
  await applyUpdate(fakePi(), ctx2, { objective: "Keep my words." });
  const qid = getState().qid!;
  await writeFile(join(cwd, draftPath(qid)), "agent-authored plan stays", "utf8");
  await applyUpdate(fakePi(), ctx2, { draftName: "thing" });
  const kept = await readFile(join(cwd, draftPath(qid)), "utf8");
  check(kept === "agent-authored plan stays", "first draft never clobbers existing content");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
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

Deno.test("archive completed without PASS guides to claimComplete", async () => {
  replaceState({ ...promote(authored(), "review"), phase: "implementing" as const });
  let detail = "";
  try {
    await archiveActiveQuest(fakePi(), fakeCtx(tmp()), "COMPLETED", "shipped");
  } catch (err) {
    detail = err instanceof Error ? err.message : String(err);
  }
  check(detail.includes("claimComplete"), "rejection names the recovery path");
  check(getState().qid === "abc123", "quest stays active after rejection");
  replaceState(IDLE_STATE);
});

Deno.test("archive abandoned blocks silent discard and cleans both dirs", async () => {
  const cwd = tmp();
  const pi = fakePi();
  replaceState({ ...promote(authored(), "review"), phase: "implementing" as const });
  let blocked = "";
  try {
    await archiveActiveQuest(pi, fakeCtx(cwd), "ABANDONED", "changed mind");
  } catch (err) {
    blocked = err instanceof Error ? err.message : String(err);
  }
  check(blocked.includes("confirmDiscard"), "unconfirmed discard rejected");
  check(getState().qid === "abc123", "quest stays active after rejection");
  let nosummary = "";
  try {
    await archiveActiveQuest(pi, fakeCtx(cwd), "ABANDONED", null, { confirmDiscard: true });
  } catch (err) {
    nosummary = err instanceof Error ? err.message : String(err);
  }
  check(nosummary.includes("summary"), "discard without summary rejected");
  await mkdir(join(cwd, ".pi/quest/future"), { recursive: true });
  await writeFile(join(cwd, draftPath(getState().qid!)), "draft", "utf8");
  const done = await archiveActiveQuest(pi, fakeCtx(cwd), "ABANDONED", "superseded by new direction", { confirmDiscard: true });
  check(done.archivedQid === "abc123", "explicit discard archived");
  check(getState().phase === "idle", "archive clears");
  check(!existsSync(join(cwd, stageDir("abc123" as Qid))), "staging dir removed");
  check(!existsSync(join(cwd, draftPath("abc123" as Qid))), "future draft removed");
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

Deno.test("draft creation carries refinements and blinks", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const calls: Array<string | undefined> = [];
  const ctx = fakeCtx(cwd, [], {
    setStatus: (_key: string, text: string | undefined) => {
      calls.push(text);
    },
  });
  try {
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    await applyUpdate(pi, ctx, { refinement: "found a global registry" });
    const drafted = await applyUpdate(pi, ctx, { draftName: "thing" });
    const qid = getState().qid!;
    const file = await readFile(join(cwd, draftPath(qid)), "utf8");
    check(file.includes("## Findings (pre-draft investigation)"), "refinements carried into scaffold");
    check(file.includes("- found a global registry"), "refinement text filed");
    check(!drafted.applied.join(" ").includes("created thin"), "no thin nudge when findings exist");
    check(calls.length === 1 && calls[0] === `\x1b[97m📝 ${qid} [F2]\x1b[0m`, "creation flashes bright");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("plan writes blink the hint", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const calls: Array<string | undefined> = [];
  const ctx = fakeCtx(cwd, [], {
    setStatus: (_key: string, text: string | undefined) => {
      calls.push(text);
    },
  });
  try {
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    await applyUpdate(pi, ctx, { draftName: "thing" });
    const qid = getState().qid!;
    calls.length = 0;
    await applyUpdate(pi, ctx, { plan: "Do step one." });
    check(calls.length === 1 && calls[0] === `\x1b[97m📝 ${qid} [F2]\x1b[0m`, "plan write flashes bright");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("plan saves warn on citations that do not resolve on disk", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const ctx = fakeCtx(cwd);
  try {
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    await applyUpdate(pi, ctx, { draftName: "thing" });
    const missing = await applyUpdate(pi, ctx, { plan: "Walk the tree via song.c:12 and gig.c:40." });
    check(missing.applied.some((a) => a.includes("claims check")), "claims check reported");
    check(missing.applied.some((a) => a.includes("song.c:12 not found")), "missing file named");
    const dir = join(cwd, "mods/common/ux");
    await mkdir(dir, { recursive: true });
    await writeFile(join(dir, "site_page.c"), Array(30).fill("/* x */").join("\n"), "utf8");
    const resolved = await applyUpdate(pi, ctx, { plan: "Use the canonical responder mods/common/ux/site_page.c:20." });
    check(resolved.applied.some((a) => a.includes("claims check: 1 citations resolve")), "resolving citation counted as ok");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("checkPlan returns a draft profile without writing or booting a review", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const ctx = fakeCtx(cwd);
  try {
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    await applyUpdate(pi, ctx, { draftName: "thing" });
    const qid = getState().qid!;
    const beforeHash = getState().draft?.contentHash ?? null;
    const snapsBefore = pi.appended.filter((e) => e.customType === SNAPSHOT_TYPE).length;
    const probe = await applyUpdate(pi, ctx, { checkPlan: "Do step one via song.c:12." });
    check(probe.applied.some((a) => a.includes("draft profile")), "profile reported");
    check(probe.applied.some((a) => a.includes("requirements 0")), "partial draft reported honestly");
    check((getState().draft?.contentHash ?? null) === beforeHash, "check does not rewrite the draft");
    check(getState().draft?.planAuthored === false, "check does not mark the plan authored");
    check(pi.appended.filter((e) => e.customType === SNAPSHOT_TYPE).length === snapsBefore, "no snapshot emitted for a pure check");
    check(getState().phase === "drafting", "no phase change");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("checkPlan resolves the would-be plan against the draft on disk", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const ctx = fakeCtx(cwd);
  try {
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    await applyUpdate(pi, ctx, { draftName: "thing" });
    const qid = getState().qid!;
    const file = join(cwd, draftPath(qid));
    await writeFile(
      file,
      "## Requirements\n- one\n- two\n\n## Evidence\n- measurement\n\n## Implementation Plan\nrough\n",
      "utf8",
    );
    const probe = await applyUpdate(pi, ctx, { checkPlan: "Rewrite the walker." });
    check(probe.applied.some((a) => a.includes("requirements 2")), "on-disk requirements counted");
    check(probe.applied.some((a) => a.includes("evidence 1")), "on-disk evidence counted");
    check(probe.applied.some((a) => a.includes("maturity bar: met")), "reviewable verdict for a real draft");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("checkPlan refuses without a draft and never creates a quest", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  try {
    const idle = await applyUpdate(pi, ctx, { checkPlan: "anything" });
    check(idle.error !== undefined && idle.error.includes("draft"), "idle check refuses with guidance");
    check(getState().qid === null, "no quest created by a pure check");
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    const provisional = await applyUpdate(pi, ctx, { checkPlan: "anything" });
    check(provisional.error !== undefined && provisional.error.includes("draft"), "provisional check refuses");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("plan saves report the draft profile alongside the citation check", async () => {
  replaceState(IDLE_STATE);
  const pi = fakePi();
  const cwd = tmp();
  const ctx = fakeCtx(cwd);
  try {
    await applyUpdate(pi, ctx, { objective: "Thin the code." });
    await applyUpdate(pi, ctx, { draftName: "thing" });
    const saved = await applyUpdate(pi, ctx, { plan: "Step one: thin gig.c:5." });
    check(saved.applied.some((a) => a.includes("draft profile")), "profile on plan save");
    check(saved.applied.some((a) => a.includes("maturity bar")), "bar verdict on plan save");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});
