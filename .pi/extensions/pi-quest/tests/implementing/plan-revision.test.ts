import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import {
  createDraft,
  createQuest,
  IDLE_STATE,
  promote,
} from "../../src/domain/quest.ts";
import {
  approvePlanRevision,
  recordPlanRevision,
  revertPlanRevision,
} from "../../src/domain/plan-revision.ts";
import { draftPath } from "../../src/domain/paths.ts";
import type { QuestState } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { implementationFingerprint } from "../../src/review/flow.ts";
import { buildReviewPrompt } from "../../src/review/prompts.ts";
import { installDraftGate } from "../../src/drafting/gate.ts";
import { cancelReview, trackReview } from "../../src/review/tracker.ts";
import { applyUpdate } from "../../src/surface/tools/update-state.ts";
import { stopBlink } from "../../src/durability/index.ts";
import type { Pi, ToolCallEvent } from "../../src/hooks/events.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

const QID = "abc123" as Qid;

function authored() {
  const s = createDraft(createQuest("req", QID), "thing");
  return { ...s, draft: { ...s.draft!, planAuthored: true, contentHash: "hash-v1" } };
}

function implementing(): QuestState {
  return promote(authored(), "review");
}

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-revision-"));
}

Deno.test("promotion binds the approved plan hash", () => {
  const s = implementing();
  check(s.draft?.approvedPlanHash === "hash-v1", "approved hash bound at promote");
  check((s.draft?.planRevisions ?? []).length === 0, "no revisions yet");
});

Deno.test("recordPlanRevision appends history and keeps the old binding", () => {
  const s = recordPlanRevision(implementing(), "hash-v1", "hash-v2", "step 3 was wrong", "old plan", 7);
  check(s.draft?.contentHash === "hash-v2", "content moves to the revision");
  check(s.draft?.approvedPlanHash === "hash-v1", "binding stays on the approved plan");
  check(s.draft?.planRevisions.length === 1, "history appended");
  check(s.draft?.planRevisions[0].plan === "old plan", "prior text kept");
  check(s.draft?.planRevisions[0].note === "step 3 was wrong", "note kept");
  check(s.snapshotPending, "marks pending");
  for (const fn of [
    () => recordPlanRevision(authored(), "h1", "h2", "n", "p"),
    () => recordPlanRevision(implementing(), "", "h2", "n", "p"),
    () => recordPlanRevision(implementing(), "h1", "", "n", "p"),
  ]) {
    let threw = false;
    try {
      fn();
    } catch {
      threw = true;
    }
    check(threw, "bad revision rejected");
  }
});

Deno.test("approvePlanRevision adopts the revision", () => {
  const staged = recordPlanRevision(implementing(), "hash-v1", "hash-v2", "fix", "old plan");
  const s = approvePlanRevision(staged, "hash-v2", "new plan");
  check(s.draft?.approvedPlanHash === "hash-v2", "binding moves on approval");
  check(s.draft?.lastReviewedPlan === "new plan", "review baseline follows");
  check(s.phase === "implementing", "stays implementing");
  let threw = false;
  try {
    approvePlanRevision(authored(), "h", "p");
  } catch {
    threw = true;
  }
  check(threw, "approval outside implementing rejected");
});

Deno.test("revertPlanRevision restores the approved binding and keeps history", () => {
  const staged = recordPlanRevision(implementing(), "hash-v1", "hash-v2", "fix", "old plan");
  const s = revertPlanRevision(staged);
  check(s.draft?.contentHash === "hash-v1", "content restored to approved");
  check(s.draft?.planRevisions.length === 1, "history stays append-only");
});

Deno.test("implementation fingerprint binds the plan hashes", () => {
  const base = implementationFingerprint(implementing());
  const revised = recordPlanRevision(implementing(), "hash-v1", "hash-v2", "fix", "old");
  check(implementationFingerprint(revised) !== base, "staged revision changes the fingerprint");
  const approved = approvePlanRevision(revised, "hash-v2", "new");
  check(implementationFingerprint(approved) !== base, "adopted revision changes the fingerprint");
  check(implementationFingerprint(approved) !== implementationFingerprint(revised), "adopt differs from staged");
  check(implementationFingerprint(implementing()) === base, "same plan, same fingerprint");
});

Deno.test("legacy snapshots without revision fields still work", () => {
  const legacy = {
    ...implementing(),
    draft: { name: "thing", planAuthored: true, approvedBy: "review", outstandingFindings: false, contentHash: "hash-v1" },
  } as unknown as QuestState;
  const fp = implementationFingerprint(legacy);
  check(typeof fp === "string" && fp.length === 64, "fingerprint tolerates missing fields");
  const staged = recordPlanRevision(legacy, "hash-v1", "hash-v2", "fix", "old plan");
  check(staged.draft?.planRevisions.length === 1, "history starts from empty");
  check((staged.draft?.approvedPlanHash ?? null) === null, "missing binding reads as null");
});

