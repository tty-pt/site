import assert from "node:assert";
import { existsSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { QuestErrorCode } from "../src/constants.ts";
import { analyzeTurnToolResults } from "../src/hooks.ts";
import {
  clearQuestLog,
  formatContextFields,
  formatLogEntry,
  getQuestLogPath,
  isQuestLoggingDegraded,
  logAgentMessageTransition,
  logCompactionTransition,
  logContinuationAnomaly,
  logEvent,
  logGateTransition,
  logImplementationOutcome,
  logPersistenceTransition,
  logQuestTransition,
  logReassessmentTransition,
  logRecoveryTransition,
  logResearchTransition,
  logResumeTransition,
  logStateUpdateTransition,
  logSubquestTransition,
  logToolAnomaly,
  logToolFailure,
  logTurnBoundary,
  logUserInteraction,
  logVerificationTransition,
  mapEventTypeToMajorPhase,
  parseLogEntry,
  QuestLogContext,
  QuestLogEventType,
  readQuestLog,
  resetQuestLoggingDegraded,
  sanitizeLogString,
  summarizeQuestJournalLog,
} from "../src/logging.ts";
import { state } from "../src/state.ts";
import { logError, reportAgentError } from "../src/messaging.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("persistent_logging: formatting, parsing, safety, and run summarizer", async (t) => {
  const testQid = "test_format_qid";
  state.questId = testQid;
  const tempLogPath = getQuestLogPath(testQid);
  clearQuestLog(tempLogPath);

  await t.step("sanitizeLogString strips newlines and caps length", () => {
    const raw = "First line\nSecond line\r\nThird line\twith tabs";
    const clean = sanitizeLogString(raw, 50);
    assert.strictEqual(clean, "First line Second line Third line with tabs");

    const longText = "a".repeat(400);
    const capped = sanitizeLogString(longText, 50);
    assert.strictEqual(capped.length, 50);
    assert.ok(capped.endsWith("..."));
  });

  await t.step("formatContextFields formats priority keys first", () => {
    const ctx: QuestLogContext = {
      customField: "customVal",
      tool: "edit",
      path: "src/foo.ts",
      round: 2,
      substantive: true,
      toolsUsed: 3,
      mutations: 1,
      failures: 0,
    };
    const formatted = formatContextFields(ctx);
    assert.ok(formatted.includes("round=2"));
    assert.ok(formatted.includes("tool=edit"));
    assert.ok(formatted.includes("path=src/foo.ts"));
    assert.ok(formatted.includes("substantive=true"));
    assert.ok(formatted.includes("toolsUsed=3"));
    assert.ok(formatted.includes("mutations=1"));
    assert.ok(formatted.includes("customField=customVal"));
  });

  await t.step("formatLogEntry and parseLogEntry round-trip correctly", () => {
    const ts = "2026-08-31T00:41:12.421Z";
    const line = formatLogEntry(
      "RESEARCH_EVIDENCE",
      "inspecting execution paths",
      {
        quest: "test-quest",
        round: 1,
        reads: 3,
        searches: 2,
        kind: "code-search",
      },
      ts,
    );

    assert.strictEqual(
      line,
      "2026-08-31T00:41:12.421Z | RESEARCH_EVIDENCE | quest=test-quest | round=1 kind=code-search reads=3 searches=2 | inspecting execution paths",
    );

    const parsed = parseLogEntry(line);
    assert.ok(parsed);
    assert.strictEqual(parsed.timestamp, "2026-08-31T00:41:12.421Z");
    assert.strictEqual(parsed.type, "RESEARCH_EVIDENCE");
    assert.strictEqual(parsed.quest, "test-quest");
    assert.strictEqual(parsed.context.round, "1");
    assert.strictEqual(parsed.context.kind, "code-search");
    assert.strictEqual(parsed.context.reads, "3");
    assert.strictEqual(parsed.context.searches, "2");
    assert.strictEqual(parsed.message, "inspecting execution paths");
  });

  await t.step(
    "lifecycle helpers write structured log entries across all 18 categories",
    () => {
      clearQuestLog(tempLogPath);

      logQuestTransition("QUEST_DETECTED", "substantive root prompt detected", {
        quest: "",
        reason: "new_prompt",
      });
      logQuestTransition("QUEST_CREATED", "provisional root quest created", {
        quest: "auth-flow",
      });
      logTurnBoundary("TURN_START", "turn started", { quest: "auth-flow" });
      logGateTransition("GATE_BLOCKED", "research pending", {
        quest: "auth-flow",
        gate: "RESEARCH_PENDING",
        reason: "research required",
      });
      logResearchTransition("RESEARCH_REQUIRED", "research required", {
        quest: "auth-flow",
        round: 1,
      });
      logResearchTransition("RESEARCH_EVIDENCE", "evidence observed", {
        quest: "auth-flow",
        kind: "code-search",
        reads: 2,
      });
      logResearchTransition("RESEARCH_COMPLETED", "research completed", {
        quest: "auth-flow",
        round: 1,
      });
      logGateTransition("GATE_OPENED", "gate opened", {
        quest: "auth-flow",
        to: "CONFIRMATION_PENDING",
      });
      logUserInteraction("CONFIRMATION_REQUESTED", "awaiting confirmation", {
        quest: "auth-flow",
      });
      logUserInteraction("CONFIRMATION_RECEIVED", "confirmation received", {
        quest: "auth-flow",
      });
      logGateTransition("GATE_OPENED", "implementation gate opened", {
        quest: "auth-flow",
        to: "IMPLEMENTATION_ALLOWED",
      });
      logImplementationOutcome(
        "IMPLEMENTATION_ATTEMPT",
        "attempted edit src/auth.ts",
        { quest: "auth-flow", tool: "edit", path: "src/auth.ts" },
      );
      logImplementationOutcome(
        "IMPLEMENTATION_COMPLETED",
        "completed edit src/auth.ts",
        { quest: "auth-flow", tool: "edit", path: "src/auth.ts" },
      );
      logVerificationTransition("TEST_STARTED", "running tests", {
        quest: "auth-flow",
        command: "make test",
      });
      logVerificationTransition("TEST_FAILED", "3 failed tests", {
        quest: "auth-flow",
        command: "make test",
        reason: "3 failed tests",
      });
      logReassessmentTransition("REASSESSMENT_REQUIRED", "test failure", {
        quest: "auth-flow",
        reason: "test failure",
        version: 1,
      });
      logReassessmentTransition(
        "REASSESSMENT_EVIDENCE",
        "contradiction inspected",
        { quest: "auth-flow", kind: "file-read" },
      );
      logReassessmentTransition(
        "REASSESSMENT_COMPLETED",
        "reassessment resolved",
        { quest: "auth-flow", version: 1 },
      );
      logStateUpdateTransition(
        "STATE_UPDATE_ACCEPTED",
        "state update accepted",
        { quest: "auth-flow", planVersion: 2 },
      );
      logPersistenceTransition("SAVE_STARTED", "save started", {
        quest: "auth-flow",
      });
      logPersistenceTransition("SAVE_VERIFIED", "save verified", {
        quest: "auth-flow",
        gen: 1,
        hash: "abc12345",
      });
      logCompactionTransition("COMPACTION_PREPARED", "compaction prepared", {
        quest: "auth-flow",
        compactionId: "cmp_1",
      });
      logCompactionTransition("COMPACTION_STARTED", "compaction started", {
        quest: "auth-flow",
        compactionId: "cmp_1",
      });
      logCompactionTransition("COMPACTION_COMPLETED", "compaction completed", {
        quest: "auth-flow",
        compactionId: "cmp_1",
      });
      logResumeTransition(
        "RESUME_OBLIGATION_CREATED",
        "resume obligation created",
        { quest: "auth-flow", compactionId: "cmp_1" },
      );
      logResumeTransition("RESUME_ATTEMPTED", "resume attempted", {
        quest: "auth-flow",
        compactionId: "cmp_1",
      });
      logResumeTransition("RESUME_DELIVERED", "resume delivered", {
        quest: "auth-flow",
        compactionId: "cmp_1",
      });
      logToolAnomaly("UNKNOWN_TOOL", "unknown tool invoked", {
        quest: "auth-flow",
        tool: "magic_tool",
      });
      logToolFailure("TOOL_TIMEOUT", "tool timed out", {
        quest: "auth-flow",
        tool: "bash",
        command: "sleep 100",
      });
      logContinuationAnomaly("REPEATED_BLOCK", "gate repeatedly blocked", {
        quest: "auth-flow",
        gate: "RESEARCH_PENDING",
        count: 3,
      });
      logSubquestTransition("SUBQUEST_START", "subquest started", {
        quest: "auth-flow",
        subquest: "auth-token",
        parent: "auth-flow",
      });
      logRecoveryTransition(
        "STATE_INCONSISTENT",
        "state inconsistent detected",
        { quest: "auth-flow", code: "RESUME_STATE_INCONSISTENT" },
      );
      logTurnBoundary("TURN_END", "turn ended", {
        quest: "auth-flow",
        substantive: true,
        toolsUsed: 4,
        mutations: 1,
        failures: 1,
      });

      const content = readQuestLog(tempLogPath);
      assert.ok(content.includes("QUEST_DETECTED"));
      assert.ok(content.includes("TURN_START"));
      assert.ok(content.includes("GATE_BLOCKED"));
      assert.ok(content.includes("RESEARCH_EVIDENCE"));
      assert.ok(content.includes("CONFIRMATION_RECEIVED"));
      assert.ok(content.includes("IMPLEMENTATION_COMPLETED"));
      assert.ok(content.includes("TEST_FAILED"));
      assert.ok(content.includes("REASSESSMENT_REQUIRED"));
      assert.ok(content.includes("SAVE_VERIFIED"));
      assert.ok(content.includes("COMPACTION_COMPLETED"));
      assert.ok(content.includes("RESUME_DELIVERED"));
      assert.ok(content.includes("UNKNOWN_TOOL"));
      assert.ok(content.includes("TOOL_TIMEOUT"));
      assert.ok(content.includes("REPEATED_BLOCK"));
      assert.ok(content.includes("SUBQUEST_START"));
      assert.ok(content.includes("STATE_INCONSISTENT"));
      assert.ok(content.includes("TURN_END"));
    },
  );

  await t.step(
    "logEvent does not throw on invalid paths or write errors and sets degraded flag",
    () => {
      resetQuestLoggingDegraded();
      assert.strictEqual(isQuestLoggingDegraded(), false);

      assert.doesNotThrow(() => {
        logEvent("ERROR", "test error without throwing", {
          code: "TEST_ERR",
          logPath: "/invalid-nonexistent-root-dir/protected/test.log",
        });
      });

      assert.strictEqual(
        isQuestLoggingDegraded(),
        true,
        "Logging failure must be detectable via isQuestLoggingDegraded()",
      );
      resetQuestLoggingDegraded();
      assert.strictEqual(isQuestLoggingDegraded(), false);
    },
  );

  await t.step(
    "summarizeQuestJournalLog generates concise and accurate run metrics with canonical major phases",
    () => {
      const sampleLog = `
2026-08-31T00:41:12.421Z | QUEST_CREATED | quest=persistent-agent | root | user request accepted
2026-08-31T00:41:15.003Z | RESEARCH_EVIDENCE | quest=persistent-agent | round=1 kind=code-search reads=3 searches=2 | inspecting execution path
2026-08-31T00:41:29.771Z | IMPLEMENTATION_ATTEMPT | quest=persistent-agent | tool=edit path=src/foo.ts | edit src/foo.ts
2026-08-31T00:41:29.774Z | IMPLEMENTATION_BLOCKED | quest=persistent-agent | gate=RESEARCH_PENDING code=RESEARCH_REQUIRED | research required
2026-08-31T00:41:44.102Z | RESEARCH_COMPLETED | quest=persistent-agent | round=1 | research complete
2026-08-31T00:41:44.221Z | CONFIRMATION_REQUESTED | quest=persistent-agent | awaiting user confirmation
2026-08-31T00:42:03.881Z | COMPACTION_PREPARED | quest=persistent-agent | compactionId=cmp_123 gen=14 | checkpoint prepared
2026-08-31T00:42:04.102Z | COMPACTION_COMPLETED | quest=persistent-agent | compactionId=cmp_123 | compaction completed
2026-08-31T00:42:05.102Z | RESUME_DELIVERED | quest=persistent-agent | compactionId=cmp_123 planVersion=4 | resume delivered
2026-08-31T00:42:17.003Z | TEST_FAILED | quest=persistent-agent | command=make test code=TEST_FAILURE | make test failed with exit code 2
2026-08-31T00:42:17.005Z | REASSESSMENT_REQUIRED | quest=persistent-agent | version=1 | reassessment triggered
2026-08-31T00:42:18.000Z | REPEATED_BLOCK | quest=persistent-agent | gate=RESEARCH_PENDING count=3 | repeated block
2026-08-31T00:42:20.000Z | ARCHIVE | quest=persistent-agent | archived quest persistent-agent
`.trim();

      const summary = summarizeQuestJournalLog(sampleLog);

      assert.deepStrictEqual(summary.quests, ["persistent-agent"]);
      assert.strictEqual(summary.researchCycles, 1);
      assert.strictEqual(summary.reassessmentCycles, 1);
      assert.strictEqual(summary.implementationAttempts, 1);
      assert.strictEqual(summary.implementationBlockedCount, 1);
      assert.deepStrictEqual(summary.blockedGates, ["RESEARCH_PENDING"]);
      assert.strictEqual(summary.failureCount, 1);
      assert.strictEqual(summary.compactionCount, 1);
      assert.strictEqual(summary.successfulCompactions, 1);
      assert.strictEqual(summary.failedCompactions, 0);
      assert.strictEqual(summary.resumeCount, 1);
      assert.strictEqual(summary.resumeSuccessCount, 1);
      assert.strictEqual(summary.resumeFailedCount, 0);
      assert.strictEqual(summary.deadlockWarnings.length, 1);

      // Verify majorPhases vocabulary mapping (Requirement 6)
      const expectedPhases = [
        "INITIALIZATION",
        "RESEARCH",
        "IMPLEMENTATION",
        "CONFIRMATION",
        "COMPACTION",
        "RESUME",
        "VERIFICATION",
        "REASSESSMENT",
        "RECOVERY",
        "COMPLETION",
      ];
      for (const ep of expectedPhases) {
        assert.ok(
          summary.majorPhases.includes(ep),
          `majorPhases must include ${ep}`,
        );
      }
      assert.ok(
        !summary.majorPhases.includes("RESEARCH_EVIDENCE"),
        "majorPhases must not contain raw event names",
      );
      assert.ok(
        !summary.majorPhases.includes("TEST_FAILED"),
        "majorPhases must not contain raw event names",
      );
      assert.strictEqual(summary.hasUnresolvedError, false);

      assert.ok(
        summary.formattedSummary.includes(
          "Quests Tracked (1): persistent-agent",
        ),
      );
      assert.ok(summary.formattedSummary.includes("Research Rounds: 1"));
      assert.ok(
        summary.formattedSummary.includes(
          "Implementation Attempts: 1 (allowed: 0, blocked: 1)",
        ),
      );
      assert.ok(
        summary.formattedSummary.includes(
          "Compactions (1): Total 1 (successful: 1, failed: 0, inconsistent/external: 0)",
        ),
      );
      assert.ok(
        summary.formattedSummary.includes(
          "Resumes (1): Total 1 (successful: 1, failed: 0, pending: 0)",
        ),
      );
      assert.ok(
        summary.formattedSummary.includes("Flow Warnings / Deadlocks (1)"),
      );
    },
  );

  await t.step(
    "logical compaction and resume deduplication in summarizer",
    () => {
      const complexMultiPhaseLog = `
2026-08-31T00:41:12.421Z | QUEST_DETECTED | quest=(none) | reason=new_prompt | substantive root prompt detected
2026-08-31T00:41:12.422Z | QUEST_CREATED | quest=my-quest | provisional root quest created
2026-08-31T00:42:00.000Z | COMPACTION_PREPARED | quest=my-quest | compactionId=cmp_alpha phase=prepared gen=1 | compaction prepared
2026-08-31T00:42:01.000Z | COMPACTION_STARTED | quest=my-quest | compactionId=cmp_alpha | compaction started
2026-08-31T00:42:02.000Z | COMPACTION_COMPLETED | quest=my-quest | compactionId=cmp_alpha | compaction completed
2026-08-31T00:42:03.000Z | RESUME_OBLIGATION_CREATED | quest=my-quest | compactionId=cmp_alpha id=cmp_alpha | resume obligation created
2026-08-31T00:42:04.000Z | RESUME_ATTEMPTED | quest=my-quest | compactionId=cmp_alpha | attempting resume delivery
2026-08-31T00:42:05.000Z | RESUME_DELIVERED | quest=my-quest | compactionId=cmp_alpha planVersion=1 | resume delivered
2026-08-31T00:43:00.000Z | COMPACTION_PREPARED | quest=my-quest | compactionId=cmp_beta phase=prepared gen=2 | compaction prepared
2026-08-31T00:43:01.000Z | COMPACTION_STARTED | quest=my-quest | compactionId=cmp_beta | compaction started
2026-08-31T00:43:02.000Z | COMPACTION_FAILED | quest=my-quest | compactionId=cmp_beta error=timeout | compaction failed
2026-08-31T00:43:03.000Z | RESUME_OBLIGATION_CREATED | quest=my-quest | compactionId=cmp_beta id=cmp_beta | resume fallback obligation created
2026-08-31T00:43:04.000Z | RESUME_ATTEMPTED | quest=my-quest | compactionId=cmp_beta | attempting fallback resume delivery
2026-08-31T00:43:05.000Z | RESUME_DELIVERED | quest=my-quest | compactionId=cmp_beta planVersion=2 | fallback resume delivered
`.trim();

      const summary = summarizeQuestJournalLog(complexMultiPhaseLog);
      assert.strictEqual(summary.compactionCount, 2);
      assert.strictEqual(summary.successfulCompactions, 1);
      assert.strictEqual(summary.failedCompactions, 1);
      assert.strictEqual(summary.resumeCount, 2);
      assert.strictEqual(summary.resumeSuccessCount, 2);
      assert.strictEqual(summary.resumeFailedCount, 0);
    },
  );

  // Cleanup
  try {
    rmSync(tempLogPath, { force: true });
  } catch {}
});

