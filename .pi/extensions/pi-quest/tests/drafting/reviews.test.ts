import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { mkdir, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { dirname, join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { draftPath } from "../../src/domain/paths.ts";
import { stopBlink } from "../../src/durability/index.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";
import {
  GO_PATTERN,
  hashContent,
  meetsReviewThresholds,
  onTurnEndCatchAll,
  parseDraftSections,
  splicePlanSection,
  watchDraftFileCatchAll,
} from "../../src/drafting/reviews.ts";

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

Deno.test("drafting installer watches turn end", () => {
  const pi = fakePi();
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
