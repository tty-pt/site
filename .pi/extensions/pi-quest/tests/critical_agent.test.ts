import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import plugin, {
  acceptRootConfirmation,
  archiveQuestFile,
  buildCriticalReviewPrompt,
  canImplement,
  canToolExecuteInCriticalReview,
  checkAndTriggerDirectionReview,
  checkOrdinaryCompletionConditions,
  classifyToolCall,
  clearActiveReviews,
  ensureQuestId,
  executeUpdateStateTool,
  type ExtensionAPI,
  type ExtensionContext,
  getActiveReviews,
  getQuestLogPath,
  getState,
  initProvisionalRootQuest,
  isCriticalReviewSubagentInvocation,
  isCriticalReviewValidForCompletion,
  isSubagentAvailable,
  isSubagentToolRegistered,
  parseCriticalReviewResponse,
  parseLogEntry,
  QuestErrorCode,
  readQuestLog,
  reconstruct,
  registerActiveReview,
  resolveSubagentExecutor,
  restoreSessionState,
  runCriticalReview,
  setCustomSubagentRunner,
  snapshotState,
  type StoredState,
  submitReviewRebuttal,
} from "../index.ts";

function createMockExtensionAPI() {
  const handlers: Record<string, any[]> = {};
  const registeredTools: any[] = [];
  const registeredCommands: any[] = [];
  const agentMessages: Array<
    { msg: any; options?: any; customType?: any; display?: any }
  > = [];
  const userMessages: Array<{ msg: any; options?: any }> = [];
  const appendedEntries: Array<{ type: string; data: any }> = [];
  let configuredTools: any[] = [];
  const eventBusHandlers: Record<string, any[]> = {};

  const events = {
    on: (event: string, handler: any) => {
      if (!eventBusHandlers[event]) eventBusHandlers[event] = [];
      eventBusHandlers[event].push(handler);
      return () => {
        const idx = eventBusHandlers[event].indexOf(handler);
        if (idx >= 0) eventBusHandlers[event].splice(idx, 1);
      };
    },
    emit: (event: string, data: any) => {
      const list = eventBusHandlers[event] || [];
      for (const h of [...list]) {
        try {
          h(data);
        } catch {}
      }
    },
  };

  const mockPi: ExtensionAPI = {
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
      agentMessages.push({
        msg: msg?.content || msg,
        options,
        customType: msg?.customType,
        display: msg?.display,
      });
    },
    sendUserMessage: (msg: any, options?: any) => {
      userMessages.push({ msg, options });
    },
    appendEntry: (type: string, data: any) => {
      appendedEntries.push({ type, data });
    },
    registerEntryRenderer: () => {},
    getAllTools: () => configuredTools,
    events,
  };

  return {
    mockPi,
    handlers,
    registeredTools,
    registeredCommands,
    agentMessages,
    userMessages,
    appendedEntries,
    setAllTools: (tools: any[]) => {
      configuredTools = tools;
    },
    events,
  };
}

function createMockContext(
  tokens = 50000,
  sessionId = `session_${Math.random().toString(36).slice(2)}`,
): ExtensionContext {
  const branch: any[] = [];
  return {
    cwd: process.cwd(),
    mode: "agent",
    hasUI: true,
    sessionManager: {
      id: sessionId,
      sessionId,
      getBranch: () => branch,
      appendCustomEntry: (_type: string, data: any) => {
        branch.push({ type: "custom", customType: "quest_journal", data });
      },
    },
    getContextUsage: () => ({ tokens, percent: (tokens / 800000) * 100 }),
    ui: {
      notify: () => {},
      setStatus: () => {},
      input: async () => "",
      select: async () => null,
    },
  };
}

