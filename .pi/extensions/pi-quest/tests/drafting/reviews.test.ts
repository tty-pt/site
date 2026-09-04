import { check } from "../check.ts";
import { GO_PATTERN, hashContent, meetsReviewThresholds, parseDraftSections } from "../../src/drafting/reviews.ts";

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