Deno.test("revision note renders in the re-review brief", () => {
  const prompt = buildReviewPrompt("draft", QID, "hash-v2", {
    objective: "ship it",
    plan: "new plan",
    evidence: [],
    amendments: [],
    planDiff: "- old\n+ new",
    revisionNote: "mid-implementation revision: objective unchanged",
  });
  check(prompt.includes("mid-implementation revision"), "revision context present");
  check(prompt.includes("- old"), "diff present");
});

type Handler = (event: ToolCallEvent, ctx: unknown) => { block?: boolean; reason?: string; terminate?: boolean } | undefined;

function captureGate(): Handler {
  const pi = fakePi();
  let captured: Handler | undefined;
  (pi as { on: unknown }).on = (_event: string, handler: unknown) => {
    captured = handler as Handler;
  };
  installDraftGate(pi as unknown as Pi);
  if (!captured) throw new Error("gate never subscribed");
  return captured;
}

Deno.test("gate tightens while a plan-revision review runs", () => {
  const handler = captureGate();
  replaceState(implementing());
  try {
    trackReview(QID, "hash-v2", () => {});
    const ctx = fakeCtx("/tmp");
    const write = { type: "tool_call", toolCallId: "1", toolName: "edit", input: { path: "src/impl.ts" } } as ToolCallEvent;
    const blocked = handler(write, ctx);
    check(blocked?.block === true, "writes blocked mid-revision-review");
    check(blocked?.terminate === true, "turn ends mid-revision-review");
    check(String(blocked?.reason).includes("PLAN_REVISION_REVIEW"), "reason names the revision review");
    const read = { type: "tool_call", toolCallId: "2", toolName: "read", input: { path: "src/impl.ts" } } as ToolCallEvent;
    check(handler(read, ctx)?.block === true, "reads blocked too, like drafting");
    const journal = { type: "tool_call", toolCallId: "3", toolName: "quest_update_state", input: {} } as ToolCallEvent;
    check(handler(journal, ctx) === undefined, "journal ops stay usable");
    const draftWrite = { type: "tool_call", toolCallId: "4", toolName: "edit", input: { path: draftPath(QID) } } as ToolCallEvent;
    check(handler(draftWrite, ctx) === undefined, "draft saves still supersede");
  } finally {
    cancelReview(QID);
    replaceState(IDLE_STATE);
  }
});

Deno.test("gate stays open in implementing with no revision review", () => {
  const handler = captureGate();
  replaceState(implementing());
  try {
    const write = { type: "tool_call", toolCallId: "1", toolName: "edit", input: { path: "src/impl.ts" } } as ToolCallEvent;
    check(handler(write, fakeCtx("/tmp")) === undefined, "implementing stays unrestricted");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("planRevision writes the draft file from implementing", async () => {
  const cwd = tmp();
  const pi = fakePi();
  const ctx = fakeCtx(cwd);
  replaceState(implementing());
  try {
    const { writeFile, mkdir } = await import("node:fs/promises");
    await mkdir(join(cwd, ".pi/quest/future"), { recursive: true });
    await writeFile(join(cwd, draftPath(QID)), "# t\n\n## Implementation Plan\n\nold plan\n", "utf8");
    const done = await applyUpdate(pi, ctx, { planRevision: "new plan", note: "step 3 was wrong" });
    check(done.applied.some((a) => a.includes("plan revision")), "revision applied");
    const onDisk = await readFile(join(cwd, draftPath(QID)), "utf8");
    check(onDisk.includes("new plan"), "revision spliced into the draft file");
    check(getState().draft?.planRevisions.length === 1, "history recorded");
    check(getState().draft?.approvedPlanHash === "hash-v1", "binding unchanged until review PASS");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("planRevision refuses outside implementing and guards scope", async () => {
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  replaceState(authored());
  try {
    const refused = await applyUpdate(pi, ctx, { planRevision: "new plan" });
    check(refused.error !== undefined, "revision refused outside implementing");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
  replaceState(implementing());
  try {
    const scoped = await applyUpdate(pi, ctx, { planRevision: "new plan", objective: "different goal" });
    check(scoped.error !== undefined && scoped.error.includes("scope"), "objective change refused as scope change");
    const plan = await applyUpdate(pi, ctx, { plan: "other plan" });
    check(plan.error !== undefined && plan.error.includes("amendment"), "plain plan still drafting-only");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});