Deno.test("Critical Agent Review Suite: 22 Comprehensive Verification Scenarios", async (t) => {
  const currentDir = ".pi/quest/current";
  await rm(currentDir, { recursive: true, force: true });
  await mkdir(currentDir, { recursive: true });

  // Setup clean runner state
  setCustomSubagentRunner(null);

  // -----------------------------------------------------------------------
  // 1. Substantive quest triggers direction review
  // -----------------------------------------------------------------------
  await t.step("1. substantive quest triggers direction review", async () => {
    const { mockPi } = createMockExtensionAPI();
    const ctx = createMockContext(50000, "session_test_1");
    const s = getState(ctx);
    s.active = "stream-audio-engine";
    s.questId = "stream-audio-engine";
    s.stack = ["stream-audio-engine"];
    s.researchComplete = true;
    s.researchRequired = false;
    s.planVersion = 1;
    s.prompts = [
      "Implement high-performance stream audio engine with zero heap allocs.",
    ];

    const qPath = `${currentDir}/${s.questId}/quest.md`;
    await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
    await writeFile(
      qPath,
      `# Quest: stream-audio-engine\n\n## Goal\nStream audio\n\n## Original request\n> Implement high-performance stream audio engine with zero heap allocs.\n\n## Current Status\n- [ ] in progress\n\n## Detailed Multi-Stage Execution Plan\n1. Buffer setup\n2. Audio loop\n\n## Remaining work\n- [ ] Task A\n`,
      "utf8",
    );

    let runnerCalledWith = "";
    const mockRunner = async (task: string) => {
      runnerCalledWith = task;
      return `PASS 1 (Provisional Inspection):
Provisional Judgment: PASS
Provisional Summary: Plan matches goal

PASS 2 (Self-Critique & Falsification):
- Tested own assumptions: buffer size is standard
- Revised Judgment: PASS

ORIGINAL-REQUEST CHECK:
- Requirement: Zero heap allocs -> Evidence: static buffer design -> Satisfied: YES

VERDICT: PASS
SEVERITY: NONE

FINDINGS:
- None

REQUIRED ACTIONS:
- Continue execution`;
    };

    setCustomSubagentRunner(mockRunner);
    const result = await checkAndTriggerDirectionReview(
      mockPi,
      ctx,
      "plan_confirmed",
    );

    assert.ok(result, "Direction review must execute");
    assert.strictEqual(result.success, true);
    assert.strictEqual(result.review?.verdict, "PASS");
    assert.ok(
      runnerCalledWith.includes("DIRECTION REVIEW"),
      "Must request Direction Review",
    );
    assert.ok(
      runnerCalledWith.toLowerCase().includes("zero heap allocs"),
      "Must include original prompt",
    );
    setCustomSubagentRunner(null);
  });

  // -----------------------------------------------------------------------
  // 2. Trivial quest does not trigger unnecessary review
  // -----------------------------------------------------------------------
  await t.step(
    "2. trivial quest does not trigger unnecessary review",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_2");
      const s = getState(ctx);
      s.active = "fix-typo";
      s.questId = "fix-typo";
      s.reassessmentRequired = true; // In research/reassessment pending

      setCustomSubagentRunner(async () => "FAIL");
      const res = await checkAndTriggerDirectionReview(mockPi, ctx, "no_op");
      assert.strictEqual(
        res,
        null,
        "Should not trigger review when reassessment is pending",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 3. Reviewer is read-only
  // -----------------------------------------------------------------------
  await t.step("3. reviewer is read-only", async () => {
    const readReviewPerm = classifyToolCall("subagent", {
      agent: "critic",
      isCriticalReview: true,
      reviewKind: "direction",
      task: "[CRITICAL REVIEW] Direction Review",
    });
    assert.strictEqual(
      readReviewPerm,
      "research",
      "Critical review subagent must be classified as read/research",
    );

    const mutatingSubagentPerm = classifyToolCall("subagent", {
      agent: "developer",
      task: "Implement player feature and write files",
    });
    assert.strictEqual(
      mutatingSubagentPerm,
      "implementation",
      "General subagent must remain classified as implementation",
    );

    assert.strictEqual(
      isCriticalReviewSubagentInvocation({ isCriticalReview: true }),
      true,
    );
    assert.strictEqual(
      isCriticalReviewSubagentInvocation({ agent: "critic" }),
      true,
    );
    assert.strictEqual(
      isCriticalReviewSubagentInvocation({ task: "random task" }),
      false,
    );
  });

  // -----------------------------------------------------------------------
  // 4. Reviewer receives the exact recorded original request
  // -----------------------------------------------------------------------
  await t.step(
    "4. reviewer receives the exact recorded original request",
    async () => {
      const originalPrompt =
        "Exact prompt from user: Create axil HTTP cache with LRU policy and ETag support.";
      const prompt = buildCriticalReviewPrompt(
        "final_acceptance",
        "test-lru-cache",
        {
          originalRequest: originalPrompt,
          refinements: ["Also support cache bypass header"],
          currentUnderstanding: "axil cache architecture",
          keyAssumptions: "Max 100 entries",
          openQuestions: "",
          plan: "1. LRU queue\n2. ETag computation",
          planConfidence: "high",
          planRevisions: "",
          findings: "FTS integration clean",
          filesModified: "mods/cache/lru.c",
          testStatus: "All unit tests pass",
          executionSnapshot: "Cache built and tested",
          exactNextAction: "Archive quest",
          remainingWork: "- [ ] None",
          status: "Complete",
        },
      );

      assert.ok(
        prompt.includes(originalPrompt),
        "Prompt must include exact recorded original prompt",
      );
      assert.ok(
        prompt.includes("Also support cache bypass header"),
        "Prompt must include refinements as supplementary context",
      );
      assert.ok(
        prompt.includes(
          "ORIGINAL USER REQUEST (Primary Acceptance Criterion):",
        ),
        "Must label original request as primary",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 5. Reviewer performs self-critique before final verdict
  // -----------------------------------------------------------------------
  await t.step(
    "5. reviewer performs self-critique before final verdict",
    async () => {
      const sampleResponse = `PASS 1 (Provisional Inspection):
Provisional Judgment: PASS
Provisional Summary: Code seems fine at first glance.

PASS 2 (Self-Critique & Falsification):
- Tested own assumptions: Did I verify thread safety?
- Evidence evaluated: No mutex in buffer write path.
- Revised Judgment: FAIL

ORIGINAL-REQUEST CHECK:
- Requirement: Thread safety -> Evidence: Shared buffer lacks locking -> Satisfied: NO

VERDICT: FAIL
SEVERITY: MAJOR

FINDINGS:
- Issue: Missing lock on shared audio ring buffer
  Evidence: mods/song/player.c line 142 writes without mutex

REQUIRED ACTIONS:
- Add pthread_mutex around ring buffer write`;

      const parsed = parseCriticalReviewResponse(sampleResponse);
      assert.strictEqual(parsed.selfCritique?.initialJudgment, "PASS");
      assert.strictEqual(parsed.selfCritique?.revisedJudgment, "FAIL");
      assert.strictEqual(parsed.verdict, "FAIL");
      assert.strictEqual(parsed.severity, "MAJOR");
      assert.strictEqual(parsed.findings.length, 1);
      assert.ok(parsed.findings[0].issue.includes("Missing lock"));
    },
  );

  // -----------------------------------------------------------------------
  // 6. Self-critique can overturn the initial PASS
  // -----------------------------------------------------------------------
  await t.step("6. self-critique can overturn the initial PASS", async () => {
    const sampleResponse = `PASS 1 (Provisional Inspection):
Provisional Judgment: PASS
Provisional Summary: All tests are passing.

PASS 2 (Self-Critique & Falsification):
- Tested own assumptions: Tests only test single-threaded case.
- Evidence evaluated: Concurrency test is absent.
- Revised Judgment: UNCERTAIN

ORIGINAL-REQUEST CHECK:
- Requirement: High concurrency -> Evidence: No concurrent stress test -> Satisfied: UNCERTAIN

VERDICT: UNCERTAIN
SEVERITY: MINOR

FINDINGS:
- Issue: Concurrency under load is unverified
  Evidence: Test suite lacks parallel client test

REQUIRED ACTIONS:
- Run concurrent test with 50 parallel clients`;

    const parsed = parseCriticalReviewResponse(sampleResponse);
    assert.strictEqual(parsed.selfCritique?.initialJudgment, "PASS");
    assert.strictEqual(parsed.selfCritique?.revisedJudgment, "UNCERTAIN");
    assert.strictEqual(parsed.verdict, "UNCERTAIN");
  });

  // -----------------------------------------------------------------------
  // 7. Reviewer identifies an unsupported assumption
  // -----------------------------------------------------------------------
  await t.step("7. reviewer identifies an unsupported assumption", async () => {
    const { mockPi, agentMessages } = createMockExtensionAPI();
    const ctx = createMockContext(50000, "session_test_7");
    const s = getState(ctx);
    s.active = "unsupported-assumption-quest";
    s.questId = "unsupported-assumption-quest";

    const runner = async () =>
      `PASS 1 (Provisional Inspection):
Provisional Judgment: UNCERTAIN

PASS 2 (Self-Critique & Falsification):
- Tested own assumptions: Assumed buffer size 4096 fits all packets without testing.
- Revised Judgment: UNCERTAIN

VERDICT: UNCERTAIN
SEVERITY: MAJOR

FINDINGS:
- Issue: Buffer size assumption unverified
  Evidence: Large audio frames exceed 4096 bytes in hyle chunker

REQUIRED ACTIONS:
- Test chunker with 8192-byte frames`;

    const res = await runCriticalReview(mockPi, ctx, {
      kind: "direction",
      subagentRunner: runner,
    });
    assert.strictEqual(res.success, false);
    assert.strictEqual(res.review?.verdict, "UNCERTAIN");
    assert.ok(
      agentMessages.some((m) => m.msg.includes("CRITICAL REVIEW UNCERTAIN")),
    );
  });

  // -----------------------------------------------------------------------
  // 8. Reviewer identifies a missed original requirement
  // -----------------------------------------------------------------------
  await t.step(
    "8. reviewer identifies a missed original requirement",
    async () => {
      const raw = `ORIGINAL-REQUEST CHECK:
- Requirement: Support MP3 -> Evidence: song_mp3.c -> Satisfied: YES
- Requirement: Support FLAC -> Evidence: No FLAC decoder implemented -> Satisfied: NO

VERDICT: FAIL
SEVERITY: CRITICAL

FINDINGS:
- Issue: FLAC support completely missing
  Evidence: No flac decoding routines found in mods/song/

REQUIRED ACTIONS:
- Implement flac_decode in mods/song/flac.c`;

      const parsed = parseCriticalReviewResponse(raw);
      assert.strictEqual(parsed.verdict, "FAIL");
      assert.strictEqual(parsed.severity, "CRITICAL");
      assert.strictEqual(parsed.originalRequestCheck.unsatisfied.length, 1);
      assert.ok(parsed.originalRequestCheck.unsatisfied[0].includes("FLAC"));
    },
  );

  // -----------------------------------------------------------------------
  // 9. Reviewer flags unnecessary complexity only when it has a concrete consequence
  // -----------------------------------------------------------------------
  await t.step(
    "9. reviewer flags unnecessary complexity only when it has a concrete consequence",
    async () => {
      const raw = `VERDICT: FAIL
SEVERITY: MAJOR

FINDINGS:
- Issue: Triple caching layer introduces race condition and memory bloat
  Evidence: Three separate hash tables cache the same request, causing memory usage to exceed 200MB limit

REQUIRED ACTIONS:
- Eliminate redundant cache layers and use single unified axil cache`;

      const parsed = parseCriticalReviewResponse(raw);
      assert.strictEqual(parsed.verdict, "FAIL");
      assert.strictEqual(parsed.severity, "MAJOR");
      assert.ok(parsed.findings[0].issue.includes("Triple caching layer"));
    },
  );

  // -----------------------------------------------------------------------
  // 10. Reviewer does not block for mere stylistic preference
  // -----------------------------------------------------------------------
  await t.step(
    "10. reviewer does not block for mere stylistic preference",
    async () => {
      const raw = `PASS 1:
Provisional Judgment: PASS

PASS 2:
- No failure modes found.

ORIGINAL-REQUEST CHECK:
- Requirement: All requirements met -> Evidence: verified -> Satisfied: YES

VERDICT: PASS
SEVERITY: NONE

FINDINGS:
- Issue: Function names could be shorter (stylistic preference)
  Evidence: reviewer preference only

REQUIRED ACTIONS:
- None`;

      const parsed = parseCriticalReviewResponse(raw);
      assert.strictEqual(parsed.verdict, "PASS");
      assert.strictEqual(parsed.severity, "NONE");
    },
  );

  // -----------------------------------------------------------------------
  // 11. FAIL creates concrete remediation work
  // -----------------------------------------------------------------------
  await t.step("11. FAIL creates concrete remediation work", async () => {
    const { mockPi } = createMockExtensionAPI();
    const ctx = createMockContext(50000, "session_test_11");
    const s = getState(ctx);
    const slug = "remediation-quest";
    s.active = slug;
    s.questId = slug;

    const qPath = `${currentDir}/${slug}/quest.md`;
    await mkdir(`${currentDir}/${slug}`, { recursive: true });
    await writeFile(
      qPath,
      `# Quest: ${slug}\n\n## Goal\nTest\n\n## Remaining work\n- [ ] Task 1\n`,
      "utf8",
    );

    const runner = async () =>
      `VERDICT: FAIL
SEVERITY: CRITICAL

FINDINGS:
- Issue: Buffer overflow in parser
  Evidence: parser.c line 55 lacks bounds check

REQUIRED ACTIONS:
- Add bounds check in parser.c before memcpy`;

    await runCriticalReview(mockPi, ctx, {
      kind: "direction",
      subagentRunner: runner,
    });

    const updatedDisk = await readFile(qPath, "utf8");
    assert.ok(
      updatedDisk.includes("Add bounds check in parser.c before memcpy"),
      "Must append required action to Remaining work",
    );
    assert.strictEqual(
      s.reassessmentRequired,
      true,
      "Critical fail must trigger reassessment",
    );
  });

  // -----------------------------------------------------------------------
  // 12. UNCERTAIN identifies specific missing evidence
  // -----------------------------------------------------------------------
  await t.step(
    "12. UNCERTAIN identifies specific missing evidence",
    async () => {
      const { mockPi, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_12");
      const s = getState(ctx);
      s.active = "uncertain-quest";
      s.questId = "uncertain-quest";

      const runner = async () =>
        `VERDICT: UNCERTAIN
SEVERITY: MINOR

FINDINGS:
- Issue: Zero evidence of Safari WebAudio compatibility
  Evidence: Only tested on Linux Chrome

REQUIRED ACTIONS:
- Test WebAudio sink in Safari WebKit environment`;

      const res = await runCriticalReview(mockPi, ctx, {
        kind: "direction",
        subagentRunner: runner,
      });
      assert.strictEqual(res.review?.verdict, "UNCERTAIN");
      const msg = agentMessages.find((m) =>
        m.msg.includes("CRITICAL REVIEW UNCERTAIN")
      );
      assert.ok(msg, "Must send UNCERTAIN message to agent");
      assert.ok(
        msg.msg.includes("Zero evidence of Safari WebAudio compatibility"),
      );
    },
  );

  // -----------------------------------------------------------------------
  // 13. Final FAIL prevents successful completion/archive/changelog
  // -----------------------------------------------------------------------
  await t.step(
    "13. final FAIL prevents successful completion/archive/changelog",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_13");
      const s = getState(ctx);
      const slug = "root-fail-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.dirty = false;
      s.researchComplete = true;
      s.researchRequired = false;
      s.reassessmentRequired = false;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nComplete root quest\n\n## Original request\n> Implement XYZ\n`,
        "utf8",
      );

      const failRunner = async () =>
        `VERDICT: FAIL
SEVERITY: CRITICAL

FINDINGS:
- Issue: XYZ not implemented
  Evidence: Empty file

REQUIRED ACTIONS:
- Implement XYZ`;

      setCustomSubagentRunner(failRunner);
      const archiveRes = await archiveQuestFile(slug, mockPi, ctx);

      assert.strictEqual(
        archiveRes.success,
        false,
        "Archive must fail when final acceptance review fails",
      );
      assert.ok(
        archiveRes.message.includes("Final critical acceptance review failed"),
        "Error message must state review failure",
      );

      const questStillExists = await readFile(qPath, "utf8");
      assert.ok(
        questStillExists.includes("Complete root quest"),
        "Quest file must NOT be deleted",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 14. Final PASS permits completion only when all ordinary Quest Journal completion conditions also hold
  // -----------------------------------------------------------------------
  await t.step(
    "14. final PASS permits completion only when all ordinary Quest Journal completion conditions also hold",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_14");
      const s = getState(ctx);
      const slug = "root-pass-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.dirty = false;
      s.researchComplete = true;
      s.researchRequired = false;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nImplement ABC\n\n## Original request\n> Implement ABC\n\n## Current Status\n- [x] done\n\n## Remaining work\n- [x] all done\n`,
        "utf8",
      );

      const passRunner = async () =>
        `PASS 1:
Provisional Judgment: PASS

PASS 2:
- Revised Judgment: PASS

ORIGINAL-REQUEST CHECK:
- Requirement: Implement ABC -> Evidence: Verified in mods/abc.c -> Satisfied: YES

VERDICT: PASS
SEVERITY: NONE

FINDINGS:
- None

REQUIRED ACTIONS:
- None`;

      setCustomSubagentRunner(passRunner);
      const archiveRes = await archiveQuestFile(slug, mockPi, ctx);

      assert.strictEqual(
        archiveRes.success,
        true,
        "Archive must succeed when review passes",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 15. A material change after PASS requires a fresh final review
  // -----------------------------------------------------------------------
  await t.step(
    "15. a material change after PASS requires a fresh final review",
    async () => {
      const s: StoredState = {
        active: "quest-a",
        questId: "quest-a",
        saveCount: 5,
        compactCount: 0,
        prompts: ["Test prompt"],
        stack: ["quest-a"],
        dirty: false,
        planVersion: 1,
        lastSavedHash: "hash_v1",
        lastCriticalReview: {
          id: "rev_1",
          questId: "quest-a",
          kind: "final_acceptance",
          reviewedStateVersion: {
            planVersion: 1,
            saveHash: "hash_v1",
            saveCount: 5,
          },
          verdict: "PASS",
          severity: "NONE",
          findings: [],
          requiredActions: [],
          resolved: true,
          timestamp: Date.now(),
        },
      };

      assert.strictEqual(
        isCriticalReviewValidForCompletion(s),
        true,
        "Should be valid before change",
      );

      // Case A: State becomes dirty
      s.dirty = true;
      assert.strictEqual(
        isCriticalReviewValidForCompletion(s),
        false,
        "Dirty state must invalidate PASS",
      );
      s.dirty = false;

      // Case B: Plan version increment
      s.planVersion = 2;
      assert.strictEqual(
        isCriticalReviewValidForCompletion(s),
        false,
        "Plan version increment must invalidate PASS",
      );
      s.planVersion = 1;

      // Case C: Save hash changes
      s.lastSavedHash = "hash_v2";
      assert.strictEqual(
        isCriticalReviewValidForCompletion(s),
        false,
        "New save hash must invalidate PASS",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 16. Reviewer error does not become PASS
  // -----------------------------------------------------------------------
  await t.step("16. reviewer error does not become PASS", async () => {
    const { mockPi, agentMessages } = createMockExtensionAPI();
    const ctx = createMockContext(50000, "session_test_16");
    const s = getState(ctx);
    s.active = "error-quest";
    s.questId = "error-quest";

    const errorRunner = async () => {
      throw new Error("Subagent model context exhausted");
    };

    const res = await runCriticalReview(mockPi, ctx, {
      kind: "final_acceptance",
      subagentRunner: errorRunner,
    });
    assert.strictEqual(res.success, false, "Subagent crash must not succeed");
    assert.strictEqual(res.review, undefined);
    assert.ok(res.error?.includes("Subagent model context exhausted"));
    assert.ok(
      agentMessages.some((m) => m.msg.includes("CRITICAL_REVIEW_ERROR")),
    );
  });

  // -----------------------------------------------------------------------
  // 17. Review findings survive compaction/reconstruction
  // -----------------------------------------------------------------------
  await t.step(
    "17. review findings survive compaction/reconstruction",
    async () => {
      const ctx = createMockContext(50000, "session_test_17");
      const s = getState(ctx);
      s.active = "persist-review-quest";
      s.questId = "persist-review-quest";
      s.lastCriticalReview = {
        id: "rev_durable_1",
        questId: "persist-review-quest",
        kind: "direction",
        reviewedStateVersion: {
          planVersion: 2,
          saveHash: "hash_durable",
          saveCount: 3,
        },
        verdict: "FAIL",
        severity: "MAJOR",
        findings: [{
          issue: "Missing input sanitization",
          evidence: "query.c:10",
        }],
        requiredActions: ["Add sanitize_query()"],
        resolved: false,
        timestamp: 123456789,
      };
      s.criticalReviews = [s.lastCriticalReview];

      const snap = snapshotState(ctx);
      assert.strictEqual(snap.lastCriticalReview?.id, "rev_durable_1");
      assert.strictEqual(snap.criticalReviews?.length, 1);

      const restored = restoreSessionState(snap);
      assert.strictEqual(restored.lastCriticalReview?.id, "rev_durable_1");
      assert.strictEqual(
        restored.lastCriticalReview?.findings[0].issue,
        "Missing input sanitization",
      );
      assert.strictEqual(
        restored.criticalReviews?.[0].requiredActions[0],
        "Add sanitize_query()",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 18. Main-agent evidence-based rebuttal can cause reviewer re-evaluation
  // -----------------------------------------------------------------------
  await t.step(
    "18. main-agent evidence-based rebuttal can cause reviewer re-evaluation",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_18");
      const s = getState(ctx);
      s.active = "rebuttal-quest";
      s.questId = "rebuttal-quest";

      let receivedPrompt = "";
      const rebuttalRunner = async (prompt: string) => {
        receivedPrompt = prompt;
        return `PASS 1:
Provisional Judgment: PASS

PASS 2:
- Evaluated main agent rebuttal: Mutex is indeed present in parent module axil_server.c
- Revised Judgment: PASS

VERDICT: PASS
SEVERITY: NONE

FINDINGS:
- Issue resolved by evidence

REQUIRED ACTIONS:
- None`;
      };

      const res = await submitReviewRebuttal(
        mockPi,
        ctx,
        "The locking is handled at the connection level in axil_server.c line 88.",
        {
          subagentRunner: rebuttalRunner,
        },
      );

      assert.ok(
        receivedPrompt.includes("MAIN AGENT EVIDENCE-BASED REBUTTAL"),
        "Prompt must include rebuttal section",
      );
      assert.ok(
        receivedPrompt.includes("axil_server.c line 88"),
        "Prompt must include specific rebuttal evidence",
      );
      assert.strictEqual(res.success, true);
      assert.strictEqual(res.review?.verdict, "PASS");
    },
  );

  // -----------------------------------------------------------------------
  // 19. Review loops remain bounded
  // -----------------------------------------------------------------------
  await t.step("19. review loops remain bounded", async () => {
    const { mockPi } = createMockExtensionAPI();
    const ctx = createMockContext(50000, "session_test_19");
    const s = getState(ctx);
    s.active = "bounded-loop-quest";
    s.questId = "bounded-loop-quest";
    s.planVersion = 1;
    s.lastSavedHash = "hash_same";

    const failRunner = async () =>
      `VERDICT: FAIL
SEVERITY: CRITICAL
FINDINGS:
- Persistent issue
REQUIRED ACTIONS:
- Fix`;

    // Run 3 times
    await runCriticalReview(mockPi, ctx, {
      kind: "direction",
      subagentRunner: failRunner,
    });
    await runCriticalReview(mockPi, ctx, {
      kind: "direction",
      subagentRunner: failRunner,
    });
    await runCriticalReview(mockPi, ctx, {
      kind: "direction",
      subagentRunner: failRunner,
    });

    // 4th attempt must be bounded and reject
    const boundedRes = await runCriticalReview(mockPi, ctx, {
      kind: "direction",
      subagentRunner: failRunner,
    });
    assert.strictEqual(boundedRes.success, false);
    assert.ok(boundedRes.error?.includes("bound"));
  });

  // -----------------------------------------------------------------------
  // 20. No subagent tool available → normal Quest Journal workflow still works
  // -----------------------------------------------------------------------
  await t.step(
    "20. no subagent tool available -> normal Quest Journal workflow still works",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_20");
      setCustomSubagentRunner(null);

      assert.strictEqual(
        isSubagentAvailable(mockPi, ctx),
        false,
        "Subagent should be unavailable in bare mock",
      );

      const res = await runCriticalReview(mockPi, ctx, { kind: "direction" });
      assert.strictEqual(res.available, false, "Must report available: false");
      assert.strictEqual(res.success, true, "Must not crash workflow");
      assert.strictEqual(res.skipped, true, "Must be skipped gracefully");
    },
  );

  // -----------------------------------------------------------------------
  // 21. Critical-review events appear in the execution log
  // -----------------------------------------------------------------------
  await t.step(
    "21. critical-review events appear in the execution log",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_21");
      const s = getState(ctx);
      s.active = "log-event-quest";
      s.questId = "log-event-quest";

      const runner = async () =>
        `PASS 1:
Provisional Judgment: PASS
PASS 2:
Revised Judgment: PASS
VERDICT: PASS
SEVERITY: NONE
FINDINGS:
- None
REQUIRED ACTIONS:
- None`;

      await runCriticalReview(mockPi, ctx, {
        kind: "direction",
        subagentRunner: runner,
      });

      const log = readQuestLog(getQuestLogPath("log-event-quest"));
      assert.ok(
        log.includes("CRITICAL_REVIEW_REQUESTED"),
        "Log must include CRITICAL_REVIEW_REQUESTED",
      );
      assert.ok(
        log.includes("CRITICAL_REVIEW_STARTED"),
        "Log must include CRITICAL_REVIEW_STARTED",
      );
      assert.ok(
        log.includes("SELF_CRITIQUE_STARTED"),
        "Log must include SELF_CRITIQUE_STARTED",
      );
      assert.ok(
        log.includes("CRITICAL_REVIEW_PASSED"),
        "Log must include CRITICAL_REVIEW_PASSED",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 22. Multiple concurrent main quests cannot mix their reviewer state, messages, or logs
  // -----------------------------------------------------------------------
  await t.step(
    "22. multiple concurrent main quests cannot mix their reviewer state, messages, or logs",
    async () => {
      const ctxA = createMockContext(50000, "session_quest_A");
      const ctxB = createMockContext(50000, "session_quest_B");
      const { mockPi } = createMockExtensionAPI();

      const sA = getState(ctxA);
      sA.active = "quest-alpha";
      sA.questId = "quest-alpha";

      const sB = getState(ctxB);
      sB.active = "quest-beta";
      sB.questId = "quest-beta";

      const runnerA = async () =>
        `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      const runnerB = async () =>
        `VERDICT: FAIL\nSEVERITY: CRITICAL\nFINDINGS:\n- Beta broken\nREQUIRED ACTIONS:\n- Fix beta`;

      await runCriticalReview(mockPi, ctxA, {
        kind: "direction",
        subagentRunner: runnerA,
      });
      await runCriticalReview(mockPi, ctxB, {
        kind: "direction",
        subagentRunner: runnerB,
      });

      assert.strictEqual(
        sA.lastCriticalReview?.verdict,
        "PASS",
        "Quest A must be PASS",
      );
      assert.strictEqual(
        sB.lastCriticalReview?.verdict,
        "FAIL",
        "Quest B must be FAIL",
      );

      const logA = readQuestLog(getQuestLogPath("quest-alpha"));
      const logB = readQuestLog(getQuestLogPath("quest-beta"));

      assert.ok(
        logA.includes("CRITICAL_REVIEW_PASSED"),
        "Log A must have passed event",
      );
      assert.ok(
        !logA.includes("Beta broken"),
        "Log A must not leak Quest B data",
      );
      assert.ok(
        logB.includes("CRITICAL_REVIEW_FAILED"),
        "Log B must have failed event",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 23. Substantive quest automatically reaches direction-review boundary on plan confirmation without manual call
  // -----------------------------------------------------------------------
  await t.step(
    "23. substantive quest automatically reaches direction-review boundary on plan confirmation without manual call",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_23");
      const s = getState(ctx);
      const slug = "auto-direction-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.awaitingUserConfirmation = true;
      s.prompts = ["Implement automated metrics collection pipeline."];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nMetrics pipeline\n\n## Original request\n> Implement automated metrics collection pipeline.\n\n## Current Status\n- [ ] awaiting confirmation\n\n## Detailed Multi-Stage Execution Plan\n1. Ingest\n2. Aggregate\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let reviewCalls = 0;
      const passRunner = async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(passRunner);

      // Accept root user confirmation
      await acceptRootConfirmation(mockPi, ctx);

      assert.strictEqual(
        reviewCalls,
        1,
        "Direction review must automatically trigger when confirmation is accepted",
      );
      assert.strictEqual(
        s.lastCriticalReview?.verdict,
        "PASS",
        "Direction review must be recorded in state",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 24. Repeated ordinary turns without meaningful state change do not repeatedly invoke the reviewer
  // -----------------------------------------------------------------------
  await t.step(
    "24. repeated ordinary turns without meaningful state change do not repeatedly invoke the reviewer",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_24");
      const s = getState(ctx);
      const slug = "dedup-direction-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.awaitingUserConfirmation = false;
      s.planVersion = 1;
      s.prompts = ["Implement feature ABC"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nABC\n\n## Original request\n> Implement feature ABC\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let reviewCalls = 0;
      const passRunner = async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(passRunner);

      // First trigger
      await checkAndTriggerDirectionReview(mockPi, ctx, "initial_plan");
      assert.strictEqual(reviewCalls, 1, "First review should execute");

      // Simulate 5 ordinary turns without state changes
      for (let i = 0; i < 5; i++) {
        const res = await checkAndTriggerDirectionReview(
          mockPi,
          ctx,
          "turn_tick",
        );
        assert.strictEqual(
          res,
          null,
          "Repeated turn without state change must be deduplicated",
        );
      }

      assert.strictEqual(
        reviewCalls,
        1,
        "Review count must remain 1 despite repeated turns",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 24b. Failed/errored direction review does not consume deduplication key and allows retry
  // -----------------------------------------------------------------------
  await t.step(
    "24b. failed/errored direction review does not consume deduplication key and allows retry",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_24b");
      const s = getState(ctx);
      const slug = "retry-direction-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.awaitingUserConfirmation = false;
      s.planVersion = 1;
      s.prompts = ["Implement feature Retry"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nRetry\n\n## Original request\n> Implement feature Retry\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let callCount = 0;
      let shouldFail = true;
      const flakyRunner = async () => {
        callCount++;
        if (shouldFail) {
          throw new Error("Subagent reviewer timeout / network failure");
        }
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(flakyRunner);

      // First trigger fails with error
      const failResult = await checkAndTriggerDirectionReview(
        mockPi,
        ctx,
        "initial_plan",
      );
      assert.strictEqual(callCount, 1, "First review attempt ran");
      assert.strictEqual(failResult?.success, false, "First attempt failed");
      assert.strictEqual(
        (s as any).__lastDirectionReviewKey,
        undefined,
        "Deduplication key must not be set on failed review",
      );

      // Second trigger with reviewer still failing
      const secondFailResult = await checkAndTriggerDirectionReview(
        mockPi,
        ctx,
        "initial_plan",
      );
      assert.strictEqual(
        callCount,
        2,
        "Second review attempt ran because key was not consumed",
      );
      assert.strictEqual(secondFailResult?.success, false);

      // Now reviewer recovers
      shouldFail = false;
      const passResult = await checkAndTriggerDirectionReview(
        mockPi,
        ctx,
        "initial_plan",
      );
      assert.strictEqual(callCount, 3, "Third attempt ran and succeeded");
      assert.strictEqual(passResult?.success, true);
      assert.strictEqual(passResult?.review?.verdict, "PASS");
      assert.ok(
        (s as any).__lastDirectionReviewKey,
        "Deduplication key must now be set after successful review",
      );

      // Subsequent trigger with same state is deduplicated
      const dedupResult = await checkAndTriggerDirectionReview(
        mockPi,
        ctx,
        "turn_tick",
      );
      assert.strictEqual(
        dedupResult,
        null,
        "Successful review deduplicates future turns",
      );
      assert.strictEqual(
        callCount,
        3,
        "Runner not invoked again for same state version",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 25. Material plan/version change permits a new direction review
  // -----------------------------------------------------------------------
  await t.step(
    "25. material plan/version change permits a new direction review",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_25");
      const s = getState(ctx);
      const slug = "plan-version-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.awaitingUserConfirmation = false;
      s.planVersion = 1;
      s.prompts = ["Implement feature XYZ"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nXYZ\n\n## Original request\n> Implement feature XYZ\n\n## Plan\n1. Initial plan\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let reviewCalls = 0;
      const passRunner = async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(passRunner);

      // Initial review
      await checkAndTriggerDirectionReview(mockPi, ctx, "initial");
      assert.strictEqual(reviewCalls, 1, "Initial review executed");

      // State update with plan revision and incremented planVersion
      await executeUpdateStateTool(
        {
          name: slug,
          plan: ["1. Revised step A", "2. Revised step B"],
          planRevisions: [
            "Revised plan to accommodate zero heap alloc constraint.",
          ],
          planVersion: 2,
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        reviewCalls,
        2,
        "Material plan revision must automatically trigger a new direction review",
      );
      assert.strictEqual(s.planVersion, 2);
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 26. Critical reviewer cannot mutate repository through subagent execution path
  // -----------------------------------------------------------------------
  await t.step(
    "26. critical reviewer cannot mutate repository through subagent execution path",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_26");
      plugin(mockPi);

      const s = getState(ctx);
      s.active = "read-only-guard-quest";
      s.questId = "read-only-guard-quest";
      s.stack = [s.active];
      s.implementationAllowed = true; // Main agent implementation gate is open
      // Open the research/implementation gate so the ONLY potential blocker during
      // the review window is the (role-based) read-only gate — isolating its behavior.
      s.researchComplete = true;
      s.researchRequired = false;

      const qPath = `${currentDir}/${s.active}/quest.md`;
      await mkdir(`${currentDir}/${s.active}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${s.active}\n\n## Goal\nGuarded\n\n## Original request\n> Guarded\n`,
        "utf8",
      );

      // Safe read checks are allowed in critical review
      assert.strictEqual(
        canToolExecuteInCriticalReview("read", { path: "src/foo.ts" }),
        true,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("search_graph", { query: "foo" }),
        true,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("bash", { command: "git status" }),
        true,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("bash", { command: "git diff HEAD" }),
        true,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("bash", { command: "rg 'test' src/" }),
        true,
      );

      // Mutating tools are disallowed in critical review
      assert.strictEqual(
        canToolExecuteInCriticalReview("edit", { path: "src/foo.ts" }),
        false,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("write", { path: "src/foo.ts" }),
        false,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("bash", {
          command: "git commit -m 'oops'",
        }),
        false,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("bash", { command: "rm -rf src/" }),
        false,
      );
      assert.strictEqual(
        canToolExecuteInCriticalReview("bash", {
          command: "npm install malicious-pkg",
        }),
        false,
      );

      // Test tool_call gate enforcement: read-only enforcement is now ROLE-BASED.
      // The main (parent) session is NEVER read-only gated by inCriticalReview: it
      // may draft/research freely while a background review runs.
      s.inCriticalReview = true;
      const toolCallHandler = handlers["tool_call"]?.[0];
      assert.ok(toolCallHandler, "tool_call handler must be registered");

      // Main session edit/mutating calls are NOT blocked, even inCriticalReview is set.
      const mainEditAttempt = await toolCallHandler({
        toolName: "edit",
        input: { path: "src/foo.ts" },
      }, ctx);
      assert.ok(
        !mainEditAttempt?.block,
        "Main session must NOT be read-only blocked by inCriticalReview flag",
      );

      const mainBashAttempt = await toolCallHandler({
        toolName: "bash",
        input: { command: "git commit -m 'test'" },
      }, ctx);
      assert.ok(
        !mainBashAttempt?.block,
        "Main session mutating bash must NOT be read-only blocked by inCriticalReview flag",
      );

      // The reviewer CHILD session IS read-only blocked by the role-based backstop,
      // even though it shares the quest's inCriticalReview flag.
      const reviewerChildSessionId = "reviewer_child_scenario_26";
      const reviewerCtx = createMockContext(50000, reviewerChildSessionId);
      // Mirror a reviewer child session that is attached to the same quest so the
      // tool gate resolves it as a reviewer rather than an unrelated observer.
      const reviewerChildState = getState(reviewerCtx);
      reviewerChildState.active = s.active;
      reviewerChildState.questId = s.questId;
      const childReview = registerActiveReview(
        "rev_scenario_26",
        s.active,
        ctx.sessionManager?.id || "session_test_26",
        "plan_review",
        {
          questId: s.questId,
          sessionId: reviewerChildSessionId,
          reviewId: "rev_scenario_26",
          reviewKind: "plan_review",
          planVersion: 1,
          saveGeneration: 0,
          stateHash: null,
          originalUserRequest: "stub",
          currentUnderstanding: "stub",
          assumptions: "",
          plan: "stub",
          planRevisions: "",
          findings: "",
          filesChanged: "",
          relevantDiff: "",
          testStatus: "",
          nextAction: "",
          createdAt: Date.now(),
        },
      );
      childReview.childSessionId = reviewerChildSessionId;

      const reviewerEditAttempt = await toolCallHandler({
        toolName: "edit",
        input: { path: "src/foo.ts" },
      }, reviewerCtx);
      assert.strictEqual(
        reviewerEditAttempt?.block,
        true,
        "Reviewer child edit must be blocked",
      );
      assert.ok(
        reviewerEditAttempt?.reason?.includes("Critical Review Read-Only Enforcement"),
      );

      const reviewerMutatingBash = await toolCallHandler({
        toolName: "bash",
        input: { command: "git commit -m 'test'" },
      }, reviewerCtx);
      assert.strictEqual(
        reviewerMutatingBash?.block,
        true,
        "Reviewer child mutating git command must be blocked",
      );

      const reviewerReadAttempt = await toolCallHandler({
        toolName: "read",
        input: { path: "src/foo.ts" },
      }, reviewerCtx);
      assert.strictEqual(
        reviewerReadAttempt,
        undefined,
        "Read tool call during critical review must be permitted for the reviewer child",
      );

      clearActiveReviews();
      s.inCriticalReview = false;
    },
  );

  // -----------------------------------------------------------------------
  // 27. Normal implementation subagent remains allowed to mutate when implementation gate is open
  // -----------------------------------------------------------------------
  await t.step(
    "27. normal implementation subagent remains allowed to mutate when implementation gate is open",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_27");
      plugin(mockPi);

      const s = getState(ctx);
      s.active = "normal-impl-quest";
      s.questId = "normal-impl-quest";
      s.stack = [s.active];
      s.researchComplete = true;
      s.researchRequired = false;
      s.reassessmentRequired = false;
      s.awaitingUserConfirmation = false;
      s.inCriticalReview = false;

      const qPath = `${currentDir}/${s.active}/quest.md`;
      await mkdir(`${currentDir}/${s.active}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${s.active}\n\n## Goal\nNormal\n\n## Original request\n> Normal\n`,
        "utf8",
      );

      // Subagent tool classification for normal developer subagent
      const normalSubagentPerm = classifyToolCall("subagent", {
        agent: "developer",
        task: "Implement feature",
      });
      assert.strictEqual(
        normalSubagentPerm,
        "implementation",
        "Normal subagent must be classified as implementation",
      );

      const toolCallHandler = handlers["tool_call"]?.[0];
      assert.ok(toolCallHandler);

      // Implementation subagent allowed
      const subagentCallRes = await toolCallHandler({
        toolName: "subagent",
        input: { agent: "developer", task: "Implement feature" },
      }, ctx);
      assert.strictEqual(
        subagentCallRes,
        undefined,
        "Normal implementation subagent must be allowed when gate is open",
      );

      // Mutating tools allowed for normal implementation
      const editRes = await toolCallHandler({
        toolName: "edit",
        input: { path: "src/feature.ts" },
      }, ctx);
      assert.strictEqual(
        editRes,
        undefined,
        "Edit tool must be allowed when implementation gate is open",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 28. Final critical-review PASS does not allow archival when an ordinary Quest Journal completion condition is still unmet
  // -----------------------------------------------------------------------
  await t.step(
    "28. final critical-review PASS does not allow archival when an ordinary Quest Journal completion condition is still unmet",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_28");
      const s = getState(ctx);
      const slug = "unmet-conditions-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.dirty = false;
      s.researchComplete = true;
      s.researchRequired = false;

      // Quest file has an uncompleted Remaining Work item (- [ ])
      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nImplement XYZ\n\n## Original request\n> Implement XYZ\n\n## Current Status\n- [ ] in progress\n\n## Remaining work\n- [ ] Finish audio codec implementation\n`,
        "utf8",
      );

      const passRunner = async () =>
        `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nORIGINAL-REQUEST CHECK:\n- Requirement: XYZ -> Evidence: done -> Satisfied: YES\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      setCustomSubagentRunner(passRunner);

      const archiveRes = await archiveQuestFile(slug, mockPi, ctx);
      assert.strictEqual(
        archiveRes.success,
        false,
        "Archival must be blocked when ordinary completion conditions are unmet",
      );
      assert.ok(
        archiveRes.message.includes("Unfinished tasks in Remaining Work"),
        "Message must specify unmet Remaining Work condition",
      );

      const questStillExists = await readFile(qPath, "utf8");
      assert.ok(
        questStillExists.includes("Finish audio codec implementation"),
        "Quest file must not be deleted",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 29. Final PASS + all ordinary conditions allows archival
  // -----------------------------------------------------------------------
  await t.step(
    "29. final PASS + all ordinary conditions allows archival",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_29");
      const s = getState(ctx);
      const slug = "full-pass-completion-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.dirty = false;
      s.researchComplete = true;
      s.researchRequired = false;
      s.reassessmentRequired = false;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nImplement XYZ\n\n## Original request\n> Implement XYZ\n\n## Current Status\n- [x] completed\n\n## Remaining work\n- [x] Finish audio codec implementation\n`,
        "utf8",
      );

      const passRunner = async () =>
        `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nORIGINAL-REQUEST CHECK:\n- Requirement: XYZ -> Evidence: verified -> Satisfied: YES\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      setCustomSubagentRunner(passRunner);

      const archiveRes = await archiveQuestFile(slug, mockPi, ctx);
      assert.strictEqual(
        archiveRes.success,
        true,
        "Archival must succeed when review passes AND ordinary conditions hold",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 30. turn_start increments execution turn exactly once per actual Pi turn
  // -----------------------------------------------------------------------
  await t.step(
    "30. turn_start increments execution turn exactly once per actual Pi turn",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_30");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "turn-start-test-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.currentTurn = 0;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nTurn test\n\n## Original request\n> Turn test\n`,
        "utf8",
      );

      const turnStartHandler = handlers["turn_start"]?.[0];
      assert.ok(turnStartHandler, "turn_start handler must be registered");

      // Fire turn 1
      await turnStartHandler({ turnIndex: 1, timestamp: Date.now() }, ctx);
      assert.strictEqual(s.currentTurn, 1, "turn_start must set turn to 1");
      const corr1 = s.currentTurnCorrelationId;
      assert.ok(
        corr1?.startsWith("turn_1_"),
        "correlation ID must reflect turn 1",
      );

      // Fire turn 2
      await turnStartHandler({ turnIndex: 2, timestamp: Date.now() }, ctx);
      assert.strictEqual(s.currentTurn, 2, "turn_start must set turn to 2");
      const corr2 = s.currentTurnCorrelationId;
      assert.ok(
        corr2?.startsWith("turn_2_"),
        "correlation ID must reflect turn 2",
      );
      assert.notStrictEqual(
        corr1,
        corr2,
        "correlation IDs across turns must be distinct",
      );

      const log = readQuestLog(getQuestLogPath(slug));
      assert.ok(log.includes("turn=1"), "Log must include turn=1");
      assert.ok(log.includes("turn=2"), "Log must include turn=2");
    },
  );

  // -----------------------------------------------------------------------
  // 31. One user prompt producing 10 LLM/tool turns results in 10 distinct turn correlations, not turn=1 ten times
  // -----------------------------------------------------------------------
  await t.step(
    "31. one user prompt producing 10 LLM/tool turns results in 10 distinct turn correlations, not turn=1 ten times",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_31");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "multi-turn-prompt-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = ["Build multi-step feature"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nMulti turn\n\n## Original request\n> Build multi-step feature\n`,
        "utf8",
      );

      const beforeAgentStartHandler = handlers["before_agent_start"]?.[0];
      const turnStartHandler = handlers["turn_start"]?.[0];
      const turnEndHandler = handlers["turn_end"]?.[0];

      assert.ok(beforeAgentStartHandler && turnStartHandler && turnEndHandler);

      // Single user prompt starts agent run
      await beforeAgentStartHandler(
        { prompt: "Build multi-step feature" },
        ctx,
      );

      const observedTurnNumbers: number[] = [];
      const observedCorrelationIds: string[] = [];

      // Simulate 10 autonomous turns inside this prompt
      for (let i = 1; i <= 10; i++) {
        await turnStartHandler({ turnIndex: i, timestamp: Date.now() }, ctx);
        observedTurnNumbers.push(s.currentTurn || 0);
        observedCorrelationIds.push(s.currentTurnCorrelationId || "");

        await turnEndHandler({
          turnIndex: i,
          toolResults: [
            {
              toolName: "read",
              input: { path: `file_${i}.txt` },
              content: [{ type: "text", text: "ok" }],
            },
          ],
        }, ctx);
      }

      assert.deepStrictEqual(observedTurnNumbers, [
        1,
        2,
        3,
        4,
        5,
        6,
        7,
        8,
        9,
        10,
      ], "Turn numbers must progress from 1 to 10");
      const uniqueCorrelations = new Set(observedCorrelationIds);
      assert.strictEqual(
        uniqueCorrelations.size,
        10,
        "All 10 correlation IDs must be distinct",
      );

      const log = readQuestLog(getQuestLogPath(slug));
      const turnStartMatches = log.match(/TURN_START/g) || [];
      const turnEndMatches = log.match(/TURN_END/g) || [];
      assert.strictEqual(
        turnStartMatches.length,
        10,
        "Must log exactly 10 TURN_START events",
      );
      assert.strictEqual(
        turnEndMatches.length,
        10,
        "Must log exactly 10 TURN_END events",
      );

      // Ensure turn=1 is not logged 10 times for turn ends
      for (let i = 1; i <= 10; i++) {
        assert.ok(log.includes(`turn=${i}`), `Log must include turn=${i}`);
      }
    },
  );

  // -----------------------------------------------------------------------
  // 32. before_agent_start does not create another execution turn
  // -----------------------------------------------------------------------
  await t.step(
    "32. before_agent_start does not create another execution turn",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_32");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "no-turn-in-before-agent-start";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.currentTurn = 4;
      s.currentTurnCorrelationId = "turn_4_fixed_test";

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nPrompt check\n\n## Original request\n> Prompt check\n`,
        "utf8",
      );

      const beforeAgentStartHandler = handlers["before_agent_start"]?.[0];
      assert.ok(beforeAgentStartHandler);

      const logBefore = readQuestLog(getQuestLogPath(slug));
      const turnStartCountBefore =
        (logBefore.match(/TURN_START/g) || []).length;

      // Trigger before_agent_start for a prompt
      await beforeAgentStartHandler({ prompt: "Refine something" }, ctx);

      assert.strictEqual(
        s.currentTurn,
        4,
        "before_agent_start must NOT increment currentTurn",
      );
      assert.strictEqual(
        s.currentTurnCorrelationId,
        "turn_4_fixed_test",
        "before_agent_start must NOT mutate currentTurnCorrelationId",
      );

      const logAfter = readQuestLog(getQuestLogPath(slug));
      const turnStartCountAfter = (logAfter.match(/TURN_START/g) || []).length;
      assert.strictEqual(
        turnStartCountAfter,
        turnStartCountBefore,
        "before_agent_start must NOT emit TURN_START log",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 33. Five actual substantive turns can trigger one direction review
  // -----------------------------------------------------------------------
  await t.step(
    "33. five actual substantive turns can trigger one direction review",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_33");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "five-turn-direction-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.reassessmentRequired = false;
      s.awaitingUserConfirmation = false;
      s.substantiveTurnsSinceCheckpoint = 0;
      s.prompts = ["Implement feature ABC"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nABC\n\n## Original request\n> Implement feature ABC\n\n## Plan\n1. Step\n\n## Remaining work\n- [ ] Task\n`,
        "utf8",
      );

      let reviewCalls = 0;
      const passRunner = async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(passRunner);

      const turnStartHandler = handlers["turn_start"]?.[0];
      const turnEndHandler = handlers["turn_end"]?.[0];

      // Run 4 substantive turns (turns 1..4)
      for (let i = 1; i <= 4; i++) {
        await turnStartHandler({ turnIndex: i, timestamp: Date.now() }, ctx);
        await turnEndHandler({
          turnIndex: i,
          toolResults: [
            {
              toolName: "bash",
              input: { command: `echo step ${i}` },
              content: [{ type: "text", text: `step ${i}` }],
            },
          ],
        }, ctx);
        assert.strictEqual(
          reviewCalls,
          0,
          `Turn ${i} should not trigger direction review yet`,
        );
      }

      // Run 5th substantive turn
      await turnStartHandler({ turnIndex: 5, timestamp: Date.now() }, ctx);
      await turnEndHandler({
        turnIndex: 5,
        toolResults: [
          {
            toolName: "bash",
            input: { command: "echo step 5" },
            content: [{ type: "text", text: "step 5" }],
          },
        ],
      }, ctx);

      assert.strictEqual(
        reviewCalls,
        1,
        "5th substantive turn must trigger direction review",
      );
      assert.strictEqual(s.lastCriticalReview?.verdict, "PASS");

      const log = readQuestLog(getQuestLogPath(slug));
      assert.ok(
        log.includes("NO_PROGRESS"),
        "Log must record NO_PROGRESS continuation anomaly at 5 turns",
      );
      assert.ok(
        log.includes("CRITICAL_REVIEW_STARTED"),
        "Log must record direction review started",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 34. Repeated additional turns do not trigger the same review again without a meaningful review/state boundary
  // -----------------------------------------------------------------------
  await t.step(
    "34. repeated additional turns do not trigger the same review again without a meaningful review/state boundary",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_34");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "no-repeat-review-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.reassessmentRequired = false;
      s.awaitingUserConfirmation = false;
      s.substantiveTurnsSinceCheckpoint = 0;
      s.planVersion = 1;
      s.prompts = ["Implement feature ABC"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nABC\n\n## Original request\n> Implement feature ABC\n\n## Plan\n1. Step\n\n## Remaining work\n- [ ] Task\n`,
        "utf8",
      );

      let reviewCalls = 0;
      const passRunner = async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(passRunner);

      const turnStartHandler = handlers["turn_start"]?.[0];
      const turnEndHandler = handlers["turn_end"]?.[0];

      // Turns 1..5: triggers 1 review at turn 5
      for (let i = 1; i <= 5; i++) {
        await turnStartHandler({ turnIndex: i, timestamp: Date.now() }, ctx);
        await turnEndHandler({
          turnIndex: i,
          toolResults: [{
            toolName: "bash",
            input: { command: `echo ${i}` },
            content: [{ type: "text", text: "ok" }],
          }],
        }, ctx);
      }
      assert.strictEqual(
        reviewCalls,
        1,
        "Should have triggered exactly 1 review at turn 5",
      );

      // Turns 6..10 without state save or plan revision: review must NOT repeat
      for (let i = 6; i <= 10; i++) {
        await turnStartHandler({ turnIndex: i, timestamp: Date.now() }, ctx);
        await turnEndHandler({
          turnIndex: i,
          toolResults: [{
            toolName: "bash",
            input: { command: `echo ${i}` },
            content: [{ type: "text", text: "ok" }],
          }],
        }, ctx);
      }
      assert.strictEqual(
        reviewCalls,
        1,
        "Review must NOT be repeated without a meaningful state boundary",
      );

      // Now update state with a new plan version
      await executeUpdateStateTool(
        {
          name: slug,
          plan: ["1. New Step A", "2. New Step B"],
          planRevisions: ["Revised plan to address new constraints"],
          planVersion: 2,
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        reviewCalls,
        2,
        "A new meaningful state version boundary permits a new direction review",
      );
      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 35. pi.getAllTools() containing 'subagent' is detected
  // -----------------------------------------------------------------------
  await t.step(
    "35. pi.getAllTools() containing 'subagent' is detected",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_35");
      setCustomSubagentRunner(null);

      // When subagent is listed in getAllTools()
      setAllTools([
        { name: "subagent", description: "Delegate to subagents" },
        { name: "bash", description: "Run bash" },
        { name: "read", description: "Read files" },
      ]);
      assert.strictEqual(
        isSubagentToolRegistered(mockPi, ctx),
        true,
        "subagent must be detected when listed in pi.getAllTools()",
      );

      // When subagent is NOT listed in getAllTools()
      setAllTools([
        { name: "bash", description: "Run bash" },
        { name: "read", description: "Read files" },
      ]);
      assert.strictEqual(
        isSubagentToolRegistered(mockPi, ctx),
        false,
        "subagent must be reported as not registered when absent from getAllTools()",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 36. A listed-but-unexecutable subagent is treated as unavailable
  // -----------------------------------------------------------------------
  await t.step(
    "36. a listed-but-unexecutable subagent is treated as unavailable",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_36");
      setCustomSubagentRunner(null);

      // Tool listed in getAllTools() but no executeTool or bridge provided
      setAllTools([{ name: "subagent", description: "Delegate to subagents" }]);
      (mockPi as any).events = null;

      const s = getState(ctx);
      const slug = "unexecutable-subagent-quest";
      s.active = slug;
      s.questId = slug;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nTest\n\n## Original request\n> Test\n`,
        "utf8",
      );

      assert.strictEqual(
        isSubagentToolRegistered(mockPi, ctx),
        true,
        "Tool must be registered",
      );
      assert.strictEqual(
        isSubagentAvailable(mockPi, ctx),
        false,
        "Tool must NOT be available because it is unexecutable",
      );

      const result = await runCriticalReview(mockPi, ctx, {
        kind: "direction",
        questSlug: slug,
      });
      assert.strictEqual(
        result.available,
        false,
        "Result must report available: false",
      );
      assert.strictEqual(
        result.success,
        false,
        "Result must report success: false",
      );
      assert.strictEqual(result.error, "subagent_tool_not_executable");

      const log = readQuestLog(getQuestLogPath(slug));
      assert.ok(
        log.includes("CRITICAL_REVIEW_UNAVAILABLE"),
        "Log must record CRITICAL_REVIEW_UNAVAILABLE",
      );
      assert.ok(
        log.includes("subagent_tool_not_executable"),
        "Log must record reason=subagent_tool_not_executable",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 37. An actually executable subagent runs a direction review automatically
  // -----------------------------------------------------------------------
  await t.step(
    "37. an actually executable subagent runs a direction review automatically",
    async () => {
      const { mockPi, setAllTools, events } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_37");
      setCustomSubagentRunner(null);

      // Tool listed in getAllTools()
      setAllTools([{ name: "subagent", description: "Delegate to subagents" }]);

      // Set up structured delegation bridge response listener
      events.on("prompt-template:subagent:request", (data: any) => {
        events.emit("prompt-template:subagent:response", {
          requestId: data.requestId,
          ownerRunId: data.ownerRunId,
          nodeId: data.nodeId,
          status: "completed",
          result: {
            kind: "text",
            text:
              `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Tested assumptions: clean\n- Revised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`,
          },
        });
      });

      const s = getState(ctx);
      const slug = "executable-bridge-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.researchComplete = true;
      s.researchRequired = false;
      s.awaitingUserConfirmation = false;
      s.prompts = ["Implement streaming codec"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nStreaming\n\n## Original request\n> Implement streaming codec\n\n## Plan\n1. Design\n`,
        "utf8",
      );

      assert.strictEqual(
        isSubagentAvailable(mockPi, ctx),
        true,
        "Subagent must be available via event bridge",
      );

      const result = await checkAndTriggerDirectionReview(
        mockPi,
        ctx,
        "test_trigger",
      );
      assert.ok(result, "Direction review must execute");
      assert.strictEqual(result.success, true);
      assert.strictEqual(result.available, true);
      assert.strictEqual(result.review?.verdict, "PASS");
    },
  );

  // -----------------------------------------------------------------------
  // 38. The critical reviewer executes with read-only tool capabilities
  // -----------------------------------------------------------------------
  await t.step(
    "38. the critical reviewer executes with read-only tool capabilities",
    async () => {
      const { mockPi, setAllTools, events } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_38");
      setCustomSubagentRunner(null);

      setAllTools([{ name: "subagent", description: "Delegate to subagents" }]);

      let capturedRequest: any = null;
      events.on("prompt-template:subagent:request", (data: any) => {
        capturedRequest = data;
        events.emit("prompt-template:subagent:response", {
          requestId: data.requestId,
          ownerRunId: data.ownerRunId,
          nodeId: data.nodeId,
          status: "completed",
          result: {
            kind: "text",
            text:
              `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\nRevised Judgment: PASS\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`,
          },
        });
      });

      const s = getState(ctx);
      const slug = "readonly-critic-quest";
      s.active = slug;
      s.questId = slug;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nRead-only\n\n## Original request\n> Read-only\n`,
        "utf8",
      );

      await runCriticalReview(mockPi, ctx, {
        kind: "direction",
        questSlug: slug,
      });

      assert.ok(capturedRequest, "Subagent bridge must have been invoked");
      assert.strictEqual(
        capturedRequest.agent,
        "reviewer",
        "Must use configured reviewer agent ('reviewer')",
      );
      assert.deepStrictEqual(
        capturedRequest.result,
        { kind: "text" },
        "Critic must request a plain text result",
      );
      // Structured delegation does not carry a tools allow-list on the wire: the read-only
      // reviewer profile + acceptance gate (pi-subagents treats 'reviewer' as read-only) enforce it.
      assert.ok(
        !Object.hasOwn(capturedRequest, "tools"),
        "Structured request must not inject a tools array",
      );
      assert.ok(
        !Object.hasOwn(capturedRequest, "async"),
        "Structured request must not carry unsupported async field",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 39. A normal implementation subagent remains implementation-capable
  // -----------------------------------------------------------------------
  await t.step(
    "39. a normal implementation subagent remains implementation-capable",
    async () => {
      const normalDevInput = {
        agent: "developer",
        task: "Implement feature XYZ and edit files",
      };
      const normalWorkerInput = { agent: "worker", task: "Fix bug in module" };
      const criticInput = {
        agent: "critic",
        task: "[CRITICAL REVIEW] Direction Review",
        isCriticalReview: true,
      };

      assert.strictEqual(
        classifyToolCall("subagent", normalDevInput),
        "implementation",
        "developer subagent must be implementation",
      );
      assert.strictEqual(
        classifyToolCall("subagent", normalWorkerInput),
        "implementation",
        "worker subagent must be implementation",
      );
      assert.strictEqual(
        classifyToolCall("subagent", criticInput),
        "research",
        "critic subagent must be research/read",
      );

      assert.strictEqual(
        isCriticalReviewSubagentInvocation(normalDevInput),
        false,
        "normal developer is not critical review invocation",
      );
      assert.strictEqual(
        isCriticalReviewSubagentInvocation(normalWorkerInput),
        false,
        "normal worker is not critical review invocation",
      );
      assert.strictEqual(
        isCriticalReviewSubagentInvocation(criticInput),
        true,
        "critic is critical review invocation",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 40. A real critical review emits the complete CRITICAL_REVIEW lifecycle in execution.log
  // -----------------------------------------------------------------------
  await t.step(
    "40. a real critical review emits the complete CRITICAL_REVIEW lifecycle in execution.log",
    async () => {
      const { mockPi, setAllTools, events } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_40");
      setCustomSubagentRunner(null);

      setAllTools([{ name: "subagent" }]);

      events.on("prompt-template:subagent:request", (data: any) => {
        events.emit("prompt-template:subagent:response", {
          requestId: data.requestId,
          ownerRunId: data.ownerRunId,
          nodeId: data.nodeId,
          status: "completed",
          result: {
            kind: "text",
            text: `PASS 1 (Provisional Inspection):
Provisional Judgment: PASS
Provisional Summary: Looks initial pass ok.

PASS 2 (Self-Critique & Falsification):
- Tested assumption: buffer safety verified
- Revised Judgment: PASS

ORIGINAL-REQUEST CHECK:
- Requirement: Zero allocs -> Evidence: static buffer -> Satisfied: YES

VERDICT: PASS
SEVERITY: NONE

FINDINGS:
- None

REQUIRED ACTIONS:
- Continue`,
          },
        });
      });

      const s = getState(ctx);
      const slug = "lifecycle-log-quest";
      s.active = slug;
      s.questId = slug;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nLifecycle test\n\n## Original request\n> Lifecycle test\n`,
        "utf8",
      );

      await runCriticalReview(mockPi, ctx, {
        kind: "direction",
        questSlug: slug,
      });

      const log = readQuestLog(getQuestLogPath(slug));
      assert.ok(
        log.includes("CRITICAL_REVIEW_REQUESTED"),
        "Must include CRITICAL_REVIEW_REQUESTED",
      );
      assert.ok(
        log.includes("CRITICAL_REVIEW_STARTED"),
        "Must include CRITICAL_REVIEW_STARTED",
      );
      assert.ok(
        log.includes("TOOL_ACTIVITY"),
        "Must include TOOL_ACTIVITY for subagent",
      );
      assert.ok(
        log.includes("SELF_CRITIQUE_STARTED"),
        "Must include SELF_CRITIQUE_STARTED",
      );
      assert.ok(
        log.includes("CRITICAL_REVIEW_PASSED"),
        "Must include CRITICAL_REVIEW_PASSED",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 41. Final acceptance remains separate from direction reviews
  // -----------------------------------------------------------------------
  await t.step(
    "41. final acceptance remains separate from direction reviews",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_test_41");
      const s = getState(ctx);
      const slug = "separate-reviews-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.dirty = false;
      s.researchComplete = true;
      s.researchRequired = false;

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nFeature X\n\n## Original request\n> Feature X\n\n## Current Status\n- [x] completed\n\n## Remaining work\n- [x] Done\n`,
        "utf8",
      );

      // Record a PASS for direction review
      s.lastCriticalReview = {
        id: "rev_dir_1",
        questId: slug,
        kind: "direction", // Direction review, NOT final_acceptance
        reviewedStateVersion: { planVersion: 1 },
        verdict: "PASS",
        severity: "NONE",
        findings: [],
        requiredActions: [],
        resolved: true,
        timestamp: Date.now(),
      };

      assert.strictEqual(
        isCriticalReviewValidForCompletion(s),
        false,
        "Direction review PASS must NOT satisfy final acceptance completion gate",
      );

      // Now run with a final_acceptance runner
      let invokedKind: string | null = null;
      const finalRunner = async (task: string, options: any) => {
        invokedKind = options?.reviewKind || options?.kind;
        return `PASS 1:\nProvisional Judgment: PASS\nPASS 2:\n- Revised Judgment: PASS\nORIGINAL-REQUEST CHECK:\n- Requirement: Feature X -> Evidence: verified -> Satisfied: YES\nVERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
      };
      setCustomSubagentRunner(finalRunner);

      const archiveRes = await archiveQuestFile(slug, mockPi, ctx);
      assert.strictEqual(
        archiveRes.success,
        true,
        "Archival must succeed after final_acceptance review runs and passes",
      );
      assert.strictEqual(
        invokedKind,
        "final_acceptance",
        "Archival must invoke review with kind: final_acceptance",
      );
      assert.strictEqual(s.lastCriticalReview?.kind, "final_acceptance");
      setCustomSubagentRunner(null);
    },
  );

  // Cleanup test directories
  const testDirs = [
    "stream-audio-engine",
    "fix-typo",
    "unsupported-assumption-quest",
    "remediation-quest",
    "uncertain-quest",
    "root-fail-quest",
    "root-pass-quest",
    "persist-review-quest",
    "rebuttal-quest",
    "bounded-loop-quest",
    "log-event-quest",
    "quest-alpha",
    "quest-beta",
    "default",
    "error-quest",
    "auto-direction-quest",
    "dedup-direction-quest",
    "plan-version-quest",
    "read-only-guard-quest",
    "normal-impl-quest",
    "unmet-conditions-quest",
    "full-pass-completion-quest",
    "turn-start-test-quest",
    "multi-turn-prompt-quest",
    "no-turn-in-before-agent-start",
    "five-turn-direction-quest",
    "no-repeat-review-quest",
    "unexecutable-subagent-quest",
    "executable-bridge-quest",
    "readonly-critic-quest",
    "retry-direction-quest",
    "lifecycle-log-quest",
    "separate-reviews-quest",
  ];
  for (const d of testDirs) {
    await rm(`${currentDir}/${d}`, { recursive: true, force: true });
  }
});
