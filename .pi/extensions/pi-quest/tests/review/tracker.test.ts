import { check } from "../check.ts";
import {
  cancelReview,
  hasInFlight,
  isCurrentReview,
  isReviewerSession,
  noteReviewerSession,
  settleReview,
  supersedeReviewThenBootFresh,
  trackReview,
} from "../../src/review/tracker.ts";

Deno.test("tracker supersede cancels the old review and boots fresh", () => {
  let aborted = 0;
  let booted: string[] = [];
  trackReview("qid-1", "rev-a", () => {
    aborted += 1;
  });
  check(isCurrentReview("qid-1", "rev-a"), "tracked");
  supersedeReviewThenBootFresh("qid-1", "rev-b", () => {
    booted.push("rev-b");
  });
  check(aborted === 1, "old review aborted");
  check(booted.length === 1, "fresh review booted");
  check(!isCurrentReview("qid-1", "rev-a"), "old target stale");
  check(isCurrentReview("qid-1", "rev-b"), "new target current");
  cancelReview("qid-1");
});

Deno.test("tracker settle accepts current and rejects stale results", () => {
  trackReview("qid-2", "rev-a", () => {});
  check(!settleReview("qid-2", "rev-old"), "stale rejected");
  check(isCurrentReview("qid-2", "rev-a"), "current survives stale settle");
  check(settleReview("qid-2", "rev-a"), "current accepted");
  check(!isCurrentReview("qid-2", "rev-a"), "settled gone");
});

Deno.test("tracker cancel is safe on unknown qids and throwing aborts", () => {
  cancelReview("never-tracked");
  trackReview("qid-3", "rev-a", () => {
    throw new Error("abort blew up");
  });
  cancelReview("qid-3");
  check(!isCurrentReview("qid-3", "rev-a"), "entry gone despite abort error");
});

Deno.test("tracker knows in-flight reviews and reviewer sessions", () => {
  trackReview("qid-4", "rev-a", () => {});
  check(hasInFlight("qid-4"), "in flight");
  check(!isReviewerSession("child-1"), "unknown session not a reviewer");
  noteReviewerSession("qid-4", "child-1");
  check(isReviewerSession("child-1"), "recorded child session matches");
  cancelReview("qid-4");
  check(!hasInFlight("qid-4"), "settled on cancel");
});