Deno.test("persistent_logging: end-to-end quest journal lifecycle emission verification", async (t) => {
  const currentDir = ".pi/quest/current";
  await mkdir(currentDir, { recursive: true });

  const testLifecycleQid = "test_lifecycle_qid";
  state.questId = testLifecycleQid;
  const tempLogPath = getQuestLogPath(testLifecycleQid);
  clearQuestLog(tempLogPath);

  const rootSlug = "test-log-root-quest";
  const rootPath = `${currentDir}/${rootSlug}.md`;

  await rm(rootPath, { force: true });

  const handlers: Record<string, EventCallback[]> = {};
  const tools: Record<string, any> = {};
  const commands: Record<string, any> = {};
  const userMessages: Array<{ msg: any; options?: any }> = [];

  const mockPi: any = {
    on(event: string, callback: EventCallback) {
      if (!handlers[event]) handlers[event] = [];
      handlers[event].push(callback);
    },
    appendEntry() {},
    registerEntryRenderer() {},
    registerTool(tool: any) {
      tools[tool.name] = tool;
    },
    registerCommand(name: string, cmd: any) {
      commands[name] = cmd;
    },
    sendUserMessage(msg: any, options?: any) {
      userMessages.push({ msg, options });
    },
    sendMessage(msg: any, options?: any) {
      userMessages.push({ msg: msg?.content || msg, options });
    },
  };

  questJournalExtension(mockPi);

  const mockCtx: any = {
    cwd: process.cwd(),
    getContextUsage: () => ({
      tokens: 10000,
      contextWindow: 1000000,
      percent: 1,
    }),
    sessionManager: { getBranch: () => [] },
    ui: {
      notify: () => {},
      setStatus: () => {},
      input: async () => "",
      select: async () => null,
    },
    hasUI: true,
    mode: "headless",
  };

  await t.step(
    "1. Agent start creates root quest and emits QUEST_DETECTED / QUEST_CREATED",
    async () => {
      for (const cb of handlers["before_agent_start"] || []) {
        await cb({
          prompt: "Please implement the new caching layer for database records",
        }, mockCtx);
      }

      const log = readQuestLog(tempLogPath);
      assert.ok(log.includes("QUEST_DETECTED"));
      assert.ok(log.includes("QUEST_CREATED"));
    },
  );

  await t.step(
    "2. Implementation tool call blocked during research emits IMPLEMENTATION_BLOCKED",
    async () => {
      for (const cb of handlers["tool_call"] || []) {
        await cb(
          { toolName: "edit", input: { path: "src/cache.ts" } },
          mockCtx,
        );
      }

      const log = readQuestLog(tempLogPath);
      assert.ok(log.includes("IMPLEMENTATION_BLOCKED"));
      assert.ok(log.includes("gate=PROVISIONAL_RESEARCH_PENDING"));
    },
  );

  await t.step(
    "3. Research update emits STATE_UPDATE_ACCEPTED and RESEARCH_COMPLETED",
    async () => {
      // Provide observed research
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "src/cache.ts" },
          content: "caching code",
        }, mockCtx);
      }

      const updateTool = tools["quest_update_state"];
      assert.ok(updateTool);

      await updateTool.execute(
        "call_1",
        {
          name: rootSlug,
          goal: "Implement caching layer",
          understanding: "Database cache architecture verified",
          assumptions: ["Cache is memory-backed"],
          openQuestions: ["None"],
          findings: ["Verified memory requirements"],
          plan: ["1. Build cache", "2. Add tests"],
          planConfidence: "high",
          exactNextAction: "Build cache module",
          researchComplete: true,
        },
        undefined,
        undefined,
        mockCtx,
      );

      const log = readQuestLog(tempLogPath);
      assert.ok(log.includes("RESEARCH_COMPLETED"));
    },
  );

  await t.step("4. Summary parsing from real lifecycle execution", () => {
    const log = readQuestLog(tempLogPath);
    const summary = summarizeQuestJournalLog(log);

    assert.ok(summary.quests.length >= 1);
    assert.ok(summary.implementationBlockedCount >= 1);
    assert.ok(summary.blockedGates.includes("PROVISIONAL_RESEARCH_PENDING"));
  });

  await t.step(
    "5. Multi-failure preservation in analyzeTurnToolResults without full output dumps",
    () => {
      const mockToolResults = [
        {
          toolName: "bash",
          args: { command: "make test" },
          content: "FAIL: test_cache assertion failed: expected 1 got 2\n" +
            "x".repeat(5000),
          isError: true,
        },
        {
          toolName: "bash",
          args: { command: "npm run build" },
          content: "SyntaxError: Unexpected token in bundle.js\n" +
            "y".repeat(5000),
          isError: true,
        },
        {
          toolName: "edit",
          args: { path: "src/cache.ts" },
          isError: false,
        },
      ];

      const analysis = analyzeTurnToolResults(mockToolResults, rootSlug);
      assert.strictEqual(analysis.meaningfulFailureDetected, true);
      assert.strictEqual(analysis.failureCount, 2);
      assert.ok(analysis.failureCategories.includes("TEST_FAILED"));
      assert.ok(analysis.failureCategories.includes("BUILD_FAILED"));
      assert.strictEqual(analysis.failures.length, 2);
      assert.ok(analysis.failureReason.includes("2 failures detected"));
      assert.ok(
        analysis.failureEvidence.length < 2000,
        "Evidence must be bounded and not log full 10k output",
      );
    },
  );

  await t.step("6. Real error codes in logError and reportAgentError", () => {
    clearQuestLog(tempLogPath);

    logError(
      "Explicit gate failure",
      undefined,
      mockCtx,
      QuestErrorCode.IMPLEMENTATION_BLOCKED,
      "corr_123",
    );
    const log = readQuestLog(tempLogPath);
    assert.ok(log.includes("code=IMPLEMENTATION_BLOCKED"));
    assert.ok(log.includes("correlationId=corr_123"));

    reportAgentError(mockPi, mockCtx, "Research prerequisite missing", {
      code: QuestErrorCode.RESEARCH_REQUIRED,
      correlationId: "corr_456",
      requiredNextAction: "Perform research",
    });

    const logAfterReport = readQuestLog(tempLogPath);
    assert.ok(logAfterReport.includes("code=RESEARCH_REQUIRED"));
    assert.ok(logAfterReport.includes("correlationId=corr_456"));
  });

  await t.step(
    "7. Acceptance test: Simulated failed run tells complete story without transcript bloat",
    () => {
      clearQuestLog(tempLogPath);
      const corrId = "chain_fail_1";

      // 1. What was the agent trying to do?
      logImplementationOutcome(
        "IMPLEMENTATION_ATTEMPT",
        "attempted edit src/critical.c",
        {
          quest: "failed-run-quest",
          tool: "edit",
          path: "src/critical.c",
          correlationId: corrId,
          allowed: false,
        },
      );

      // 2. What did Quest Journal decide?
      logGateTransition("GATE_BLOCKED", "gate blocked: REASSESSMENT_PENDING", {
        quest: "failed-run-quest",
        gate: "REASSESSMENT_PENDING",
        code: QuestErrorCode.REASSESSMENT_REQUIRED,
        reason: "Unresolved contradiction from failed test",
        requiredAction:
          "Investigate contradiction in quest file before editing code",
        correlationId: corrId,
      });

      // 3. What message did it send the agent?
      logAgentMessageTransition(
        "AGENT_MESSAGE_ATTEMPTED",
        "agent message attempted (steer)",
        {
          quest: "failed-run-quest",
          deliverAs: "steer",
          type: "reassessment_required",
          correlationId: corrId,
        },
      );

      // 4. Did delivery succeed?
      logAgentMessageTransition(
        "AGENT_MESSAGE_DELIVERED",
        "agent message delivered (steer)",
        {
          quest: "failed-run-quest",
          deliverAs: "steer",
          type: "reassessment_required",
          correlationId: corrId,
        },
      );

      // 5. What state changed?
      logReassessmentTransition(
        "REASSESSMENT_EVIDENCE",
        "investigating contradiction",
        {
          quest: "failed-run-quest",
          kind: "file-read",
          path: "src/critical.c",
          correlationId: corrId,
        },
      );

      // 6. What happened next?
      logReassessmentTransition(
        "REASSESSMENT_COMPLETED",
        "reassessment resolved",
        {
          quest: "failed-run-quest",
          version: 2,
          correlationId: corrId,
        },
      );

      logGateTransition(
        "GATE_OPENED",
        "reassessment resolved, implementation gate opened",
        {
          quest: "failed-run-quest",
          from: "REASSESSMENT_PENDING",
          to: "IMPLEMENTATION_ALLOWED",
          correlationId: corrId,
        },
      );

      // 7. Where did recovery stop?
      logStateUpdateTransition(
        "STATE_UPDATE_ACCEPTED",
        "state update accepted",
        {
          quest: "failed-run-quest",
          planVersion: 3,
          correlationId: corrId,
        },
      );

      const rawLog = readQuestLog(tempLogPath);
      const lines = rawLog.split("\n").filter((l) => l.trim().length > 0);
      assert.strictEqual(
        lines.length,
        8,
        "Raw log must be sparse and contain exactly the 8 lifecycle events",
      );

      for (const line of lines) {
        assert.ok(
          line.includes(`correlationId=${corrId}`),
          `Line must correlate to ${corrId}: ${line}`,
        );
      }

      const summary = summarizeQuestJournalLog(rawLog);
      assert.strictEqual(summary.implementationAttempts, 1);
      assert.strictEqual(summary.reassessmentCycles, 2);
      assert.deepStrictEqual(
        summary.majorPhases.sort(),
        ["IMPLEMENTATION", "PLANNING", "REASSESSMENT"].sort(),
      );
    },
  );

  // Cleanup
  try {
    rmSync(tempLogPath, { force: true });
    await rm(rootPath, { force: true });
  } catch {}
});
