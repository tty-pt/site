import { check } from "../check.ts";
import { reviewRunningNotice, shouldNoticeReview } from "../../src/review/flow.ts";

Deno.test("running notice orders end-of-turn and names the quest", () => {
  const text = reviewRunningNotice("abc123");
  check(text.includes("abc123"), "qid named");
  check(text.includes("end your turn"), "end-turn order kept");
  check(text.includes("verdict arrives as a new turn"), "wait semantics kept");
  check(text.length < 120, "token-cheap");
});

Deno.test("running notice fires once per review target", () => {
  check(shouldNoticeReview("abc123", "hash-a"), "first boot noticed");
  check(!shouldNoticeReview("abc123", "hash-a"), "repeat boot silent");
  check(shouldNoticeReview("abc123", "hash-b"), "new target noticed");
  check(shouldNoticeReview("def456", "hash-a"), "other quest noticed");
});
