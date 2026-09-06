import { check } from "../check.ts";
import { createQuest } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { buildReviewPrompt } from "../../src/review/prompts.ts";
import { implementationFingerprint } from "../../src/review/flow.ts";

const QID = "abc123" as Qid;

Deno.test("review prompts carry brief, rules, and verdict format", () => {
  const prompt = buildReviewPrompt("draft", QID, "hash-1", {
    objective: "ship it",
    plan: "do things",
    evidence: ["saw it"],
    amendments: [],
  });
  check(prompt.includes("abc123"), "qid present");
  check(prompt.includes("hash-1"), "target present");
  check(prompt.includes("ship it"), "objective present");
  check(prompt.includes("strictly read-only"), "read-only rules present");
  check(prompt.includes("VERDICT: PASS | FAIL"), "verdict format present");
  const validation = buildReviewPrompt("validation", QID, "snap-9", {
    objective: "ship it",
    plan: "did things",
    evidence: [],
    amendments: ["changed x (because y)"],
    implementationSummary: "done",
  });
  check(validation.includes("IMPLEMENTATION"), "validation material present");
  check(validation.includes("changed x"), "amendments present");
});

Deno.test("draft brief carries the maturity bar", () => {
  const prompt = buildReviewPrompt("draft", QID, "h1", {
    objective: "ship it",
    plan: "do things",
    evidence: [],
    amendments: [],
  });
  check(prompt.includes("MATURITY BAR"), "maturity section present");
  check(prompt.includes("2 requirements"), "threshold numbers present");
  const custom = buildReviewPrompt("draft", QID, "h1", {
    objective: "ship it",
    plan: "do things",
    evidence: [],
    amendments: [],
  }, { requirements: 3, evidence: 10 });
  check(custom.includes("3 requirements") && custom.includes("10 evidence"), "configured bar honored");
  const validation = buildReviewPrompt("validation", QID, "s1", {
    objective: "ship it",
    plan: "did things",
    evidence: [],
    amendments: [],
  });
  check(!validation.includes("MATURITY BAR"), "validation has no maturity bar");
});

Deno.test("re-review briefs carry the diff plus prior findings", () => {
  const prompt = buildReviewPrompt("draft", QID, "hash-2", {
    objective: "ship it",
    plan: "do things\nthen verify",
    evidence: [],
    amendments: [],
    planDiff: "- do things\n+ do things\n+ then verify",
    previousVerdict: "FAIL",
    previousFindings: "no verification step",
  });
  check(prompt.includes("CHANGES SINCE LAST REVIEW"), "diff section present");
  check(prompt.includes("then verify"), "diff content present");
  check(prompt.includes("PRIOR VERDICT: FAIL"), "prior verdict present");
  check(prompt.includes("no verification step"), "prior findings present");
  check(prompt.includes("a prior FAIL presumes nothing"), "independence preserved");
  const fresh = buildReviewPrompt("draft", QID, "hash-1", {
    objective: "ship it",
    plan: "do things",
    evidence: [],
    amendments: [],
  });
  check(!fresh.includes("CHANGES SINCE LAST REVIEW"), "first review has no diff section");
  check(!fresh.includes("PRIOR VERDICT"), "first review has no prior section");
});
Deno.test("empty sections render self-describing, never a bare none", () => {
  const prompt = buildReviewPrompt("draft", QID, "h1", {
    objective: "ship it",
    plan: "do things",
    evidence: [],
    amendments: [],
  });
  check(!prompt.includes("(none)"), "no bare none for reviewers to quote");
  check(prompt.includes("no evidence items recorded"), "empty evidence labeled");
});

Deno.test("output format splits PASS support from FAIL revisions", () => {
  const draft = buildReviewPrompt("draft", QID, "h1", {
    objective: "ship it",
    plan: "do things",
    evidence: [],
    amendments: [],
  });
  check(draft.includes("ADVISORIES"), "PASS branch offers advisories");
  check(draft.includes("REQUIRED REVISIONS"), "FAIL branch keeps revisions");
  check(draft.includes("approval is unconditional"), "PASS approval unconditional");
  const validation = buildReviewPrompt("validation", QID, "s1", {
    objective: "ship it",
    plan: "did things",
    evidence: [],
    amendments: [],
  });
  check(validation.includes("ADVISORIES"), "validation shares the PASS branch");
  check(validation.includes("REQUIRED REVISIONS"), "validation shares the FAIL branch");
});

Deno.test("implementation fingerprint is stable and content-bound", () => {
  const a = createQuest("same", "abc123");
  const b = createQuest("same", "abc123");
  check(implementationFingerprint(a) === implementationFingerprint(b), "stable");
  check(/^[0-9a-f]{64}$/.test(implementationFingerprint(a)), "sha256 hex");
  check(
    implementationFingerprint({ ...a, exactNextAction: "different" }) !== implementationFingerprint(a),
    "content-bound",
  );
});
