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
