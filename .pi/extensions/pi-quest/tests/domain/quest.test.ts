import { check } from "../check.ts";
import type { Qid } from "../../src/domain/qid.ts";
import {
  acknowledgeChild,
  addChild,
  settleChild,
} from "../../src/domain/children.ts";
import {
  createQuest,
  IDLE_STATE,
  recordHumanAnswer,
  recordRebuttal,
  recordRefinement,
  recordReviewResult,
  resolveDialogueRound,
} from "../../src/domain/quest.ts";
import {
  archive,
  claimComplete,
  createDraft,
  demoteToDrafting,
  demoteToImplementing,
  noteDraftFindings,
  promote,
  promoteToValidation,
} from "../../src/domain/transitions.ts";

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
    () => promoteToValidation(drafting(), "user"),
    () => claimComplete(drafting()),
    () => demoteToImplementing(drafting()),
    () => demoteToDrafting(drafting()),
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

Deno.test("quest kind defaults to standard and stays immutable per draft", () => {
  check(IDLE_STATE.kind === "standard", "idle is standard");
  const standard = drafting();
  check(standard.kind === "standard", "default kind standard");
  const analysis = createDraft(provisional(), "thing", "analysis");
  check(analysis.kind === "analysis", "explicit analysis kind stamped");
  check(analysis.draft?.planAuthored === false, "deliverable starts unauthored");
});

Deno.test("promoteToValidation claims straight to validating with the deliverable", () => {
  const s = createDraft(provisional(), "thing", "analysis");
  const authored = { ...s, draft: { ...s.draft!, planAuthored: true, contentHash: "h1" } };
  const midReview = { ...authored, activeReview: { kind: "draft" as const, target: "h1" } };
  const done = promoteToValidation(midReview, "review");
  check(done.phase === "validating", "skips implementing to validating");
  check(done.draft?.approvedBy === "review", "approval recorded");
  check(done.draft?.approvedPlanHash === "h1", "approved hash bound to the analysis");
  check(done.activeReview === null, "draft review cleared");
  check(done.snapshotPending, "marks pending");
});

Deno.test("demoteToDrafting returns an analysis quest to drafting with findings flagged", () => {
  const s = createDraft(provisional(), "thing", "analysis");
  const authored = { ...s, draft: { ...s.draft!, planAuthored: true, contentHash: "h1" } };
  const validating = promoteToValidation(authored, "review");
  const back = demoteToDrafting(validating);
  check(back.phase === "drafting", "demoted to drafting");
  check(back.draft?.outstandingFindings === true, "findings flagged");
  check(back.draft?.approvedBy === null, "approval cleared");
  check(back.activeReview === null, "review cleared");
  const alive = { ...validating, activeReview: { kind: "validation" as const, target: "h1" } };
  const liveBack = demoteToDrafting(alive);
  check(liveBack.draft?.outstandingFindings === true, "keeps contentHash, flags findings");
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

Deno.test("quest acknowledges returned children explicitly", () => {
  const linked = addChild(provisional(), {
    qid: "kid001" as Qid,
    brief: "slice",
    status: "running",
    findings: null,
    acknowledged: false,
  });
  const settled = settleChild(linked, "kid001" as Qid, "returned", "done");
  const acked = acknowledgeChild(settled, "kid001" as Qid);
  check(acked.children[0].acknowledged, "acknowledged");
  let threw = false;
  try {
    acknowledgeChild(linked, "kid001" as Qid);
  } catch {
    threw = true;
  }
  check(threw, "running child cannot be continued past");
  threw = false;
  try {
    acknowledgeChild(linked, "zzz999" as Qid);
  } catch {
    threw = true;
  }
  check(threw, "unknown child rejected");
});

Deno.test("quest qid type brands strings", () => {
  const qid = "abc123" as Qid;
  check(typeof qid === "string", "qid is a string at runtime");
});
