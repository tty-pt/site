import { check } from "../check.ts";
import type { Qid } from "../../src/domain/qid.ts";
import {
  archive,
  claimComplete,
  createDraft,
  createQuest,
  demoteToImplementing,
  IDLE_STATE,
  noteDraftFindings,
  promote,
  recordHumanAnswer,
  recordRebuttal,
  recordRefinement,
  recordReviewResult,
  resolveDialogueRound,
} from "../../src/domain/quest.ts";

const QID = "abc123";

function provisional() {
  return createQuest("do the thing", QID);
}

function drafting() {
  return createDraft(provisional(), "thing");
}

function authored() {
  const s = drafting();
  if (s.draft === null) throw new Error("no draft");
  return { ...s, draft: { ...s.draft, planAuthored: true } };
}

Deno.test("quest creation records the verbatim request and blocks implementation", () => {
  const s = provisional();
  check(s.phase === "provisional", "phase provisional");
  check(s.qid === QID, "qid assigned at creation");
  check(s.pendingRootRequest === "do the thing", "verbatim request kept");
  check(s.snapshotPending, "creation marks pending");
});

Deno.test("quest creation rejects non-qids", () => {
  let threw = false;
  try {
    createQuest("x", "my-slug");
  } catch {
    threw = true;
  }
  check(threw, "slug qid throws");
});

Deno.test("quest lifecycle runs draft to archive", () => {
  const implemented = promote(authored(), "review");
  check(implemented.phase === "implementing", "promoted");
  check(implemented.draft?.approvedBy === "review", "approval path recorded");
  check(implemented.activeReview === null, "review cleared on promote");
  const validating = claimComplete(implemented);
  check(validating.phase === "validating", "validating");
  const back = demoteToImplementing(validating);
  check(back.phase === "implementing", "demoted");
  const done = archive(claimComplete(back), "COMPLETED");
  check(done.phase === "archived", "archived");
  check(done.archivedOutcome === "COMPLETED", "outcome recorded");
});

Deno.test("quest draft findings flag revision and clear approval", () => {
  const revised = noteDraftFindings({ ...authored(), draft: { ...authored().draft!, approvedBy: "user" as const } });
  check(revised.draft?.outstandingFindings === true, "findings flagged");
  check(revised.draft?.approvedBy === null, "approval cleared");
});

Deno.test("quest transitions reject wrong phases", () => {
  const cases: Array<() => void> = [
    () => createDraft(IDLE_STATE, "x"),
    () => promote(provisional(), "user"),
    () => promote(drafting(), "user"),
    () => claimComplete(drafting()),
    () => demoteToImplementing(drafting()),
    () => archive(drafting(), "FAILED"),
  ];
  for (const fn of cases) {
    let threw = false;
    try {
      fn();
    } catch {
      threw = true;
    }
    check(threw, "wrong-phase transition throws");
  }
});

Deno.test("quest every transition marks snapshot pending", () => {
  check(provisional().snapshotPending, "create pending");
  check(drafting().snapshotPending, "draft pending");
  check(promote(authored(), "user").snapshotPending, "promote pending");
  check(IDLE_STATE.snapshotPending === false, "idle clean");
});

Deno.test("quest records reviews, rebuttals, refinements, and answers", () => {
  const reviewed = recordReviewResult(authored(), "FAIL", "h1", "missing auth");
  check(reviewed.lastReview?.verdict === "FAIL", "review recorded");
  const { state: rebutted, round } = recordRebuttal(reviewed, "auth is in section 3 with tests", "FAIL", "missing auth");
  check(round === 1 && rebutted.reviewDialogue.length === 1, "dialogue round recorded");
  const resolved = resolveDialogueRound(rebutted, 1, "PASS");
  check(resolved.reviewDialogue[0].verdictAfter === "PASS", "round resolved");
  const refined = recordRefinement(resolved, "also cover retries");
  check(refined.refinements.length === 1, "refinement recorded");
  const answered = recordHumanAnswer(refined, "color?", "blue", true);
  check(answered.humanAnswers[0].late, "late answer recorded");
  let threw = false;
  try {
    recordRebuttal(reviewed, "short", "FAIL", "x");
  } catch {
    threw = true;
  }
  check(threw, "thin rebuttal rejected");
});

Deno.test("quest qid type brands strings", () => {
  const qid = "abc123" as Qid;
  check(typeof qid === "string", "qid is a string at runtime");
});
