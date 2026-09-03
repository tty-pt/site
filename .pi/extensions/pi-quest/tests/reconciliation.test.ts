import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import plugin, {
  auditQuestConsistency,
  type ConsistencyAuditResult,
  type StoredState,
} from "../index.ts";
import { questPath } from "../src/paths.ts";

function createMockExtensionAPI() {
  const handlers: Record<string, any[]> = {};
  const registeredTools: any[] = [];
  const registeredCommands: any[] = [];
  const userMessages: Array<{ msg: any; options?: any }> = [];

  const mockPi = {
    on: (event: string, handler: any) => {
      if (!handlers[event]) handlers[event] = [];
      handlers[event].push(handler);
    },
    registerTool: (tool: any) => {
      registeredTools.push(tool);
    },
    registerCommand: (name: string, cmd: any) => {
      registeredCommands.push({ name, ...cmd });
    },
    sendMessage: (msg: any, options?: any) => {
      userMessages.push({ msg, options });
    },
    sendUserMessage: (msg: any, options?: any) => {
      userMessages.push({ msg, options });
    },
    registerEntryRenderer: () => {},
  };

  return {
    mockPi,
    handlers,
    registeredTools,
    registeredCommands,
    userMessages,
  };
}

function createMockContext(tokens = 50000) {
  const branch: any[] = [];
  return {
    mode: "agent",
    hasUI: false,
    sessionManager: {
      getBranch: () => branch,
      appendCustomEntry: (_type: string, data: any) => {
        branch.push({ type: "custom", customType: "quest_journal", data });
      },
    },
    getContextUsage: () => ({ tokens, percent: (tokens / 800000) * 100 }),
    ui: {
      notify: () => {},
      input: async () => "",
      select: async () => "",
    },
  };
}

