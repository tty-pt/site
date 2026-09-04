import { check } from "../check.ts";
import { parseReviewText } from "../../src/review/verdicts.ts";

const PASS_TEXT = `Reviewed independently.

VERDICT: PASS
SEVERITY: NONE

FINDINGS:
- Issue: none
  Evidence: checked
`;

const FAIL_TEXT = `Reviewed independently.

VERDICT: FAIL
SEVERITY: MAJOR

FINDINGS:
- Issue: plan skips auth
  Evidence: no auth step in plan
- Issue: no tests
  Evidence: test section empty

REQUIRED REVISIONS:
- add auth step
- add tests
`;

Deno.test("verdicts parse PASS and FAIL with findings", () => {
  const pass = parseReviewText(PASS_TEXT);
  check(pass.verdict === "PASS", "pass");
  check(pass.severity === "NONE", "severity kept");
  const fail = parseReviewText(FAIL_TEXT);
  check(fail.verdict === "FAIL", "fail");
  check(fail.severity === "MAJOR", "major kept");
  check(fail.findings.includes("plan skips auth"), "first finding kept");
  check(fail.findings.includes("add tests"), "revisions kept");
});

Deno.test("verdicts normalize legacy words and fail closed on garbage", () => {
  check(parseReviewText("VERDICT: APPROVE\n").verdict === "PASS", "approve maps");
  check(parseReviewText("VERDICT: REVISE\n").verdict === "FAIL", "revise maps");
  const empty = parseReviewText("   ");
  check(empty.verdict === "FAIL", "empty fails closed");
  const missing = parseReviewText("some rambling without a verdict line");
  check(missing.verdict === "FAIL", "missing fails closed");
  check(missing.findings.includes("no parseable VERDICT"), "guidance included");
  const uncertain = parseReviewText("VERDICT: UNCERTAIN\n");
  check(uncertain.verdict === "FAIL", "uncertain fails closed");
});

Deno.test("verdicts take the last verdict line and coerce severity", () => {
  const text = "VERDICT: PASS\nnotes\nVERDICT: FAIL\nSEVERITY: NONE\n";
  const parsed = parseReviewText(text);
  check(parsed.verdict === "FAIL", "last wins");
  check(parsed.severity === "MAJOR", "fail coerces NONE to MAJOR");
  const soft = parseReviewText("VERDICT: PASS\nSEVERITY: CRITICAL\n");
  check(soft.severity === "NONE", "pass coerces CRITICAL to NONE");
});
