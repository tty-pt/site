import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { mkdir, readFile, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { getState, replaceState, updateState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE, recordReviewResult } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths.ts";
import { stopBlink } from "../../src/durability/index.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";
import type { FakePi, SentMessage } from "../fake-pi.ts";
import { DEFAULT_CONFIG } from "../../src/config.ts";
import {
  GO_PATTERN,
  MAX_REVIEW_RETRIES,
  REVIEW_RETRY_BASE_MS,
  bootDraftReview,
  clearDraftReviewRetry,
  draftReviewRetryCount,
  onTurnEndCatchAll,
  setReviewRetryDispatcher,
  watchDraftFileCatchAll,
} from "../../src/drafting/reviews.ts";
import {
  bumpReviewCount,
  hashContent,
  meetsReviewThresholds,
  parseDraftSections,
  reviewMaterial,
  seedReviewCount,
  splicePlanSection,
} from "../../src/drafting/plan-text.ts";

const DRAFT = `## Requirements
- first requirement
- second requirement

## Evidence
- found it in the code

## Implementation Plan
Do the work in order.
`;

Deno.test("draft sections parse requirements, evidence, and plan", () => {
  const sections = parseDraftSections(DRAFT);
  check(sections.requirements.length === 2, "two requirements");
  check(sections.evidence.length === 1, "one evidence");
  check(sections.plan.includes("Do the work"), "plan body kept");
});

Deno.test("draft thresholds follow the configured counts", () => {
  const sections = parseDraftSections(DRAFT);
  check(meetsReviewThresholds(sections), "2 requirements pass");
  const thin = parseDraftSections("## Requirements\n- one\n\n## Implementation Plan\nplan\n");
  check(!meetsReviewThresholds(thin), "1 requirement without evidence fails");
  const evidential = parseDraftSections(
    `## Requirements\n- one\n\n## Evidence\n${Array.from({ length: 7 }, (_, i) => `- e${i}`).join("\n")}\n\n## Implementation Plan\nplan\n`,
  );
  check(meetsReviewThresholds(evidential), "1 requirement plus 7 evidence passes");
  const planless = parseDraftSections("## Requirements\n- a\n- b\n");
  check(!meetsReviewThresholds(planless), "no plan never passes");
});

Deno.test("draft thresholds accept configured counts", () => {
  const sections = parseDraftSections("## Requirements\n- one\n\n## Implementation Plan\nplan\n");
  check(meetsReviewThresholds(sections, { requirements: 1, evidence: 0 }), "custom counts honored");
  check(!meetsReviewThresholds(sections, { requirements: 5, evidence: 7 }), "custom counts enforced");
});

Deno.test("go pattern matches approval and nothing else", () => {
  for (const text of ["go", "Go", "  go. ", "approve", "approved", "lgtm", "ship it!"]) {
    check(GO_PATTERN.test(text), `"${text}" is approval`);
  }
  for (const text of ["go on", "going well", "good", "stop", ""]) {
    check(!GO_PATTERN.test(text), `"${text}" is not approval`);
  }
});

Deno.test("content hash is stable hex", () => {
  const a = hashContent("same");
  check(a === hashContent("same"), "stable");
  check(/^[0-9a-f]{64}$/.test(a), "sha256 hex");
  check(a !== hashContent("different"), "content-bound");
});

Deno.test("plan splice replaces the section or appends it", () => {
  const doc = "## Requirements\n- one\n\n## Implementation Plan\nOld plan.\n\n## Evidence\n- e\n";
  const replaced = splicePlanSection(doc, "New plan.");
  check(replaced.includes("New plan.") && !replaced.includes("Old plan."), "section replaced");
  check(replaced.includes("## Evidence"), "later sections kept");
  const appended = splicePlanSection("## Requirements\n- one\n", "Fresh plan.");
  check(appended.includes("## Implementation Plan") && appended.includes("Fresh plan."), "section appended");
});

Deno.test("plan splice replaces a suffixed plan header instead of duplicating", () => {
  const doc = "# T\n\n## Implementation Plan — concrete redesign\n\nOld.\n\n## Evidence\n\nE.\n";
  const out = splicePlanSection(doc, "New plan.");
  const headers = out.split("\n").filter((line) => /^##\s+.*implementation plan/i.test(line));
  check(headers.length === 1, "exactly one plan section");
  check(out.includes("New plan.") && !out.includes("Old."), "plan body replaced");
  check(out.includes("## Evidence"), "later sections kept");
});

function staleBaseState(qid: Qid, basePlan: string): void {
  replaceState(createDraft(createQuest("req", qid), "mat"));
  updateState((s) => recordReviewResult(s, "FAIL", "old-target", "thin plan"));
  updateState((s) =>
    s.draft === null ? s : { ...s, draft: { ...s.draft, lastReviewedPlan: basePlan } }
  );
}

Deno.test("re-review material diffs against the stale base, not the plan being sent", () => {
  const qid = "mat001" as Qid;
  try {
    staleBaseState(qid, "step one\nstep two");
    const sections = parseDraftSections(
      "## Requirements\n- one\n- two\n\n## Implementation Plan\nstep one\nstep three\n",
    );
    const material = reviewMaterial(getState(), sections);
    check(material.planDiff !== undefined, "diff present");
    check(
      (material.planDiff ?? "").includes("- step two") &&
        (material.planDiff ?? "").includes("+ step three"),
      "diff spans old and new plan",
    );
    check(material.previousVerdict === "FAIL", "prior verdict travels");
    check(material.previousFindings === "thin plan", "prior findings travel");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("evidence-only revisions keep the prior verdict without a diff", () => {
  const qid = "mat002" as Qid;
  try {
    staleBaseState(qid, "step one");
    const sections = parseDraftSections("## Requirements\n- one\n\n## Implementation Plan\nstep one\n");
    const material = reviewMaterial(getState(), sections);
    check(material.planDiff === undefined, "identical plan has no diff");
    check(material.previousVerdict === "FAIL", "prior verdict still travels");
    check(material.previousFindings === "thin plan", "prior findings still travel");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("first reviews carry no continuity fields", () => {
  const qid = "mat003" as Qid;
  try {
    replaceState(createDraft(createQuest("req", qid), "mat"));
    const sections = parseDraftSections("## Requirements\n- one\n\n## Implementation Plan\nstep one\n");
    const material = reviewMaterial(getState(), sections);
    check(material.planDiff === undefined, "no diff on first review");
    check(material.previousVerdict === undefined, "no prior verdict on first review");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("draft sections accept numbered and plus bullets", () => {
  const sections = parseDraftSections(
    "## Requirements\n1. first thing\n2) second thing\n\n## Evidence\n+ saw it in the code\n1. measured it\n\n## Implementation Plan\nplan\n",
  );
  check(sections.requirements.length === 2, "numbered requirements parse");
  check(sections.requirements[0] === "first thing", "dot number stripped");
  check(sections.requirements[1] === "second thing", "paren number stripped");
  check(sections.evidence.length === 2, "plus and numbered evidence parse");
  check(meetsReviewThresholds(sections, { requirements: 5, evidence: 2 }), "parsed evidence counts toward the bar");
});

Deno.test("the 1x38Fd evidence shape survives to the brief", () => {
  const sections = parseDraftSections(
    "## Requirements\n- one\n- two\n\n## Evidence (7 file-backed items)\n1. `a.c:1` — first\n2. `b.c:2` — second\n\n## Implementation Plan\nplan\n",
  );
  check(sections.evidence.length === 2, "numbered items under a suffixed header parse");
  check(sections.evidence[0].includes("a.c:1"), "file:line citation kept");
});

Deno.test("drafting installer watches turn end", () => {  const pi = fakePi();
  watchDraftFileCatchAll(pi);
  check(pi.subscriptions.includes("turn_end"), "catch-all subscribed");
});

Deno.test("turn-end catch-all absorbs the scaffold silently, then blinks on bypass edits", async () => {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-catchall-"));
  const qid = "abc123" as Qid;
  replaceState(createDraft(createQuest("req", qid), "thing"));
  const file = join(cwd, draftPath(qid));
  await mkdir(dirname(file), { recursive: true });
  await writeFile(file, "# abc123\n\nScaffold.\n", "utf8");
  const calls: Array<string | undefined> = [];
  const ctx = fakeCtx(cwd, [], {
    setStatus: (_key: string, text: string | undefined) => {
      calls.push(text);
    },
  });
  const pi = fakePi();
  try {
    await onTurnEndCatchAll(pi, ctx);
    check(calls.length === 0, "scaffold absorption stays silent");
    check(getState().draft?.contentHash !== null, "baseline recorded");
    await writeFile(file, "# abc123\n\nScaffold.\n\nBypass edit.\n", "utf8");
    await onTurnEndCatchAll(pi, ctx);
    check(calls.length >= 1 && calls[0] === "\x1b[97m📝 abc123 [F2]\x1b[0m", "bypass edit flashes bright");
    check(getState().snapshotPending === true, "bypass edit marks snapshot pending");
  } finally {
    stopBlink();
    replaceState(IDLE_STATE);
  }
});

Deno.test("plan seeding adds the review-count marker and strips old ones", () => {
  const seeded = seedReviewCount("### Goal\nDo the work.");
  check(/^<!--\s*pi-quest:\s*review-count 0 -->/.test(seeded), "marker seeded at zero");
  check(seeded.includes("### Goal\nDo the work."), "plan body kept");
  const reseeded = seedReviewCount("<!-- pi-quest: review-count 9 -->\n### Goal\nDo the work.");
  check(/^<!--\s*pi-quest:\s*review-count 0 -->/.test(reseeded), "stale marker reset");
  check(!reseeded.includes("review-count 9"), "old marker gone");
});

Deno.test("bumpReviewCount increments the marker inside the plan section", () => {
  const doc = "## Requirements\n- one\n- two\n\n## Implementation Plan\n\n<!-- pi-quest: review-count 0 -->\n### Goal\nDo it.\n\n## Evidence\n- e\n";
  const bumped = bumpReviewCount(doc, 1);
  check(bumped.includes("<!-- pi-quest: review-count 1 -->"), "marker incremented");
  check(!bumped.includes("review-count 0"), "old marker replaced");
  check(bumped.includes("### Goal\nDo it."), "plan body preserved");
  check(bumped.includes("## Evidence"), "later sections kept");
  const markers = parseDraftSections(bumped).plan.match(/review-count/g)?.length ?? 0;
  check(markers === 1, "exactly one marker left");
  const empty = bumpReviewCount("## Requirements\n- one\n\n## Implementation Plan\n\n", 2);
  check(empty === "## Requirements\n- one\n\n## Implementation Plan\n\n", "an empty plan section is left alone");
});

interface RetryRecord {
  delay: number;
  fire: () => void;
}

interface RetryBus {
  pi: FakePi;
  emitted: Array<{ event: string; data: unknown }>;
  feed: (data: unknown) => void;
  schedules: RetryRecord[];
}

function retryBus(): RetryBus {
  const pi = fakePi();
  pi.toolNames = ["subagent"];
  const emitted: Array<{ event: string; data: unknown }> = [];
  const handlers = new Map<string, Array<(data: unknown) => void>>();
  (pi as { events: unknown }).events = {
    on: (event: string, handler: (data: unknown) => void) => {
      const list = handlers.get(event) ?? [];
      list.push(handler);
      handlers.set(event, list);
      return () => {};
    },
    emit: (event: string, data: unknown) => {
      emitted.push({ event, data });
    },
  };
  const feed = (data: unknown) => {
    for (const handler of handlers.get("prompt-template:subagent:response") ?? []) handler(data);
  };
  return { pi, emitted, feed, schedules: [] };
}

function requests(emitted: Array<{ event: string; data: unknown }>): Array<Record<string, unknown>> {
  return emitted
    .filter((e) => e.event === "prompt-template:subagent:request")
    .map((e) => e.data as Record<string, unknown>);
}

async function waitForRequests(
  emitted: Array<{ event: string; data: unknown }>,
  count: number,
): Promise<void> {
  for (let i = 0; i < 200 && requests(emitted).length < count; i += 1) {
    await new Promise((r) => setTimeout(r, 5));
  }
  check(requests(emitted).length >= count, `expected ${count} review attempts`);
}

function wakes(pi: FakePi): Array<SentMessage["message"]> {
  return pi.sent.filter((m) => m.options?.triggerTurn === true).map((m) => m.message);
}

// Each retry test gets its own qid: the retry counter is module-level
// (per-session in production) and must not leak between tests.
async function draftFailureSetup(qid: Qid): Promise<{ cwd: string; qid: Qid; file: string; doc: string; target: string }> {
  const cwd = mkdtempSync(join(tmpdir(), "pi-quest-retry-"));
  replaceState(createDraft(createQuest("req", qid), "mat"));
  const file = join(cwd, draftPath(qid));
  await mkdir(dirname(file), { recursive: true });
  const doc = "## Requirements\n- one\n- two\n\n## Evidence\n- found\n\n## Implementation Plan\n\n### Goal\nDo the work.\n";
  await writeFile(file, doc, "utf8");
  const target = hashContent(doc);
  // Mirror the real pipeline: maybeBootDraftReview marks planAuthored and the
  // content hash before any review boots direct at bootDraftReview.
  updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, planAuthored: true, contentHash: target },
  });
  return { cwd, qid, file, doc, target };
}

Deno.test("a failed draft review schedules a backoff retry and never self-approves", async () => {
  const d = await draftFailureSetup("ret001" as Qid);
  const bus = retryBus();
  const prior = setReviewRetryDispatcher((delay, fire) => {
    bus.schedules.push({ delay, fire });
  });
  try {
    const booted = bootDraftReview(bus.pi, fakeCtx(d.cwd), d.target, DEFAULT_CONFIG);
    await waitForRequests(bus.emitted, 1);
    bus.feed({ requestId: requests(bus.emitted)[0]["requestId"], status: "error", error: "boom" });
    await booted;
    check(bus.schedules.length === 1, "first retry scheduled");
    check(bus.schedules[0].delay === REVIEW_RETRY_BASE_MS, "base backoff");
    check(draftReviewRetryCount(d.qid) === 1, "retry count recorded");
    check(wakes(bus.pi).length === 0, "no wake while retrying");
    const onDisk = await readFile(d.file, "utf8");
    check(onDisk === d.doc, "plan not touched at scheduling time");
    check(getState().phase === "drafting", "still drafting");
  } finally {
    setReviewRetryDispatcher(prior);
    clearDraftReviewRetry("ret001" as Qid);
    replaceState(IDLE_STATE);
  }
});

Deno.test("the draft retry bumps the review count and a fresh review promotes on PASS", async () => {
  const d = await draftFailureSetup("ret002" as Qid);
  const bus = retryBus();
  const prior = setReviewRetryDispatcher((delay, fire) => {
    bus.schedules.push({ delay, fire });
  });
  try {
    const booted = bootDraftReview(bus.pi, fakeCtx(d.cwd), d.target, DEFAULT_CONFIG);
    await waitForRequests(bus.emitted, 1);
    bus.feed({ requestId: requests(bus.emitted)[0]["requestId"], status: "error", error: "boom" });
    await booted;
    await bus.schedules[0].fire();
    await waitForRequests(bus.emitted, 2);
    const bumpedDoc = await readFile(d.file, "utf8");
    check(bumpedDoc.includes("<!-- pi-quest: review-count 1 -->"), "marker bumped on retry");
    check(!bumpedDoc.includes("review-count 0"), "old marker replaced");
    check(getState().draft?.contentHash === hashContent(bumpedDoc), "content hash follows the bump");
    bus.feed({
      requestId: requests(bus.emitted)[1]["requestId"],
      status: "completed",
      result: { kind: "text", text: "VERDICT: PASS\nSEVERITY: NONE\nSUPPORTING FINDINGS:\n- sound plan" },
    });
    await new Promise((r) => setTimeout(r, 20));
    check(getState().phase === "implementing", "PASS after retry promotes");
    check(draftReviewRetryCount(d.qid) === 0, "retry counter cleared on verdict");
  } finally {
    setReviewRetryDispatcher(prior);
    clearDraftReviewRetry("ret002" as Qid);
    replaceState(IDLE_STATE);
  }
});

Deno.test("draft retries cap out and wake the agent without proceed-on-judgment", async () => {
  const d = await draftFailureSetup("ret003" as Qid);
  const bus = retryBus();
  const prior = setReviewRetryDispatcher((delay, fire) => {
    bus.schedules.push({ delay, fire });
  });
  try {
    const booted = bootDraftReview(bus.pi, fakeCtx(d.cwd), d.target, DEFAULT_CONFIG);
    await waitForRequests(bus.emitted, 1);
    bus.feed({ requestId: requests(bus.emitted)[0]["requestId"], status: "error", error: "boom" });
    await booted;
    for (let step = 1; step <= MAX_REVIEW_RETRIES; step += 1) {
      check(bus.schedules.length === step, `retry ${step} scheduled`);
      await bus.schedules[step - 1].fire();
      await waitForRequests(bus.emitted, step + 1);
      bus.feed({ requestId: requests(bus.emitted)[step]["requestId"], status: "error", error: "boom" });
      await new Promise((r) => setTimeout(r, 10));
    }
    check(bus.schedules.length === MAX_REVIEW_RETRIES, "no retry past the cap");
    const sentWakes = wakes(bus.pi);
    check(sentWakes.length === 1, "woken exactly once, at the cap");
    const text = String(sentWakes[0].content);
    check(text.includes(`failed ${MAX_REVIEW_RETRIES} times`), "cap named");
    check(!text.includes("proceed on your judgment"), "no self-judgment escape");
    check(draftReviewRetryCount(d.qid) === 0, "retry counter reset at cap");
    check(getState().phase === "drafting", "still drafting after the cap");
  } finally {
    setReviewRetryDispatcher(prior);
    clearDraftReviewRetry("ret003" as Qid);
    replaceState(IDLE_STATE);
  }
});

Deno.test("a FAIL verdict wake carries the verbatim review text and marks findings outstanding", async () => {
  const d = await draftFailureSetup("ret005" as Qid);
  const bus = retryBus();
  try {
    const booted = bootDraftReview(bus.pi, fakeCtx(d.cwd), d.target, DEFAULT_CONFIG);
    await waitForRequests(bus.emitted, 1);
    const reviewerText = "VERDICT: FAIL\nSEVERITY: MAJOR\nFINDINGS:\n- Issue: double-free\n  Evidence: respond_html frees page\n\nREQUIRED REVISIONS:\n- drop the trailing free(page)";
    bus.feed({
      requestId: requests(bus.emitted)[0]["requestId"],
      status: "completed",
      result: { kind: "text", text: reviewerText },
    });
    await booted;
    const sentWakes = wakes(bus.pi);
    check(sentWakes.length === 1, "FAIL wakes once");
    const text = String(sentWakes[0].content);
    check(text.includes("double-free"), "parsed findings in the wake");
    check(text.includes("REQUIRED REVISIONS"), "verbatim reviewer text in the wake");
    check(getState().draft?.outstandingFindings === true, "findings marked outstanding");
    check(getState().lastReview?.findings.includes("double-free") === true, "findings recorded in state");
    check(getState().lastReview?.reviewText?.includes("REQUIRED REVISIONS") === true, "review text recorded in state");
  } finally {
    replaceState(IDLE_STATE);
  }
});

Deno.test("a superseding save cancels the pending retry", async () => {
  const d = await draftFailureSetup("ret004" as Qid);
  const bus = retryBus();
  const prior = setReviewRetryDispatcher((delay, fire) => {
    bus.schedules.push({ delay, fire });
  });
  try {
    const booted = bootDraftReview(bus.pi, fakeCtx(d.cwd), d.target, DEFAULT_CONFIG);
    await waitForRequests(bus.emitted, 1);
    bus.feed({ requestId: requests(bus.emitted)[0]["requestId"], status: "error", error: "boom" });
    await booted;
    await writeFile(d.file, d.doc.replace("Do the work.", "Replanned approach."), "utf8");
    await bus.schedules[0].fire();
    await new Promise((r) => setTimeout(r, 10));
    check(bus.emitted.filter((e) => e.event === "prompt-template:subagent:request").length === 1, "no fresh boot over the superseding save");
    check(draftReviewRetryCount(d.qid) === 0, "retry cleared by the superseding save");
  } finally {
    setReviewRetryDispatcher(prior);
    clearDraftReviewRetry("ret004" as Qid);
    replaceState(IDLE_STATE);
  }
});