Deno.test("quest_journal_reconciliation: complete verification of durable-state reconciliation and consistency audit", async (t) => {
  const QUEST_DIR = ".pi/quest/current";
  await mkdir(QUEST_DIR, { recursive: true });

  // -----------------------------------------------------------------------
  // 1. Concrete Failure Case Detection from Prompt
  // -----------------------------------------------------------------------
  await t.step(
    "1. Concrete failure case: detected as contradictory and inconsistent",
    () => {
      const failureCaseMarkdown = `# Quest: i18n-locale-negotiation

## Goal
Add language support and locale negotiation.

## Original request
> Implement i18n locale constants and dictionary.

## Current Status
- [ ] in progress

## Current Understanding
- Dictionary keys need standard locale identifiers.

## Key Assumptions
- [ ] Header constants are sufficient for initial dictionary lookup.

## Open Questions & Uncertainties
- [ ] None remaining - user confirmed language defaults and negotiation strategy

## Research Findings
- i18n_dict.h defines language translation mappings.

## Plan Version
2

## Plan
1. Add constants to i18n_dict.h
2. Rerun unit tests

## Plan Confidence
high

## Plan Revisions
- Initial plan formulated.

## Latest Reassessment
- Added I18N_LOCALE_EN and I18N_LOCALE_PT definitions to i18n_dict.h.

## Rejected Approaches
-

## Execution Snapshot

### Completed
-

### In Progress
-

### Files Examined
-

### Files Modified
-

### Test / Build Status
-

### Remaining Work
- [ ] Add constants to i18n_dict.h and rerun unit tests

### Exact Next Action
Add constants to i18n_dict.h and rerun unit tests
`;

      const audit = auditQuestConsistency(failureCaseMarkdown);
      assert.strictEqual(
        audit.consistent,
        false,
        "Concrete failure case must be detected as inconsistent",
      );
      assert.ok(
        audit.issues.length >= 3,
        `Expected at least 3 consistency issues, got: ${
          JSON.stringify(audit.issues)
        }`,
      );

      // Issue 1: Exact Next Action repeats completed action
      const hasRepeatedAction = audit.issues.some((i) =>
        i.includes("repeats work already recorded as completed")
      );
      assert.ok(
        hasRepeatedAction,
        "Must detect that Exact Next Action repeats completed work from Reassessment",
      );

      // Issue 2: Files Modified is empty despite modified file in Reassessment
      const hasMissingFiles = audit.issues.some((i) =>
        i.includes("Files Modified is empty") || i.includes("i18n_dict.h")
      );
      assert.ok(
        hasMissingFiles,
        "Must detect that Files Modified is empty despite i18n_dict.h being modified",
      );

      // Issue 3: Completed is empty despite completed action in Reassessment
      const hasEmptyCompleted = audit.issues.some((i) =>
        i.includes("Completed section is empty")
      );
      assert.ok(
        hasEmptyCompleted,
        "Must detect that Completed is empty despite completed addition in Reassessment",
      );

      // Issue 4: Plan Version 2 with only initial plan formulated
      const hasPlanVersionMismatch = audit.issues.some((i) =>
        i.includes(
          "Plan Version is 2 but Plan Revisions only lists initial plan",
        )
      );
      assert.ok(
        hasPlanVersionMismatch,
        "Must detect Plan Version 2 without corresponding plan revision rationale",
      );

      // Issue 5: User confirmation claimed as eliminating all uncertainties while unverified assumptions remain
      const hasUserConfIssue = audit.issues.some((i) =>
        i.includes(
          "Open Questions & Uncertainties claims no uncertainties based solely on user",
        )
      );
      assert.ok(
        hasUserConfIssue,
        "Must detect user confirmation improperly claimed as eliminating engineering uncertainties",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 2. Reconciled Durable State Verification
  // -----------------------------------------------------------------------
  await t.step(
    "2. Reconciled durable state: all fields synchronized and consistent",
    () => {
      const reconciledMarkdown = `# Quest: i18n-locale-negotiation

## Goal
Add language support and locale negotiation.

## Original request
> Implement i18n locale constants and dictionary.

## Current Status
- [ ] in progress

## Current Understanding
- Dictionary keys need standard locale identifiers.

## Key Assumptions
- [ ] HTTP Accept-Language negotiation hooks into axil-auth correctly.

## Open Questions & Uncertainties
- [ ] How will WASM client sync locale preferences across hydration boundaries?

## Research Findings
- i18n_dict.h defines language translation mappings.

## Plan Version
2

## Plan
1. Add locale constants to i18n_dict.h
2. Rerun unit tests and verify constant definitions
3. Implement HTTP Accept-Language parser

## Plan Confidence
medium
Reason:
Initial header constants added, but HTTP negotiation and SSR/WASM sync remain unverified.

## Plan Revisions
- Previous plan assumed static dict -> discovery showed dynamic locale selection needed -> revised plan to add negotiation hooks.

## Latest Reassessment
- Added I18N_LOCALE_EN and I18N_LOCALE_PT definitions to i18n_dict.h.

## Rejected Approaches
- Hardcoding locale strings directly in UX modules.

## Execution Snapshot

### Completed
- Added I18N_LOCALE_EN and I18N_LOCALE_PT definitions to i18n_dict.h

### In Progress
- [ ] Rerun i18n unit tests and verify locale constants

### Files Examined
- i18n_dict.h
- mods/auth/auth.c

### Files Modified
- i18n_dict.h

### Test / Build Status
- Unit tests pending rerun after i18n_dict.h constant additions.

### Remaining Work
- [ ] Rerun i18n unit tests and verify locale constants
- [ ] Implement HTTP Accept-Language parser

### Exact Next Action
Run the i18n unit tests that exercise the locale constants and inspect any failures before proceeding to UX integration.
`;

      const audit = auditQuestConsistency(reconciledMarkdown);
      assert.strictEqual(
        audit.consistent,
        true,
        `Reconciled markdown must be consistent, got issues: ${
          JSON.stringify(audit.issues)
        }`,
      );
      assert.strictEqual(audit.issues.length, 0);
    },
  );

  // -----------------------------------------------------------------------
  // 3. Live Pointer Invariant: Exact Next Action Must Advance
  // -----------------------------------------------------------------------
  await t.step(
    "3. Live pointer invariant: Exact Next Action cannot repeat completed tasks",
    () => {
      const repetitiveMarkdown = `# Quest: query-parsing

## Current Status
- [ ] in progress

## Plan Version
1

## Execution Snapshot

### Completed
- Implemented parse_query_params in src/query.c

### Files Modified
- src/query.c

### Test / Build Status
- Tests pending rerun

### Remaining Work
- [ ] Run test_query suite

### Exact Next Action
Implement parse_query_params in src/query.c
`;

      const audit1 = auditQuestConsistency(repetitiveMarkdown);
      assert.strictEqual(audit1.consistent, false);
      assert.ok(
        audit1.issues.some((i) =>
          i.includes("repeats work already recorded as completed")
        ),
      );

      const advancedMarkdown = repetitiveMarkdown.replace(
        "Implement parse_query_params in src/query.c",
        "Run test_query suite with make test and inspect results",
      );
      const audit2 = auditQuestConsistency(advancedMarkdown);
      assert.strictEqual(
        audit2.consistent,
        true,
        `Advanced live pointer should be consistent, got: ${
          JSON.stringify(audit2.issues)
        }`,
      );
    },
  );

  // -----------------------------------------------------------------------
  // 4. Meaningful Plan Versions & Plan Revisions
  // -----------------------------------------------------------------------
  await t.step(
    "4. Meaningful Plan Versions: Version > 1 requires explained revision",
    () => {
      const v2WithoutRevision = `# Quest: auth-flow
## Plan Version
2
## Plan Revisions
- Initial plan formulated.
## Execution Snapshot
### Completed
-
### Remaining Work
- [ ] Setup OAuth
### Exact Next Action
Investigate OAuth callback path
`;
      const audit1 = auditQuestConsistency(v2WithoutRevision);
      assert.strictEqual(audit1.consistent, false);
      assert.ok(
        audit1.issues.some((i) =>
          i.includes(
            "Plan Version is 2 but Plan Revisions only lists initial plan",
          )
        ),
      );

      const v2WithRevision = v2WithoutRevision.replace(
        "- Initial plan formulated.",
        "- Previous plan: monolithic callback -> Contradiction: PKCE requires token exchange step -> New understanding: split into initiate and exchange -> Revised plan: multi-step OAuth",
      );
      const audit2 = auditQuestConsistency(v2WithRevision);
      assert.strictEqual(
        audit2.consistent,
        true,
        `Revision explanation must pass audit, got: ${
          JSON.stringify(audit2.issues)
        }`,
      );
    },
  );

  // -----------------------------------------------------------------------
  // 5. User Decisions vs Engineering Invariants
  // -----------------------------------------------------------------------
  await t.step(
    "5. User requirement confirmation does not eliminate engineering uncertainties",
    () => {
      const conflatedMarkdown = `# Quest: ux-theming
## Plan Version
1
## Key Assumptions
- [ ] CSS custom properties dynamically resolve inside WASM shadow DOM.
## Open Questions & Uncertainties
- None remaining - user confirmed the dark theme palette.
## Execution Snapshot
### Completed
-
### Remaining Work
- [ ] Verify shadow DOM CSS variables
### Exact Next Action
Test shadow DOM variables in browser
`;
      const audit = auditQuestConsistency(conflatedMarkdown);
      assert.strictEqual(audit.consistent, false);
      assert.ok(
        audit.issues.some((i) =>
          i.includes(
            "claims no uncertainties based solely on user requirement confirmation",
          )
        ),
      );
    },
  );

  // -----------------------------------------------------------------------
  // 6. Test / Build Status Staleness after File Modifications
  // -----------------------------------------------------------------------
  await t.step("6. Modified files require explicit Test / Build Status", () => {
    const staleTestStatusMarkdown = `# Quest: song-cache
## Plan Version
1
## Execution Snapshot
### Completed
- Added LRU cache to mods/song/song.c
### Files Modified
- mods/song/song.c
### Test / Build Status
-
### Remaining Work
- [ ] Verify cache hit rate
### Exact Next Action
Run cache benchmark tests
`;
    const audit = auditQuestConsistency(staleTestStatusMarkdown);
    assert.strictEqual(audit.consistent, false);
    assert.ok(
      audit.issues.some((i) =>
        i.includes("Files were modified but Test / Build Status is empty")
      ),
    );
  });

  // -----------------------------------------------------------------------
  // 7. Structured State Reconciliation via quest_update_state
  // -----------------------------------------------------------------------
  await t.step(
    "7. quest_update_state synchronizes completed, filesModified, testStatus, and nextAction",
    async () => {
      const { mockPi, registeredTools } = createMockExtensionAPI();
      plugin(mockPi as any);
      const mockCtx = createMockContext();

      const updateTool = registeredTools.find((t) =>
        t.name === "quest_update_state"
      );
      assert.ok(updateTool, "quest_update_state tool must be registered");

      const res: any = await updateTool.execute(
        "call_1",
        {
          name: "reconciliation-test-quest",
          goal: "Verify structured field reconciliation",
          understanding: "Understanding of reconciliation semantics",
          assumptions: ["Assumptions verified"],
          openQuestions: ["No open blockers"],
          findings: ["Findings logged"],
          plan: ["Step 1", "Step 2"],
          planConfidence: "high",
          researchComplete: true,
          completed: [
            "Added I18N_LOCALE_EN and I18N_LOCALE_PT definitions to i18n_dict.h",
          ],
          filesModified: ["i18n_dict.h"],
          testStatus: "Unit tests pending rerun after header edits",
          remaining: ["Rerun unit tests and verify locale constants"],
          exactNextAction:
            "Run the i18n unit tests and verify locale constants",
        },
        undefined,
        undefined,
        mockCtx,
      );

      assert.ok(res.details?.hash, "Update state must succeed and return hash");
      const writtenContent = await readFile(
        res.details?.path || questPath("reconciliation-test-quest"),
        "utf8",
      );
      assert.ok(
        writtenContent.includes("Added I18N_LOCALE_EN and I18N_LOCALE_PT"),
        "Completed text must be written",
      );
      assert.ok(
        writtenContent.includes("i18n_dict.h"),
        "Files modified must be written",
      );
      assert.ok(
        writtenContent.includes("Unit tests pending rerun"),
        "Test status must be written",
      );
      assert.ok(
        writtenContent.includes("Run the i18n unit tests and verify"),
        "Exact next action must be written",
      );

      const audit = auditQuestConsistency(writtenContent);
      assert.strictEqual(
        audit.consistent,
        true,
        `Generated quest file must be internally consistent, got: ${
          JSON.stringify(audit.issues)
        }`,
      );
    },
  );

  // -----------------------------------------------------------------------
  // 8. verifyAndMarkSaved integration with Consistency Audit
  // -----------------------------------------------------------------------
  await t.step(
    "8. quest_mark_saved reports consistency audit notices on contradictory markdown",
    async () => {
      const { mockPi, registeredTools } = createMockExtensionAPI();
      plugin(mockPi as any);
      const mockCtx = createMockContext();

      const qid = "contradictory-quest-qid";
      await mkdir(`.pi/quest/current/${qid}`, { recursive: true });
      const contradictoryPath = `.pi/quest/current/${qid}/quest.md`;
      await writeFile(
        contradictoryPath,
        `# Quest: contradictory-quest

## Plan Version
1

## Latest Reassessment
- Added I18N_LOCALE_EN to i18n_dict.h.

## Execution Snapshot
### Completed
-

### Files Modified
-

### Test / Build Status
-

### Remaining Work
- [ ] Add constants to i18n_dict.h

### Exact Next Action
Add constants to i18n_dict.h
`,
        "utf8",
      );

      const markTool = registeredTools.find((t) =>
        t.name === "quest_mark_saved"
      );
      assert.ok(markTool, "quest_mark_saved tool must be registered");

      const res: any = await markTool.execute(
        "call_mark_1",
        { name: "contradictory-quest" },
        undefined,
        undefined,
        mockCtx,
      );

      assert.ok(
        res.content[0].text.includes("Consistency Audit Notice"),
        "Must report consistency audit notice on contradictory markdown",
      );
      assert.strictEqual(
        res.details?.consistency?.consistent,
        false,
        "Consistency result in details must be false",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 9. Session File Mutation Tracking
  // -----------------------------------------------------------------------
  await t.step(
    "9. Session file mutation tracking identifies omitted modified files",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      plugin(mockPi as any);
      const mockCtx = createMockContext();

      const questSlug = "session-mod-test";
      const qid = "session-mod-qid";
      await mkdir(`.pi/quest/current/${qid}`, { recursive: true });
      const questFilePath = `.pi/quest/current/${qid}/quest.md`;
      await writeFile(
        questFilePath,
        `# Quest: ${questSlug}
## Plan Version
1
## Execution Snapshot
### Completed
- Added header
### Files Modified
-
### Test / Build Status
- Tests pending
### Remaining Work
- [ ] Test
### Exact Next Action
Run tests
`,
        "utf8",
      );

      // Activate quest
      for (const cb of handlers["session_start"] || []) {
        await cb({}, mockCtx);
      }

      // Tool result for edit on project file
      for (const cb of handlers["tool_result"] || []) {
        await cb(
          { toolName: "edit", input: { path: "mods/core/core.c" } },
          mockCtx,
        );
      }

      const content = await readFile(questFilePath, "utf8");
      const audit = auditQuestConsistency(content, {
        recentModifiedFiles: ["mods/core/core.c"],
      });
      assert.strictEqual(
        audit.consistent,
        false,
        "Audit must detect that modified session file mods/core/core.c is missing from Files Modified",
      );
      assert.ok(
        audit.issues.some((i) =>
          i.includes("core/core.c") || i.includes("mods/core/core.c")
        ),
      );
    },
  );

  // Clean up
  await rm(QUEST_DIR, { recursive: true, force: true });
});
