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

Deno.test("verdicts parse PASS and FAIL with findings", () => {  const pass = parseReviewText(PASS_TEXT);
  check(pass.verdict === "PASS", "pass");
  check(pass.severity === "NONE", "severity kept");
  const fail = parseReviewText(FAIL_TEXT);
  check(fail.verdict === "FAIL", "fail");
  check(fail.severity === "MAJOR", "major kept");
  check(fail.findings.includes("plan skips auth"), "first finding kept");
  check(fail.findings.includes("add tests"), "revisions kept");
});

const ADVISORY_PASS_TEXT = `Reviewed independently.

VERDICT: PASS
SEVERITY: NONE

SUPPORTING FINDINGS:
- Issue: plan is evidence-rich
  Evidence: file:line citations throughout

ADVISORIES:
- confirm the Phase D choice before implementing
- run boundary scripts after Phase A
`;

Deno.test("PASS advisories parse apart from findings", () => {
  const pass = parseReviewText(ADVISORY_PASS_TEXT);
  check(pass.verdict === "PASS", "pass");
  check(pass.findings.includes("evidence-rich"), "support kept in findings");
  check(!pass.findings.includes("Phase D"), "homework kept out of findings");
  check(pass.advisories.includes("Phase D"), "first advisory kept");
  check(pass.advisories.includes("boundary scripts"), "second advisory kept");
});

Deno.test("old-style PASS with revisions still parses", () => {
  const legacy = parseReviewText(`${PASS_TEXT}\nREQUIRED REVISIONS:\n- confirm this\n`);
  check(legacy.verdict === "PASS", "pass");
  check(legacy.findings.includes("confirm this"), "legacy revisions retained");
  check(legacy.advisories === "", "no advisories section, empty field");
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

// The real reviewers write prose headers ("### Findings", "## Required
// revisions") and markdown bullets ("- **Finding A (P0 — …).**"). The parser
// must collect those, not return the "no itemized findings" fallback — a rich
// FAIL dying to the fallback is exactly what caused the lost-verdict rewrite.
const PROSE_FAIL_TEXT = `## Review
I verified every claim in the tree.

### What the plan gets right
- correct seam identified

### Findings
- **Finding A (P0 — blocks Phase 1).** The responder has a double-free that the plan neither notes nor fixes.
- **Finding B (P1).** The plan never reconciles the two dead responders.
- **Finding D (P2).** The cited line ranges are wrong: sb_load_song_row is at gig.c:751, not 403-520.

## Required revisions
1. **Fix the double-free before activation.** Remove the redundant free(page) after respond_html.
2. **Reconcile the two dead responders.** State which single function is the target.
3. **Correct the Phase 2 line citations.**

VERDICT: FAIL
SEVERITY: MAJOR
`;

Deno.test("prose headers and markdown bullets are collected as findings", () => {
  const parsed = parseReviewText(PROSE_FAIL_TEXT);
  check(parsed.verdict === "FAIL", "verdict parsed");
  check(parsed.severity === "MAJOR", "severity parsed");
  check(parsed.findings.includes("double-free"), "Finding A captured");
  check(parsed.findings.includes("gig.c:751"), "Finding D detail captured");
  check(parsed.findings.includes("Remove the redundant free(page)"), "revision 1 captured");
  check(parsed.findings.includes("line citations"), "revision 3 captured");
});

Deno.test("the verbatim review text travels with the parsed review", () => {
  const noOutput = parseReviewText("   ");
  check(noOutput.text === "", "empty run carries no text");
  const missing = parseReviewText("rambling without a verdict");
  check(missing.text.includes("rambling"), "unverdictable text still travels");
  const parsed = parseReviewText(PROSE_FAIL_TEXT);
  check(parsed.text.includes("double-free"), "full text preserved");
  check(parsed.text.includes("VERDICT: FAIL"), "verdict lines first under truncation");
  const huge = parseReviewText(`VERDICT: PASS\nSEVERITY: NONE\n${"x".repeat(20000)}`);
  check(huge.text.length < 8500, "text clipped to the budget");
  check(huge.text.includes("VERDICT: PASS"), "verdict survives clipping");
});
