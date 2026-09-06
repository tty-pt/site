import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { mkdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { getState, replaceState, updateState } from "../../src/app/store.ts";
import { installDrafting } from "../../src/drafting/index.ts";
import { ensureDraftReview, hashContent } from "../../src/drafting/reviews.ts";
import { createDraft, createQuest, IDLE_STATE, recordReviewResult } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths.ts";
import { cancelReview, trackReview } from "../../src/review/tracker.ts";
import { fakeCtx, fakePi, type FakePi } from "../fake-pi.ts";

const DRAFT_ONE = `## Requirements
- first requirement
- second requirement

## Implementation Plan
Do the work in order.
`;

const DRAFT_TWO = `${DRAFT_ONE}Then verify the result.
`;

function resumeSteers(pi: FakePi): string[] {
  return pi.sent
    .filter((s) => s.options?.deliverAs === "steer")
    .map((s) => String((s.message.content as unknown) ?? ""))
    .filter((text) => text.includes("No reviewer available"));
}

// Boots run detached (void bootDraftReview); the steer lands shortly after.
const settle = (): Promise<void> => new Promise((resolve) => setTimeout(resolve, 150));

async function draftingSetup(qid: Qid, content: string): Promise<{ cwd: string; ctx: ReturnType<typeof fakeCtx> }> {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-resume-"));
  const file = join(cwd, draftPath(qid));
  await mkdir(dirname(file), { recursive: true });
  await writeFile(file, content, "utf8");
  replaceState(createDraft(createQuest("resume work", qid), "resume"));
  return { cwd, ctx: fakeCtx(cwd) };
}

Deno.test("unreviewed draft boots exactly one notice per target", async () => {
  const qid = "resume1" as Qid;
  const { ctx } = await draftingSetup(qid, DRAFT_ONE);
  const pi = fakePi();
  try {
    await ensureDraftReview(pi, ctx);
    await settle();
    check(resumeSteers(pi).length === 1, "first start boots the review");
    await ensureDraftReview(pi, ctx);
    await settle();
    check(resumeSteers(pi).length === 1, "second start stays quiet for the same target");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("recorded verdicts are not re-booted", async () => {
  const qid = "resume2" as Qid;
  const { ctx } = await draftingSetup(qid, DRAFT_ONE);
  const pi = fakePi();
  try {
    updateState((s) => recordReviewResult(s, "FAIL", hashContent(DRAFT_ONE), "thin plan"));
    await ensureDraftReview(pi, ctx);
    check(resumeSteers(pi).length === 0, "FAIL on file stays with the implementer");
    updateState((s) => recordReviewResult(s, "PASS", hashContent(DRAFT_ONE), "solid"));
    await ensureDraftReview(pi, ctx);
    check(resumeSteers(pi).length === 0, "PASS on file is not re-reviewed");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("in-flight reviews are not double-booted", async () => {
  const qid = "resume3" as Qid;
  const { ctx } = await draftingSetup(qid, DRAFT_ONE);
  const pi = fakePi();
  try {
    trackReview(qid, hashContent(DRAFT_ONE), () => {});
    await ensureDraftReview(pi, ctx);
    check(resumeSteers(pi).length === 0, "live review keeps the floor");
    check(getState().qid === qid, "state untouched");
  } finally {
    cancelReview(qid);
    replaceState(IDLE_STATE);
  }
});

Deno.test("revised content after a verdict boots a fresh review", async () => {
  const qid = "resume4" as Qid;
  const { cwd, ctx } = await draftingSetup(qid, DRAFT_ONE);
  const pi = fakePi();
  try {
    updateState((s) => recordReviewResult(s, "FAIL", hashContent(DRAFT_ONE), "thin plan"));
    await writeFile(join(cwd, draftPath(qid)), DRAFT_TWO, "utf8");
    await ensureDraftReview(pi, ctx);
    await settle();
    check(resumeSteers(pi).length === 1, "new target boots again");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("planless drafts and idle stay silent on resume", async () => {
  const qid = "resume5" as Qid;
  const { ctx } = await draftingSetup(qid, "## Requirements\n- one\n");
  const pi = fakePi();
  try {
    await ensureDraftReview(pi, ctx);
    check(resumeSteers(pi).length === 0, "nothing reviewable, nothing booted");
  } finally {
    replaceState(IDLE_STATE);
  }
  replaceState(IDLE_STATE);
  const idlePi = fakePi();
  await ensureDraftReview(idlePi, fakeCtx("/tmp"));
  check(resumeSteers(idlePi).length === 0, "idle is a no-op");
});

Deno.test("drafting installer resumes on session start", () => {
  const pi = fakePi();
  installDrafting(pi);
  check(pi.subscriptions.includes("session_start"), "session start subscribed");
  check(pi.subscriptions.includes("turn_end"), "turn end still subscribed");
});
