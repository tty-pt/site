import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import plugin, {
  acceptRootConfirmation,
  asyncContext,
  barIcon,
  buildCriticalReviewPrompt,
  canImplement,
  canToolExecuteInCriticalReview,
  cancelIdentityMap,
  checkAndTriggerPlanReview,
  classifyToolCall,
  clearActiveReviews,
  ensureQuestId,
  executeMarkTool,
  executeUpdateStateTool,
  formatQuestShort,
  type ExtensionAPI,
  type ExtensionContext,
  getActiveReviews,
  getImplementationBlockReason,
  getPendingReviews,
  getQuestLogPath,
  getState,
  isActionableDraftPlan,
  isActionablePlanContent,
  isDraftRevisionOutstanding,
  isFutureDraftPath,
  isPlanReviewValidForState,
  isSubagentAvailable,
  isSubagentToolRegistered,
  parseCriticalReviewResponse,
  promoteDraft,
  QuestErrorCode,
  readQuestLog,
  reconstruct,
  requestPlanReview,
  setCustomSubagentRunner,
  snapshotState,
  type StoredState,
  __resetCoalesceSteerForTests,
  handleToolResult,
  handleTurnStart,
  handleTurnEnd,
  normalizeReviewModel,
  resolveDefaultReviewModel,
  isModelResolutionOrProviderError,
  PiSubagentReviewer,
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

Deno.test("Adversarial Plan Review Suite: Comprehensive Verification of Planning Gates & Reviewer Self-Attack", async (t) => {
  const currentDir = ".pi/quest/current";
  await mkdir(currentDir, { recursive: true });

  setCustomSubagentRunner(null);

  // -----------------------------------------------------------------------
  // 1. Plan draft is not complete merely because main agent claims ready
  // -----------------------------------------------------------------------
  await t.step(
    "1. plan draft is not complete merely because main agent claims ready",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_1");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "audio-chunker-plan";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = [
        "Build an async audio chunker with ring buffer and backpressure handling.",
      ];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nAudio chunker\n\n## Original request\n> Build an async audio chunker with ring buffer and backpressure handling.\n\n## Plan\n1. Simple chunking without buffer\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      // Reviewer rejects because backpressure / ring buffer is omitted from plan
      let reviewerCalled = false;
      const rejectRunner = async () => {
        reviewerCalled = true;
        return `PASS 1 (Independent Evaluation):
Provisional Judgment: REVISE
Provisional Summary: Plan omitted ring buffer and backpressure.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Is ring buffer strictly required?
- Invalidation risk: User explicitly requested ring buffer and backpressure handling.
- Revised Judgment: REVISE

PROMPT-COMPLIANCE:
- Requirement: Async audio chunker -> Plan Handling: Addressed in step 1 -> Status: SATISFIED
- Requirement: Ring buffer -> Plan Handling: Not mentioned in plan -> Status: UNSATISFIED
- Requirement: Backpressure handling -> Plan Handling: Omitted -> Status: UNSATISFIED

VERDICT: REVISE
SEVERITY: CRITICAL

FINDINGS:
- Issue: Plan omits ring buffer and backpressure handling
  Evidence: Step 1 uses naive chunking without ring buffer structure

REQUIRED REVISIONS:
- Add ring buffer data structure step
- Add backpressure callback handling step`;
      };
      setCustomSubagentRunner(rejectRunner);

      // Agent attempts to complete research with plan draft
      await executeUpdateStateTool(
        {
          name: slug,
          plan: ["1. Simple chunking without buffer"],
          planConfidence: "high",
          planConfidenceReason:
            "Main agent claims high confidence and tested assumptions.",
          researchComplete: true,
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        reviewerCalled,
        true,
        "Independent reviewer must be invoked",
      );
      assert.strictEqual(
        s.researchComplete,
        false,
        "researchComplete must NOT be set when reviewer issues REVISE",
      );
      assert.strictEqual(
        s.researchRequired,
        true,
        "researchRequired must remain true",
      );
      assert.strictEqual(
        canImplement(s, ctx),
        false,
        "Implementation gate must remain blocked",
      );

      const blockReason = getImplementationBlockReason(s, ctx);
      assert.strictEqual(blockReason.blocked, true);
      assert.strictEqual(blockReason.code, QuestErrorCode.PLAN_REVIEW_REQUIRED);

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 2. Reviewer prompt receives exact prompt, 11-point criteria & 2-pass instructions
  // -----------------------------------------------------------------------
  await t.step(
    "2. reviewer prompt receives exact prompt, 11-point criteria & 2-pass instructions",
    async () => {
      const originalPrompt =
        "Implement zero-copy audio stream muxer with sample-rate conversion";
      const prompt = buildCriticalReviewPrompt(
        "plan_review",
        "stream-muxer-quest",
        {
          originalRequest: originalPrompt,
          refinements: ["Must support 48kHz and 96kHz rates"],
          currentUnderstanding: "axil audio muxing architecture",
          keyAssumptions: "Zero-copy requires aligned memory pools",
          openQuestions: "Latency budget < 5ms",
          plan: "1. Memory pool setup\n2. SRC filter\n3. Mux loop",
          planConfidence: "high",
          planRevisions: "Initial plan",
          findings: "SIMD acceleration available for SRC",
          filesModified: "",
          testStatus: "Unit tests ready",
          executionSnapshot: "Research complete",
          exactNextAction: "Begin implementation",
          remainingWork: "- [ ] Step 1\n- [ ] Step 2",
          status: "Research complete",
        },
      );

      assert.ok(
        prompt.includes(originalPrompt),
        "Prompt must include verbatim user prompt",
      );
      assert.ok(
        prompt.includes("WHAT YOU MUST EVALUATE (PLAN REVIEW):"),
        "Prompt must include 11-point plan evaluation checklist",
      );
      assert.ok(
        prompt.includes(
          "1. Whether the plan actually addresses the user's objective;",
        ),
        "Must include criteria 1",
      );
      assert.ok(
        prompt.includes(
          "10. Whether the plan contains contradictions or internally incompatible steps;",
        ),
        "Must include criteria 10",
      );
      assert.ok(
        prompt.includes(
          "11. Whether the plan provides a credible path to satisfying the original request.",
        ),
        "Must include criteria 11",
      );
      assert.ok(
        prompt.includes("Reviewer Preference (MUST NOT block a plan)"),
        "Must instruct reviewer not to block on preference",
      );
      assert.ok(
        prompt.includes("TWO-PASS SELF-ATTACK REQUIREMENT:"),
        "Must instruct 2-pass self-attack",
      );
      assert.ok(
        prompt.includes("What requirement could this plan still be missing?"),
        "Must include self-attack question 1",
      );
      assert.ok(
        prompt.includes("What would make this plan wrong?"),
        "Must include self-attack question 4",
      );
      assert.ok(
        prompt.includes("Do not trust the main agent's summary or claims"),
        "Must instruct untrusted inspection",
      );
    },
  );

  await t.step(
    "2b. reviewer prompt §13 states the true serialization invariant, no falsehoods (T-F6)",
    async () => {
      const prompt = buildCriticalReviewPrompt(
        "plan_review",
        "stream-muxer-quest",
        {
          originalRequest: "Implement zero-copy audio stream muxer",
          refinements: [],
          currentUnderstanding: "",
          keyAssumptions: "",
          openQuestions: "",
          plan: "1. Memory pool setup\n2. SRC filter\n3. Mux loop",
          planConfidence: "high",
          planRevisions: "Initial",
          findings: "",
          filesModified: "",
          testStatus: "",
          executionSnapshot: "",
          exactNextAction: "",
          remainingWork: "",
          status: "",
        },
      );
      assert.ok(
        prompt.includes("REVIEWER SERIALIZATION"),
        "§13 must state the serialization invariant",
      );
      assert.ok(
        /at most ONE active\s+Critical Review per quest/i.test(prompt),
        "§13 must state single-review-per-quest invariant",
      );
      assert.ok(
        /global cap of 1 review/i.test(prompt),
        "§13 must state the global cap of 1",
      );
      assert.ok(
        !prompt.includes("3 concurrent"),
        "§13 must not contain the '3 concurrent' falsehood",
      );
      assert.ok(
        !prompt.includes("maxConcurrency = 3"),
        "§13 must not contain the 'maxConcurrency = 3' falsehood",
      );
      assert.ok(
        !prompt.includes("current implementation is wrong"),
        "§13 must not claim the implementation is wrong",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 3. Reviewer self-attack overturning initial PASS to REVISE blocks the plan
  // -----------------------------------------------------------------------
  await t.step(
    "3. reviewer self-attack overturning initial PASS to REVISE blocks the plan",
    async () => {
      const rawResponse = `PASS 1 (Independent Evaluation):
Provisional Judgment: APPROVE
Provisional Summary: Plan looks comprehensive on first pass.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Did the plan account for sample-rate conversion filter latency?
- Invalidation risk: Without latency compensation, multi-track audio will drift out of sync.
- Revised Judgment: REVISE

PROMPT-COMPLIANCE:
- Requirement: Zero-copy muxer -> Plan Handling: Covered in step 1 -> Status: SATISFIED
- Requirement: Sample-rate conversion -> Plan Handling: Covered in step 2 -> Status: SATISFIED
- Requirement: Multi-track sync -> Plan Handling: Latency compensation omitted -> Status: UNSATISFIED

VERDICT: REVISE
SEVERITY: MAJOR

FINDINGS:
- Issue: Missing filter latency compensation causes multi-track drift
  Evidence: Plan step 2 does not buffer delay compensation

REQUIRED REVISIONS:
- Add multi-track latency delay line step to plan`;

      const parsed = parseCriticalReviewResponse(rawResponse);
      assert.strictEqual(parsed.selfCritique?.initialJudgment, "APPROVE");
      assert.strictEqual(parsed.selfCritique?.revisedJudgment, "REVISE");
      assert.strictEqual(parsed.verdict, "REVISE");
      assert.strictEqual(parsed.severity, "MAJOR");
      assert.strictEqual(parsed.findings.length, 1);
      assert.ok(
        parsed.findings[0].issue.includes(
          "Missing filter latency compensation",
        ),
      );
      assert.strictEqual(parsed.requiredActions.length, 1);
      assert.ok(parsed.requiredActions[0].includes("delay line step"));
    },
  );

  // -----------------------------------------------------------------------
  // 4. Reviewer approval allows planning phase to complete
  // -----------------------------------------------------------------------
  await t.step(
    "4. reviewer approval allows planning phase to complete",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_4");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);

      const s = getState(ctx);
      const slug = "approved-muxer-plan";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = [
        "Implement zero-copy audio stream muxer with sample-rate conversion and delay compensation.",
      ];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nAudio muxer\n\n## Original request\n> Implement zero-copy audio stream muxer with sample-rate conversion and delay compensation.\n\n## Plan\n1. Memory pool setup\n2. SRC filter with delay line\n3. Mux loop\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      const approveRunner = async () =>
        `PASS 1 (Independent Evaluation):
Provisional Judgment: APPROVE
Provisional Summary: Plan satisfies all requirements.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Memory alignment verified against libxylem specs.
- Invalidation risk: None found.
- Revised Judgment: APPROVE

PROMPT-COMPLIANCE:
- Requirement: Zero-copy muxer -> Plan Handling: Addressed in step 1 -> Status: SATISFIED
- Requirement: Sample-rate conversion -> Plan Handling: Addressed in step 2 -> Status: SATISFIED
- Requirement: Delay compensation -> Plan Handling: Addressed in step 2 -> Status: SATISFIED

VERDICT: APPROVE
SEVERITY: NONE

FINDINGS:
- None

REQUIRED REVISIONS:
- None`;
      setCustomSubagentRunner(approveRunner);

      await executeUpdateStateTool(
        {
          name: slug,
          plan: [
            "1. Memory pool setup",
            "2. SRC filter with delay line",
            "3. Mux loop",
          ],
          planConfidence: "high",
          planConfidenceReason: "Verified alignment and delay calculations.",
          researchComplete: true,
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        s.researchComplete,
        true,
        "researchComplete must be true after APPROVE",
      );
      assert.strictEqual(s.researchRequired, false);
      assert.strictEqual(
        isPlanReviewValidForState(s),
        true,
        "Plan review approval must be valid",
      );
      assert.strictEqual(s.lastPlanReviewApproval?.planVersion, 1);
      assert.ok(s.lastPlanReviewApproval?.reviewId);

      // Awaiting confirmation for root quest
      assert.strictEqual(s.awaitingUserConfirmation, true);

      // Accept user confirmation
      await acceptRootConfirmation(mockPi, ctx);
      assert.strictEqual(
        canImplement(s, ctx),
        true,
        "Implementation gate must open after plan approval + confirmation",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 5. Main-agent revision loop (P1 REVISE -> P2 APPROVE)
  // -----------------------------------------------------------------------
  await t.step(
    "5. main-agent revision loop (P1 REVISE -> P2 APPROVE)",
    async () => {
      const { mockPi, setAllTools, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_5");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);

      const s = getState(ctx);
      const slug = "revision-loop-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = [
        "Implement HTTP/2 multiplexed stream parser with flow control.",
      ];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nHTTP/2 parser\n\n## Original request\n> Implement HTTP/2 multiplexed stream parser with flow control.\n\n## Plan\n1. Frame parser\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let callCount = 0;
      const dynamicRunner = async (_task: string) => {
        callCount++;
        if (callCount === 1) {
          // Draft 1 rejected
          return `PASS 1:\nProvisional Judgment: REVISE\nPASS 2:\n- Revised Judgment: REVISE\nPROMPT-COMPLIANCE:\n- Requirement: Flow control -> Plan Handling: Missing -> Status: UNSATISFIED\nVERDICT: REVISE\nSEVERITY: CRITICAL\nFINDINGS:\n- Issue: Flow control missing from plan\n  Evidence: Step 1 lacks WINDOW_UPDATE handling\nREQUIRED REVISIONS:\n- Add WINDOW_UPDATE flow control state machine`;
        }
        // Draft 2 approved
        return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Flow control -> Plan Handling: WINDOW_UPDATE added in step 2 -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      };
      setCustomSubagentRunner(dynamicRunner);

      // Draft 1
      await executeUpdateStateTool(
        {
          name: slug,
          plan: ["1. Frame parser"],
          planVersion: 1,
          researchComplete: true,
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(callCount, 1);
      assert.strictEqual(s.researchComplete, false);
      assert.strictEqual(isPlanReviewValidForState(s), false);

      // Verify steer message was delivered to main agent
      const steerMsg = agentMessages.find((m) =>
        m.msg?.includes("ADVERSARIAL PLAN REVIEW REJECTED")
      );
      assert.ok(
        steerMsg,
        "Steer message must be delivered to main agent with findings",
      );
      assert.ok(steerMsg.msg.includes("Flow control missing from plan"));
      assert.ok(
        steerMsg.msg.includes("Add WINDOW_UPDATE flow control state machine"),
      );

      // Draft 2 addressing findings
      await executeUpdateStateTool(
        {
          name: slug,
          plan: [
            "1. Frame parser",
            "2. WINDOW_UPDATE flow control state machine",
          ],
          planRevisions: [
            "Added flow control state machine per reviewer findings",
          ],
          planVersion: 2,
          researchComplete: true,
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        callCount,
        2,
        "Second review must execute for revised plan version",
      );
      assert.strictEqual(
        s.researchComplete,
        true,
        "Plan version 2 must be approved",
      );
      assert.strictEqual(isPlanReviewValidForState(s), true);
      assert.strictEqual(s.lastPlanReviewApproval?.planVersion, 2);

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 6. Material plan change after approval invalidates previous approval
  // -----------------------------------------------------------------------
  await t.step(
    "6. material plan change after approval invalidates previous approval",
    async () => {
      const s: StoredState = {
        active: "invalidate-plan-quest",
        questId: "invalidate-plan-quest",
        saveCount: 1,
        compactCount: 0,
        prompts: ["Test prompt"],
        stack: ["invalidate-plan-quest"],
        dirty: false,
        planVersion: 1,
        lastSavedHash: "hash_plan_v1",
        lastPlanReviewApproval: {
          questId: "invalidate-plan-quest",
          planVersion: 1,
          reviewId: "rev_app_1",
          saveHash: "hash_plan_v1",
          saveCount: 1,
          timestamp: Date.now(),
        },
      };

      assert.strictEqual(
        isPlanReviewValidForState(s),
        true,
        "Approval must be valid before modification",
      );

      // Change 1: Plan version increments
      s.planVersion = 2;
      assert.strictEqual(
        isPlanReviewValidForState(s),
        false,
        "Plan version mismatch must invalidate approval",
      );
      s.planVersion = 1;

      // Change 2: Save hash mismatch
      s.lastSavedHash = "hash_plan_v2";
      assert.strictEqual(
        isPlanReviewValidForState(s),
        false,
        "Save hash mismatch must invalidate approval",
      );
      s.lastSavedHash = "hash_plan_v1";

      // Change 3: Dirty state
      s.dirty = true;
      assert.strictEqual(
        isPlanReviewValidForState(s),
        false,
        "Dirty working state must invalidate approval",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 7. Reviewer error, timeout, or UNCERTAIN does not become approval
  // -----------------------------------------------------------------------
  await t.step(
    "7. reviewer error, timeout, or UNCERTAIN does not become approval",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_7");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);

      const s = getState(ctx);
      const slug = "error-uncertain-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = ["Build component X"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nX\n\n## Original request\n> Build component X\n\n## Plan\n1. X\n\n## Remaining work\n- [ ] 1\n`,
        "utf8",
      );

      // Case A: Reviewer throws error
      const errorRunner = async () => {
        throw new Error("Subagent execution timeout");
      };
      setCustomSubagentRunner(errorRunner);

      const errRes = await requestPlanReview(mockPi, ctx, slug);
      assert.strictEqual(errRes.success, false);
      assert.strictEqual(s.researchComplete, false);
      assert.strictEqual(isPlanReviewValidForState(s), false);

      // Case B: Reviewer returns UNCERTAIN
      const uncertainRunner = async () =>
        `PASS 1:\nProvisional Judgment: UNCERTAIN\nPASS 2:\n- Revised Judgment: UNCERTAIN\nPROMPT-COMPLIANCE:\n- Requirement: X -> Plan Handling: Unclear -> Status: UNCERTAIN\nVERDICT: UNCERTAIN\nSEVERITY: MAJOR\nFINDINGS:\n- Issue: Unknown dependency\n  Evidence: Missing header\nREQUIRED REVISIONS:\n- Verify header exists`;
      setCustomSubagentRunner(uncertainRunner);

      const uncRes = await requestPlanReview(mockPi, ctx, slug);
      assert.strictEqual(uncRes.success, false);
      assert.strictEqual(s.researchComplete, false);
      assert.strictEqual(isPlanReviewValidForState(s), false);

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 8. Repeated rejections hit loop bound without automatic approval
  // -----------------------------------------------------------------------
  await t.step(
    "8. repeated rejections hit loop bound without automatic approval",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_8");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);

      const s = getState(ctx);
      const slug = "loop-bound-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.planVersion = 1;
      s.lastSavedHash = "hash_fixed";
      s.prompts = ["Build component Y"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nY\n\n## Original request\n> Build component Y\n\n## Plan\n1. Y\n\n## Remaining work\n- [ ] 1\n`,
        "utf8",
      );

      const rejectRunner = async () =>
        `VERDICT: REVISE\nSEVERITY: CRITICAL\nFINDINGS:\n- Persistent flaw\nREQUIRED REVISIONS:\n- Fix`;
      setCustomSubagentRunner(rejectRunner);

      // 3 attempts
      await requestPlanReview(mockPi, ctx, slug);
      await requestPlanReview(mockPi, ctx, slug);
      await requestPlanReview(mockPi, ctx, slug);

      // 4th attempt exceeds bound
      const boundRes = await requestPlanReview(mockPi, ctx, slug);
      assert.strictEqual(boundRes.success, false);
      assert.ok(boundRes.error?.includes("bound"));
      assert.strictEqual(
        isPlanReviewValidForState(s),
        false,
        "Bound limit must NEVER convert to approval",
      );
      assert.strictEqual(
        canImplement(s, ctx),
        false,
        "Implementation gate must remain blocked",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 9. Plan review events are recorded in execution log
  // -----------------------------------------------------------------------
  await t.step(
    "9. plan review events are recorded in execution log",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_9");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);

      const s = getState(ctx);
      const slug = "plan-log-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = ["Build logged feature"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nLogged\n\n## Original request\n> Build logged feature\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] 1\n`,
        "utf8",
      );

      const passRunner = async () =>
        `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Logged feature -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      setCustomSubagentRunner(passRunner);

      await requestPlanReview(mockPi, ctx, slug);

      const log = readQuestLog(getQuestLogPath(slug));
      assert.ok(
        log.includes("PLAN_REVIEW_REQUESTED"),
        "Must log PLAN_REVIEW_REQUESTED",
      );
      assert.ok(
        log.includes("PLAN_REVIEW_STARTED"),
        "Must log PLAN_REVIEW_STARTED",
      );
      assert.ok(
        log.includes("PLAN_REVIEW_APPROVED"),
        "Must log PLAN_REVIEW_APPROVED",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 10. When subagent capability is genuinely not registered, normal Quest Journal behavior is preserved
  // -----------------------------------------------------------------------
  await t.step(
    "10. when subagent capability is genuinely not registered, normal Quest Journal behavior is preserved",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_10");
      plugin(mockPi);
      setCustomSubagentRunner(null);

      assert.strictEqual(isSubagentToolRegistered(mockPi, ctx), false);
      assert.strictEqual(isSubagentAvailable(mockPi, ctx), false);

      const s = getState(ctx);
      const slug = "unregistered-subagent-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.dirty = false;
      s.prompts = ["Build simple utility"];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nUtility\n\n## Original request\n> Build simple utility\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] 1\n`,
        "utf8",
      );

      // With subagent tool unregistered, review is gracefully skipped
      const reviewRes = await requestPlanReview(mockPi, ctx, slug);
      assert.strictEqual(reviewRes.available, false);
      assert.strictEqual(reviewRes.skipped, true);
      assert.strictEqual(reviewRes.success, true);
    },
  );

  // -----------------------------------------------------------------------
  // 10b. Draft branch with no registered reviewer returns null AND emits audit
  // log (covers #21: silent return null). The !registered early-return in the
  // draft shard must log REVIEW_DEDUP_HIT + CRITICAL_REVIEW_SUPPRESSED_DUPLICATE
  // -----------------------------------------------------------------------
  await t.step(
    "10b. checkAndTriggerPlanReview draft !registered returns null and logs REVIEW_DEDUP_HIT",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_10b");
      plugin(mockPi);
      setCustomSubagentRunner(null);

      const s = getState(ctx);
      const slug = "draft-unregistered-audit";
      const qid = `qid_10b_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];
      s.draftPrompts = ["prompt one"];

      // checkAndTriggerPlanReview reads the future draft to compute the hash
      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Requirements\n- prompt one\n`,
        "utf8",
      );

      // Ensure the quest log dir exists so logEvent writes to a real file (path derives from s.questId)
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      // No reviewer registered -> draft !registered branch returns null.
      // Wrap in asyncContext.run so the bare logEvent calls (which resolve the
      // log path from getActiveContext()) route to this session's questId,
      // matching how the framework invokes handlers via withContext (context.ts).
      const result = await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx),
      );
      assert.strictEqual(
        result,
        null,
        "must return null when no reviewer is registered",
      );

      // The previously-silent return must now emit structured audit events (#21)
      const logContent = await readFile(logPath, "utf8");
      assert.ok(
        logContent.includes("REVIEW_DEDUP_HIT"),
        "must log REVIEW_DEDUP_HIT for draft !registered",
      );
      assert.ok(
        logContent.includes("reason=not_registered"),
        "REVIEW_DEDUP_HIT must carry reason=not_registered",
      );
      assert.ok(
        logContent.includes("shard=draft"),
        "REVIEW_DEDUP_HIT must carry shard=draft",
      );
      assert.ok(
        logContent.includes("CRITICAL_REVIEW_SUPPRESSED_DUPLICATE"),
        "must log CRITICAL_REVIEW_SUPPRESSED_DUPLICATE",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 11. Edge-triggered plan review: 10 consecutive quest_update_state calls with unchanged plan
  // -----------------------------------------------------------------------
  await t.step(
    "11. edge-triggered plan review: 10 consecutive quest_update_state calls invoke reviewer exactly once",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_11");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);
      clearActiveReviews();

      const s = getState(ctx);
      const slug = "edge-triggered-10-updates-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = ["Implement edge-triggered plan review scheduler."];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nEdge triggered scheduler\n\n## Original request\n> Implement edge-triggered plan review scheduler.\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let reviewerInvocations = 0;
      const passRunner = async () => {
        reviewerInvocations++;
        return `PASS 1 (Independent Evaluation):
Provisional Judgment: APPROVE
Provisional Summary: Plan is solid.

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: Is edge trigger sufficient?
- Evidence evaluated: Yes.
- Invalidation risk: None.
- Revised Judgment: APPROVE

PROMPT-COMPLIANCE:
- Requirement: Edge-triggered scheduler -> Status: SATISFIED

VERDICT: APPROVE
SEVERITY: NONE

FINDINGS:
- None

REQUIRED REVISIONS:
- None`;
      };
      setCustomSubagentRunner(passRunner);

      // Call 1: Initial plan draft and research completion
      await executeUpdateStateTool(
        {
          name: slug,
          plan: ["1. Step one", "2. Step two", "3. Step three"],
          planConfidence: "high",
          planConfidenceReason: "Tested and verified assumptions",
          researchComplete: true,
          status: "Initial plan submitted",
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        reviewerInvocations,
        1,
        "Reviewer must be invoked exactly once on initial actionable plan",
      );
      assert.strictEqual(s.researchComplete, true, "Plan should be approved");
      assert.strictEqual(isPlanReviewValidForState(s), true);
      assert.strictEqual(
        getPendingReviews().size,
        0,
        "No pending reviews should exist after completion",
      );

      // Calls 2 through 10: Pure cognitive and non-plan metadata updates with unchanged plan
      const cognitiveUpdates = [
        {
          understanding: "Discovered module X boundaries",
          status: "Understanding updated",
        },
        {
          assumptions: ["Assumption A verified", "Assumption B verified"],
          status: "Assumptions updated",
        },
        {
          findings: [
            "Finding 1: edge trigger is clean",
            "Finding 2: no extra requests",
          ],
          status: "Findings logged",
        },
        {
          nextAction: "Begin step 1",
          exactNextAction: "Begin step 1",
          status: "Next action updated",
        },
        {
          status: "In progress on step 1",
          inProgress: ["Step 1 implementation"],
        },
        {
          filesExamined: [
            "src/tools/update_operation.ts",
            "src/critical_agent/policy.ts",
          ],
          status: "Examined files",
        },
        {
          completed: ["Step 1 implementation"],
          inProgress: ["Step 2 tests"],
          status: "Step 1 done",
        },
        {
          plan: ["1. Step one", "2. Step two", "3. Step three"],
          status: "Resent identical plan array",
        },
        {
          plan: "1. Step one\n2. Step two\n3. Step three",
          status: "Resent identical plan text",
          nextAction: "Finalizing",
        },
      ];

      for (let i = 0; i < cognitiveUpdates.length; i++) {
        const update = cognitiveUpdates[i];
        const res = await executeUpdateStateTool(
          {
            name: slug,
            ...update,
          },
          mockPi,
          ctx,
        );

        assert.strictEqual(
          res.details?.error,
          undefined,
          `Update ${i + 2} must succeed without error`,
        );
        assert.strictEqual(
          reviewerInvocations,
          1,
          `Update ${
            i + 2
          } (${update.status}) must NOT trigger a new reviewer invocation`,
        );
        assert.strictEqual(
          getPendingReviews().size,
          0,
          `Update ${i + 2} must NOT queue any pending review requests`,
        );
        assert.strictEqual(
          getActiveReviews().size,
          0,
          `Update ${i + 2} must NOT start any repeated active reviews`,
        );
      }

      assert.strictEqual(
        reviewerInvocations,
        1,
        "Total reviewer invocations across all 10 calls must be exactly 1",
      );
      assert.strictEqual(
        isPlanReviewValidForState(s),
        true,
        "Plan approval must remain valid across non-plan cognitive updates",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // 12. In-flight review coalescence: non-plan updates do not queue, material change queues exactly 1 pending review
  // -----------------------------------------------------------------------
  await t.step(
    "12. in-flight review coalescence: non-plan updates do not queue, material change queues exactly 1 pending review",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_12");
      plugin(mockPi);
      setAllTools([{ name: "subagent" }]);
      clearActiveReviews();

      const s = getState(ctx);
      const slug = "coalesce-in-flight-quest";
      s.active = slug;
      s.questId = slug;
      s.stack = [slug];
      s.prompts = ["Implement asynchronous audio router."];

      const qPath = `${currentDir}/${slug}/quest.md`;
      await mkdir(`${currentDir}/${slug}`, { recursive: true });
      await writeFile(
        qPath,
        `# Quest: ${slug}\n\n## Goal\nAudio router\n\n## Original request\n> Implement asynchronous audio router.\n\n## Remaining work\n- [ ] Task 1\n`,
        "utf8",
      );

      let reviewerCallCount = 0;
      let resolveV1Review!: (text: string) => void;
      const v1Promise = new Promise<string>((res) => {
        resolveV1Review = res;
      });

      const customRunner = async (task: string, options?: any) => {
        reviewerCallCount++;
        if (reviewerCallCount === 1) {
          // First review hangs until we manually resolve it
          return await v1Promise;
        }
        // Second review (V2) resolves immediately with APPROVE
        return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Audio router V2 -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      };
      setCustomSubagentRunner(customRunner);

      // 1. Trigger V1 review (runs asynchronously / in-flight)
      const v1UpdatePromise = executeUpdateStateTool(
        {
          name: slug,
          plan: ["1. Basic audio routing"],
          planConfidence: "high",
          planConfidenceReason: "Initial plan",
          researchComplete: true,
        },
        mockPi,
        ctx,
      );

      // Yield to allow V1 review promise to register and become active
      for (let i = 0; i < 50 && reviewerCallCount === 0; i++) {
        await new Promise((r) => setTimeout(r, 10));
      }

      assert.strictEqual(reviewerCallCount, 1, "V1 review must be started");
      assert.strictEqual(
        getActiveReviews().size,
        1,
        "Exactly one review must be active",
      );
      assert.strictEqual(
        getPendingReviews().size,
        0,
        "No pending reviews should exist yet",
      );

      // 2. Perform several non-plan cognitive updates while V1 review is in-flight
      await executeUpdateStateTool(
        {
          name: slug,
          understanding: "Deeper understanding of PCM buffer formats",
          status: "Researching formats",
        },
        mockPi,
        ctx,
      );

      await executeUpdateStateTool(
        {
          name: slug,
          findings: ["Sample rate conversion needed"],
          status: "Documenting findings",
        },
        mockPi,
        ctx,
      );

      await executeUpdateStateTool(
        {
          name: slug,
          nextAction: "Refine audio filter graph",
          status: "Next action planned",
        },
        mockPi,
        ctx,
      );

      // Verify non-plan updates did NOT queue any pending review
      assert.strictEqual(
        reviewerCallCount,
        1,
        "Non-plan updates must NOT start another reviewer",
      );
      assert.strictEqual(
        getPendingReviews().size,
        0,
        "Non-plan updates must NOT queue any pending review",
      );

      // 3. Materially change the plan to V2
      await executeUpdateStateTool(
        {
          name: slug,
          plan: [
            "1. Basic audio routing",
            "2. Multi-channel mixer",
            "3. Sample-rate conversion delay compensation",
          ],
          planRevisions: ["Added mixer and SRC delay compensation"],
          planVersion: 2,
          planConfidence: "high",
          planConfidenceReason: "Revised architecture",
          status: "Materially revised plan to V2",
        },
        mockPi,
        ctx,
      );

      // Verify exactly 1 pending review is now queued for V2
      assert.strictEqual(
        reviewerCallCount,
        1,
        "V2 review must not launch immediately while V1 is in-flight",
      );
      assert.strictEqual(
        getPendingReviews().size,
        1,
        "Exactly one pending review request must be queued for V2",
      );
      const pending = getPendingReviews().get(slug);
      assert.strictEqual(
        pending?.planVersion,
        2,
        "Pending review must be for plan version 2",
      );

      // 4. Perform another non-plan update while V1 is still in-flight
      await executeUpdateStateTool(
        {
          name: slug,
          status: "Waiting for V1 review to complete",
          nextAction: "Await V2 review",
        },
        mockPi,
        ctx,
      );

      assert.strictEqual(
        getPendingReviews().size,
        1,
        "Additional non-plan update must NOT duplicate or alter pending review",
      );

      // 5. Complete V1 review (resolving with APPROVE for V1)
      resolveV1Review(
        `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: Audio router V1 -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`,
      );

      await v1UpdatePromise;

      // Yield to allow finally block setTimeout(..., 0) to execute the queued V2 review
      await new Promise((r) => setTimeout(r, 50));

      // Verify V2 review was launched as a follow-up
      assert.strictEqual(
        reviewerCallCount,
        2,
        "Exactly one V2 follow-up review must be started after V1 completes",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // Change A + B + C + D: Draft → Reviewer → Auto-Promote workflow
  // -----------------------------------------------------------------------

  await t.step(
    "11. isActionableDraftPlan distinguishes placeholder vs substantive plan",
    async () => {
      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const placeholderSlug = "draft-placeholder-act";
      const realSlug = "draft-real-act";
      await writeFile(
        `${futureDir}/${placeholderSlug}.md`,
        `# Draft: ${placeholderSlug}\n\n## Plan\n1.\n`,
        "utf8",
      );
      await writeFile(
        `${futureDir}/${realSlug}.md`,
        `# Draft: ${realSlug}\n\n## Plan\n1. Memory pool setup\n2. SRC filter with delay line\n3. Mux loop\n`,
        "utf8",
      );
      assert.strictEqual(
        isActionableDraftPlan(placeholderSlug),
        false,
        "placeholder plan should NOT be actionable",
      );
      assert.strictEqual(
        isActionableDraftPlan(realSlug),
        true,
        "substantive plan should be actionable",
      );
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  await t.step(
    "11b. isActionablePlanContent recognizes scaffold header and placeholders (T-F0)",
    async () => {
      const scaffold = `# Draft: x\n\n## Implementation Plan\n1. Investigate via read/search the existing codebase structure\n2. Implement the feature\n3. Test the feature\n\nPlan confidence low.`;
      const scaffoldBullets = `# Draft: x\n\n## Implementation Plan\n- goal: \n- stages: \n- findings: \n\nPlan confidence low.`;
      const realUnderImpl = `# Draft: x\n\n## Implementation Plan\n1. Memory pool setup\n2. SRC filter with delay line\n3. Mux loop\n`;
      const realUnderPlan = `# Draft: x\n\n## Plan\n1. Memory pool setup\n2. SRC filter with delay line\n3. Mux loop\n`;
      const realUnderExec = `# Draft: x\n\n## Execution Plan\n1. Frame parser\n2. Flow control\n3. Backpressure\n`;
      assert.strictEqual(
        isActionablePlanContent(scaffold),
        false,
        "scaffold Implementation Plan must NOT be actionable",
      );
      assert.strictEqual(
        isActionablePlanContent(scaffoldBullets),
        false,
        "scaffold bullet placeholders must NOT be actionable",
      );
      assert.strictEqual(
        isActionablePlanContent(realUnderImpl),
        true,
        "substantive plan under ## Implementation Plan must be actionable",
      );
      assert.strictEqual(
        isActionablePlanContent(realUnderPlan),
        true,
        "substantive plan under ## Plan must be actionable",
      );
      assert.strictEqual(
        isActionablePlanContent(realUnderExec),
        true,
        "substantive plan under ## Execution Plan must be actionable",
      );
    },
  );

  await t.step(
    "12. promoteDraft force option bypasses review gate",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_12");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);
      setCustomSubagentRunner(null);

      const s = getState(ctx);
      const slug = "draft-force-go";
      const qid = `qid_12_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Requirements\n- req one\n\n## Plan\n1. Do the thing\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });

      // No reviewer approved (valid=false) → without force, promoteDraft fails
      const blocked = await asyncContext.run(
        ctx,
        () => promoteDraft(slug, ctx, mockPi),
      );
      assert.strictEqual(blocked.success, false, "non-force promote must fail without approval");

      // With force=true, promotes immediately
      const forced = await asyncContext.run(
        ctx,
        () => promoteDraft(slug, ctx, mockPi, { force: true }),
      );
      assert.strictEqual(forced.success, true, "force promote must succeed");
      assert.strictEqual(
        getState(ctx).activeDraft,
        null,
        "draft must be cleared after force promote",
      );
      assert.strictEqual(
        s.active === slug || getState(ctx).active === slug,
        true,
        "quest must be promoted to active",
      );

      setCustomSubagentRunner(null);
    },
  );

  await t.step(
    "13. cancelActiveReview emits cancel event carrying all three identity fields",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_13");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-cancel-plumbing";
      const qid = `qid_13_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      // Simulate a live review and populate identity map
      const reviewId = "review_abc123";
      const ownerRunId = qid;
      cancelIdentityMap.set(reviewId, {
        requestId: "req_xxx",
        nodeId: "node_yyy",
        ownerRunId,
      });

      let capturedCancel: any = null;
      (mockPi as any).events.on(
        "prompt-template:subagent:cancel",
        (data: any) => {
          capturedCancel = data;
        },
      );

      // Assert identity map records the exact 3 fields the bridge requires
      const id = cancelIdentityMap.get(reviewId);
      assert.ok(id, "cancel identity must be recorded for live review");
      assert.strictEqual(id.requestId, "req_xxx");
      assert.strictEqual(id.nodeId, "node_yyy");
      assert.strictEqual(id.ownerRunId, ownerRunId);

      // Exercise the abort listener exactly as wired in review() (pi_adapter.ts):
      // it reads the Map at emit time ({requestId, nodeId, ownerRunId}) so the
      // cancel event carries all three required fields.
      const abortPayload = cancelIdentityMap.get(reviewId);
      (mockPi as any).events.emit(
        "prompt-template:subagent:cancel",
        abortPayload,
      );

      assert.ok(capturedCancel, "cancel event must carry identity");
      assert.strictEqual(capturedCancel.requestId, "req_xxx");
      assert.strictEqual(capturedCancel.nodeId, "node_yyy");
      assert.strictEqual(capturedCancel.ownerRunId, ownerRunId);

      cancelIdentityMap.delete(reviewId);
    },
  );

  await t.step(
    "14. APPROVE on actionable draft auto-promotes",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_14");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-auto-promote";
      const qid = `qid_14_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];
      s.draftPrompts = ["Implement zero-copy audio muxer"];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Requirements\n- Implement zero-copy audio muxer\n\n## Plan\n1. Memory pool setup\n2. SRC filter with delay line\n3. Mux loop\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      const approveRunner = async () =>
        `PASS 1 (Independent Evaluation):\nProvisional Judgment: APPROVE\nProvisional Summary: Plan satisfies all requirements.\n\nPASS 2 (Self-Attack & Falsification):\n- Assumptions tested: ok\n- Invalidation risk: None\n- Revised Judgment: APPROVE\n\nPROMPT-COMPLIANCE:\n- Requirement: zero-copy muxer -> Plan Handling: Addressed in step 1 -> Status: SATISFIED\n\nVERDICT: APPROVE\nSEVERITY: NONE\n\nFINDINGS:\n- None\n\nREQUIRED REVISIONS:\n- None`;
      setCustomSubagentRunner(approveRunner);

      const result = await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );
      assert.ok(result, "review must return a result");

      // Allow async auto-promote to complete
      await new Promise((r) => setTimeout(r, 100));

      const postState = getState(ctx);
      assert.ok(
        !postState.activeDraft || postState.active === slug,
        "draft must be auto-promoted after APPROVE",
      );

      setCustomSubagentRunner(null);
    },
  );

  await t.step(
    "14b. REGISTERED draft review snapshot carries boundaryKey (T-F3-0)",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_14b");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-registered-key";
      const qid = `qid_14b_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const content = `# Draft: ${slug}\n\n## Plan\n1. Memory pool setup\n2. SRC filter\n3. Mux loop\n`;
      await writeFile(`${futureDir}/${slug}.md`, content, "utf8");
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      const { createHash } = await import("node:crypto");
      const expectedHash = createHash("sha256")
        .update(content)
        .digest("hex")
        .slice(0, 12);
      const expectedKey = `draft:${slug}:${expectedHash}`;

      let releaseReview: (() => void) | null = null;
      const gate = new Promise<void>((res) => {
        releaseReview = res;
      });
      const approved = `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Plan Handling: addressed -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      setCustomSubagentRunner(async () => {
        await gate;
        return approved;
      });

      const launchPromise = asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );

      let registered: any = null;
      for (let i = 0; i < 50; i++) {
        const active = getActiveReviews();
        for (const rev of active.values()) {
          if (rev.questSlug === slug) {
            registered = rev;
            break;
          }
        }
        if (registered) break;
        await new Promise((r) => setTimeout(r, 10));
      }

      assert.ok(
        registered,
        "plan review must be registered as active while running",
      );
      assert.ok(
        registered.snapshot,
        "registered record must carry a snapshot",
      );
      assert.strictEqual(
        registered.snapshot.boundaryKey,
        expectedKey,
        "REGISTERED (source A) snapshot must carry draft:slug:hash boundaryKey after F3-0",
      );

      releaseReview!();
      const result = await launchPromise;
      assert.ok(result, "review must return a result after release");

      setCustomSubagentRunner(null);
      await rm(futureDir, { recursive: true, force: true });
      clearActiveReviews();
    },
  );

  await t.step(
    "14c. REVISE-draft steer directs editing the future file, not quest_update_state (T-F1)",
    async () => {
      const { mockPi, setAllTools, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_14c");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-revise-steer";
      const qid = `qid_14c_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Plan\n1. Memory pool setup\n2. SRC filter\n3. Mux loop\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      const reviseRunner = async () =>
        `PASS 1:\nProvisional Judgment: REVISE\nPASS 2:\n- Revised Judgment: REVISE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Plan Handling: Missing -> Status: UNSATISFIED\nVERDICT: REVISE\nSEVERITY: CRITICAL\nFINDINGS:\n- Issue: Missing stage\nREQUIRED REVISIONS:\n- Add backpressure stage`;
      setCustomSubagentRunner(reviseRunner);

      await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );
      setCustomSubagentRunner(null);
      await new Promise((r) => setTimeout(r, 100));

      const joined = agentMessages.map((m: any) => m.msg?.toString() || "")
        .join("\n");
      assert.ok(
        joined.includes(`.pi/quest/future/${slug}.md`),
        "REVISE-draft steer must name the future draft file",
      );
      assert.ok(
        /revise\s+`?\.pi\/quest\/future\/[^`]*\.md/i.test(joined),
        "REVISE-draft steer must direct editing the draft file",
      );
      assert.ok(
        joined.includes("do NOT use `quest_update_state` for a draft plan") ||
          joined.toLowerCase().includes("do not use `quest_update_state`"),
        "REVISE-draft steer must warn against quest_update_state for drafts",
      );
      assert.ok(
        joined.includes("re-review triggers automatically") ||
          joined.toLowerCase().includes("triggers re-review"),
        "REVISE-draft steer must note automatic re-review",
      );

      await rm(futureDir, { recursive: true, force: true });
      clearActiveReviews();
    },
  );

  await t.step(
    "15. draft update fires a fresh plan review (no coalesce deadlock)",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_15");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-refine-trigger";
      const qid = `qid_15_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Requirements\n- req one\n\n## Plan\n1. Step one\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });

      let reviewCalls = 0;
      const runner = async () => {
        reviewCalls++;
        return `PASS 1 (Independent Evaluation):\nProvisional Judgment: APPROVE\nProvisional Summary: ok\n\nPASS 2 (Self-Attack & Falsification):\n- Invalidation risk: None\n- Revised Judgment: APPROVE\n\nPROMPT-COMPLIANCE:\n- Requirement: x -> Status: SATISFIED\n\nVERDICT: APPROVE\nSEVERITY: NONE\n\nFINDINGS:\n- None\n\nREQUIRED REVISIONS:\n- None`;
      };
      setCustomSubagentRunner(runner);

      // Fire initial review
      await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );
      await new Promise((r) => setTimeout(r, 50));
      const callsAfterFirst = reviewCalls;
      assert.ok(callsAfterFirst >= 1, "first review must fire");

      // Change draft hash → checkAndTriggerPlanReview fires again (new dedup key)
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Requirements\n- req one\n- req two\n\n## Plan\n1. Step one\n2. Step two\n`,
        "utf8",
      );
      await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );
      await new Promise((r) => setTimeout(r, 50));
      assert.ok(
        reviewCalls > callsAfterFirst,
        "draft revision must fire a fresh review (new hash)",
      );

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // T-F2: Launch-time yield steer for draft plan_review
  // -----------------------------------------------------------------------
  await t.step(
    "16. launch-time yield steer emitted for draft plan_review (T-F2)",
    async () => {
      const { mockPi, agentMessages, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_16");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "yield-steer-draft";
      const qid = `qid_16_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const content = `# Draft: ${slug}\n\n## Plan\n1. Step one\n2. Step two\n`;
      await writeFile(`${futureDir}/${slug}.md`, content, "utf8");
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      let releaseReview: (() => void) | null = null;
      const gate = new Promise<void>((res) => { releaseReview = res; });
      const approved = `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      setCustomSubagentRunner(async () => {
        await gate;
        return approved;
      });

      agentMessages.length = 0;
      const launchPromise = asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "yield-steer-draft"),
      );

      // Wait for review to register
      let registered = false;
      for (let i = 0; i < 50; i++) {
        for (const rev of getActiveReviews().values()) {
          if (rev.questSlug === slug) { registered = true; break; }
        }
        if (registered) break;
        await new Promise((r) => setTimeout(r, 10));
      }
      assert.ok(registered, "review must register");

      // Wait for yield steer to be emitted (import inside lock yields)
      await new Promise((r) => setTimeout(r, 100));

      // The yield steer should have been emitted at launch
      const steerTexts = agentMessages.map((m: any) => String(m.msg || ""));
      const hasYieldSteer = steerTexts.some(
        (t: string) => /plan review.*launched.*finish your current tool call/i.test(t),
      );
      assert.ok(hasYieldSteer, "launch-time yield steer must be emitted");

      releaseReview!();
      await launchPromise;
      setCustomSubagentRunner(null);
      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  // -----------------------------------------------------------------------
  // T-F3i: Auto re-review on draft-file edit
  // -----------------------------------------------------------------------
  await t.step(
    "17. draft-file edit triggers DRAFT_PLAN_EDITED and auto re-review (T-F3i)",
    async () => {
      const { mockPi, agentMessages, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_17");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-edit-trigger";
      const qid = `qid_17_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(draftPath, `# Draft: ${slug}\n\n## Plan\n1. Old step\n`, "utf8");
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      let reviewCalls = 0;
      setCustomSubagentRunner(async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      });

      // Simulate a tool_result event for editing the draft file
      agentMessages.length = 0;
      await asyncContext.run(
        ctx,
        () => handleToolResult(
          {
            toolName: "edit",
            input: { path: draftPath, oldString: "Old step", newString: "New step" },
            content: "edited",
            isError: false,
          },
          ctx,
          mockPi as any,
        ),
      );

      // Wait for async checkAndTriggerPlanReview to fire and review to start
      for (let i = 0; i < 50; i++) {
        if (reviewCalls >= 1) break;
        await new Promise((r) => setTimeout(r, 50));
      }

      // The review should have been triggered
      assert.ok(reviewCalls >= 1, "auto re-review must fire after draft edit");

      setCustomSubagentRunner(null);
      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  await t.step(
    "17b. non-draft file edit does NOT trigger re-review (T-F3i negative)",
    async () => {
      const { mockPi, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_17b");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      s.activeDraft = "some-draft";
      s.active = "some-quest";
      s.questId = "qid_17b";

      let reviewCalls = 0;
      setCustomSubagentRunner(async () => { reviewCalls++; return "VERDICT: APPROVE\nSEVERITY: NONE"; });

      const before = reviewCalls;
      await asyncContext.run(
        ctx,
        () => handleToolResult(
          {
            toolName: "edit",
            input: { path: "src/unrelated.ts", oldString: "a", newString: "b" },
            content: "edited",
            isError: false,
          },
          ctx,
          mockPi as any,
        ),
      );
      await new Promise((r) => setTimeout(r, 50));
      assert.strictEqual(reviewCalls, before, "non-draft edit must not trigger re-review");

      setCustomSubagentRunner(null);
    },
  );

  // -----------------------------------------------------------------------
  // T-F3ii: Hard throttle while REVISE outstanding
  // -----------------------------------------------------------------------
  await t.step(
    "18. isDraftRevisionOutstanding returns true after REVISE + unchanged file (T-F3ii)",
    async () => {
      const s = getState(createMockContext(50000, "session_plan_18"));
      const slug = "throttle-test";
      const qid = `qid_18_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const content = `# Draft: ${slug}\n\n## Plan\n1. Step\n`;
      await writeFile(`${futureDir}/${slug}.md`, content, "utf8");
      await mkdir(`${currentDir}/${qid}`, { recursive: true });

      const { createHash } = await import("node:crypto");
      const hash = createHash("sha256").update(content).digest("hex").slice(0, 12);

      // Simulate a REVISE verdict on the draft
      s.lastCriticalReview = {
        id: "rev_test",
        questId: qid,
        kind: "plan_review",
        reviewedStateVersion: { planVersion: 1, saveHash: null, saveCount: 0 },
        verdict: "REVISE",
        severity: "MAJOR",
        findings: [],
        requiredActions: [],
        resolved: false,
        timestamp: Date.now(),
        snapshot: {
          questId: qid,
          sessionId: "s",
          reviewId: "rev_test",
          reviewKind: "plan_review",
          planVersion: 1,
          boundaryKey: `draft:${slug}:${hash}`,
          saveGeneration: 0,
          stateHash: null,
          originalUserRequest: "test",
          currentUnderstanding: "",
          assumptions: "",
          plan: "",
          planRevisions: "",
          findings: "",
          filesChanged: "",
          relevantDiff: "",
          testStatus: "",
          nextAction: "",
          createdAt: Date.now(),
        },
      };
      s.awaitingReview = null;

      assert.ok(
        isDraftRevisionOutstanding(s),
        "must be outstanding when REVISE + file unchanged",
      );

      // After file edit → hash drifts → no longer outstanding
      await writeFile(
        `${futureDir}/${slug}.md`,
        `# Draft: ${slug}\n\n## Plan\n1. Revised step\n`,
        "utf8",
      );
      assert.ok(
        !isDraftRevisionOutstanding(s),
        "must NOT be outstanding after file edit (hash drift)",
      );

      // No verdict → not outstanding
      s.lastCriticalReview = null;
      assert.ok(
        !isDraftRevisionOutstanding(s),
        "must NOT be outstanding with no lastCriticalReview",
      );

      // awaitingReview set → not outstanding (review in flight)
      s.lastCriticalReview = {
        id: "rev_test2",
        questId: qid,
        kind: "plan_review",
        reviewedStateVersion: { planVersion: 1, saveHash: null, saveCount: 0 },
        verdict: "REVISE",
        severity: "MAJOR",
        findings: [],
        requiredActions: [],
        resolved: false,
        timestamp: Date.now(),
        snapshot: {
          questId: qid, sessionId: "s", reviewId: "rev_test2",
          reviewKind: "plan_review", planVersion: 1,
          boundaryKey: `draft:${slug}:${hash}`, saveGeneration: 0,
          stateHash: null, originalUserRequest: "test",
          currentUnderstanding: "", assumptions: "", plan: "",
          planRevisions: "", findings: "", filesChanged: "",
          relevantDiff: "", testStatus: "", nextAction: "",
          createdAt: Date.now(),
        },
      };
      s.awaitingReview = { kind: "plan_review", reviewId: "rev_test2", since: Date.now() };
      assert.ok(
        !isDraftRevisionOutstanding(s),
        "must NOT be outstanding while awaitingReview is set",
      );
      s.awaitingReview = null;

      // APPROVE verdict → not outstanding
      s.lastCriticalReview!.verdict = "APPROVE";
      assert.ok(
        !isDraftRevisionOutstanding(s),
        "must NOT be outstanding with APPROVE verdict",
      );

      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  await t.step(
    "18b. getImplementationBlockReason returns DRAFT_REVISION_PENDING when throttle active (T-F3ii)",
    async () => {
      const ctx = createMockContext(50000, "session_plan_18b");
      const s = getState(ctx);
      const slug = "gate-throttle";
      const qid = `qid_18b_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const content = `# Draft: ${slug}\n\n## Plan\n1. Step\n`;
      await writeFile(`${futureDir}/${slug}.md`, content, "utf8");
      await mkdir(`${currentDir}/${qid}`, { recursive: true });

      const { createHash } = await import("node:crypto");
      const hash = createHash("sha256").update(content).digest("hex").slice(0, 12);

      s.lastCriticalReview = {
        id: "rev_gate", questId: qid, kind: "plan_review",
        reviewedStateVersion: { planVersion: 1, saveHash: null, saveCount: 0 },
        verdict: "REVISE", severity: "MAJOR", findings: [],
        requiredActions: [], resolved: false, timestamp: Date.now(),
        snapshot: {
          questId: qid, sessionId: "s", reviewId: "rev_gate",
          reviewKind: "plan_review", planVersion: 1,
          boundaryKey: `draft:${slug}:${hash}`, saveGeneration: 0,
          stateHash: null, originalUserRequest: "test",
          currentUnderstanding: "", assumptions: "", plan: "",
          planRevisions: "", findings: "", filesChanged: "",
          relevantDiff: "", testStatus: "", nextAction: "",
          createdAt: Date.now(),
        },
      };
      s.awaitingReview = null;

      const gate = getImplementationBlockReason(s, ctx);
      assert.strictEqual(
        gate.stateName,
        "DRAFT_REVISION_PENDING",
        "gate must return DRAFT_REVISION_PENDING stateName",
      );
      assert.ok(gate.blocked, "gate must be blocked");

      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  // -----------------------------------------------------------------------
  // T-F4: Coalesce-drop steer (60 s dedup)
  // -----------------------------------------------------------------------
  await t.step(
    "19. coalesce-drop steer emitted once per 60s window (T-F4)",
    async () => {
      const { mockPi, agentMessages, setAllTools } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_19");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "coalesce-steer";
      const qid = `qid_19_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const content = `# Draft: ${slug}\n\n## Plan\n1. Step\n`;
      await writeFile(`${futureDir}/${slug}.md`, content, "utf8");
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      __resetCoalesceSteerForTests();

      let releaseReview: (() => void) | null = null;
      const gate = new Promise<void>((res) => { releaseReview = res; });
      const approved = `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      setCustomSubagentRunner(async () => {
        await gate;
        return approved;
      });

      agentMessages.length = 0;
      // Launch first review
      const launchPromise1 = asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "coalesce-steer"),
      );
      // Wait for registration
      for (let i = 0; i < 50; i++) {
        let found = false;
        for (const rev of getActiveReviews().values()) {
          if (rev.questSlug === slug) { found = true; break; }
        }
        if (found) break;
        await new Promise((r) => setTimeout(r, 10));
      }

      const msgsBefore = agentMessages.length;
      // Fire second review with SAME boundaryKey → should coalesce + emit steer
      const launchPromise2 = asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "coalesce-steer"),
      );
      await new Promise((r) => setTimeout(r, 50));

      const newMsgs = agentMessages.slice(msgsBefore);
      const hasCoalesceSteer = newMsgs.some((m: any) =>
        /already running.*coalesced/i.test(String(m.msg || "")),
      );
      assert.ok(hasCoalesceSteer, "first coalesce must emit steer");

      // Third call within 60s → no second steer (dedup)
      const msgsBefore3 = agentMessages.length;
      await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "coalesce-steer"),
      );
      await new Promise((r) => setTimeout(r, 50));
      const newMsgs3 = agentMessages.slice(msgsBefore3);
      const hasSecondCoalesceSteer = newMsgs3.some((m: any) =>
        /already running.*coalesced/i.test(String(m.msg || "")),
      );
      assert.ok(!hasSecondCoalesceSteer, "second coalesce within 60s must NOT emit steer (dedup)");

      releaseReview!();
      await launchPromise1;
      await launchPromise2;
      setCustomSubagentRunner(null);
      __resetCoalesceSteerForTests();
      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  // -------------------------------------------------------------------------
  // 20. handleTurnStart emits DRAFT_REVISION_PENDING and draft_revision phase
  // -------------------------------------------------------------------------
  await t.step(
    "20. handleTurnStart sets activeGate DRAFT_REVISION_PENDING and phase draft_revision when revision outstanding (T-F7)",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(2000, "session_test_turn_start_gate");
      const s = getState(ctx);

      const slug = "draft-gate-turn-start";
      const qid = "draft-gate-qid";
      const futureDir = ".pi/quest/future";
      const futurePath = `${futureDir}/${slug}.md`;
      await mkdir(futureDir, { recursive: true });
      const content = `# Proposal: ${slug}\n\n## Implementation Plan\n\n- Stage 1: Initial draft\n`;
      await writeFile(
        futurePath,
        content,
        "utf8",
      );

      const { createHash } = await import("node:crypto");
      const hash = createHash("sha256").update(content).digest("hex").slice(0, 12);

      s.active = slug;
      s.activeDraft = slug;
      s.questId = qid;
      s.researchRequired = false;
      s.researchComplete = true;

      // Simulate an UNCERTAIN review
      s.lastCriticalReview = {
        kind: "plan_review",
        verdict: "UNCERTAIN",
        snapshot: {
          questId: slug,
          planVersion: 1,
          researchComplete: true,
          reassessmentRequired: false,
          awaitingReview: false,
          reviewKind: "plan_review",
          sessionId: "session_test_turn_start_gate",
          boundaryKey: `draft:${slug}:${hash}`,
          sourceHash: hash,
          draftFileHash: hash,
        },
      } as any;

      // Ensure isDraftRevisionOutstanding returns true
      assert.strictEqual(isDraftRevisionOutstanding(s), true);

      // Run handleTurnStart and verify log / state transitions
      await asyncContext.run(ctx, () => handleTurnStart({ turnIndex: 1 }, ctx));

      // After handleTurnStart, syncImplementationPermission must have run
      assert.strictEqual(s.implementationAllowed, false);

      await rm(futureDir, { recursive: true, force: true });
    },
  );

  // -----------------------------------------------------------------------
  // T-B1: Turn start AWAITING_REVIEW gate & turn end no-steer-loop
  // -----------------------------------------------------------------------
  await t.step(
    "21. handleTurnStart sets activeGate AWAITING_REVIEW and handleTurnEnd does NOT emit steer (T-B1)",
    async () => {
      const { mockPi, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_21");
      const s = getState(ctx);
      const slug = "awaiting-review-turn";
      s.active = slug;
      s.questId = "qid_21";
      s.awaitingReview = {
        kind: "plan_review",
        reviewId: "rev_21_test",
        since: Date.now(),
      };

      await asyncContext.run(ctx, () => handleTurnStart({ turnIndex: 1 }, ctx));
      assert.ok(
        s._lastSemanticKey?.endsWith(":AWAITING_REVIEW"),
        `semantic key must reflect AWAITING_REVIEW, got ${s._lastSemanticKey}`,
      );
      assert.ok(
        s._lastSemanticKey?.startsWith("awaiting_review:"),
        `semantic key must reflect awaiting_review phase, got ${s._lastSemanticKey}`,
      );

      // Verify handleTurnEnd does not emit steer messages while awaiting review
      agentMessages.length = 0;
      await asyncContext.run(ctx, () => handleTurnEnd(mockPi as any, ctx, { turnIndex: 1 }));
      const hasAwaitingSteer = agentMessages.some((m: any) =>
        String(m.msg || "").includes("verdict pending")
      );
      assert.strictEqual(
        hasAwaitingSteer,
        false,
        "handleTurnEnd must not emit steer messages while awaiting review",
      );
    },
  );

  // -----------------------------------------------------------------------
  // T-B2: ask_user_question classification and unconditional allow
  // -----------------------------------------------------------------------
  await t.step(
    "22. classifyToolCall treats ask_user_question as interaction and gate allows it (T-B2)",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_22");
      plugin(mockPi);

      const s = getState(ctx);
      s.active = "interaction-test";
      s.questId = "qid_22";
      s.awaitingReview = {
        kind: "plan_review",
        reviewId: "rev_22_test",
        since: Date.now(),
      };

      // Classification check
      assert.strictEqual(
        classifyToolCall("ask_user_question"),
        "interaction",
        "ask_user_question must be classified as interaction",
      );
      assert.strictEqual(
        classifyToolCall("ask_questions"),
        "interaction",
        "ask_questions must be classified as interaction",
      );

      // Gate check
      let blocked = false;
      for (const cb of handlers["tool_call"] || []) {
        const res = await asyncContext.run(ctx, () =>
          cb({ toolName: "ask_user_question", input: { questions: [] } }, ctx));
        if (res?.block) blocked = true;
      }
      assert.strictEqual(blocked, false, "ask_user_question must not be blocked during AWAITING_REVIEW");
    },
  );

  // -----------------------------------------------------------------------
  // T-B3: quest_mark_saved verifies draft at futureDraftPath
  // -----------------------------------------------------------------------
  await t.step(
    "23. quest_mark_saved verifies future draft file when state.activeDraft is set (T-B3)",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_23");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "draft-mark-saved-test";
      s.activeDraft = slug;
      s.active = "";
      s.questId = "1788530059"; // auto-created qid without current/ file

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(draftPath, `# Draft: ${slug}\n\n## Implementation Plan\n1. Step\n`, "utf8");

      // Verify tool call gate does not block quest_mark_saved for the draft
      let blocked = false;
      for (const cb of handlers["tool_call"] || []) {
        const res = await asyncContext.run(ctx, () =>
          cb({ toolName: "quest_mark_saved" }, ctx));
        if (res?.block) blocked = true;
      }
      assert.strictEqual(blocked, false, "quest_mark_saved must not be blocked when draft file exists on disk");

      // Verify persistence.verifyAndMarkSaved saves against the draft file
      const { verifyAndMarkSaved } = await import("../src/persistence.ts");
      const res = await asyncContext.run(ctx, () =>
        verifyAndMarkSaved(mockPi as any, ctx, slug));
      assert.strictEqual(res.success, true, `save must succeed for future draft: ${res.error}`);
      assert.ok(s.saveGeneration?.path.endsWith(`future/${slug}.md`), "saveGeneration path must be future draft");

      await rm(futureDir, { recursive: true, force: true });
    },
  );

  // -----------------------------------------------------------------------
  // T-B4: Reads and research are never blocked during draft revision
  // -----------------------------------------------------------------------
  await t.step(
    "24. reads to non-quest files are allowed during draft revision (T-B4)",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_24");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "draft-read-test";
      s.activeDraft = slug;
      s.active = slug;
      s.questId = "qid_24";

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const content = `# Draft: ${slug}\n\n## Implementation Plan\n1. Step\n`;
      await writeFile(`${futureDir}/${slug}.md`, content, "utf8");

      const { createHash } = await import("node:crypto");
      const hash = createHash("sha256").update(content).digest("hex").slice(0, 12);

      // Simulate a REVISE verdict so isDraftRevisionOutstanding is true
      s.lastCriticalReview = {
        kind: "plan_review",
        verdict: "REVISE",
        snapshot: {
          questId: slug,
          planVersion: 1,
          boundaryKey: `draft:${slug}:${hash}`,
        },
      } as any;

      assert.strictEqual(isDraftRevisionOutstanding(s), true);

      // Test reading an external file (e.g. external/bud/src/libbud.c or docs/OVERVIEW.md)
      let blocked = false;
      for (const cb of handlers["tool_call"] || []) {
        const res = await asyncContext.run(ctx, () =>
          cb({ toolName: "read", input: { path: "external/bud/src/libbud.c" } }, ctx));
        if (res?.block) blocked = true;
      }
      assert.strictEqual(blocked, false, "read to external file must NOT be blocked during draft revision");

      await rm(futureDir, { recursive: true, force: true });
    },
  );

  await t.step(
    "25. normalizeReviewModel maps extension providers to built-in openrouter and preserves built-in providers",
    () => {
      // kilo and cline mapping to openrouter
      assert.strictEqual(
        normalizeReviewModel("kilo", "openrouter/free"),
        "openrouter/openrouter/free",
      );
      assert.strictEqual(
        normalizeReviewModel("cline", "openrouter/free"),
        "openrouter/openrouter/free",
      );
      assert.strictEqual(
        normalizeReviewModel("kilo", "deepseek/deepseek-r1"),
        "deepseek/deepseek-r1",
      );

      // Built-in providers preserved
      assert.strictEqual(
        normalizeReviewModel("openrouter", "openrouter/free"),
        "openrouter/openrouter/free",
      );
      assert.strictEqual(
        normalizeReviewModel("openai", "gpt-4o"),
        "openai/gpt-4o",
      );
      assert.strictEqual(
        normalizeReviewModel("anthropic", "claude-3-7-sonnet"),
        "anthropic/claude-3-7-sonnet",
      );

      // Error classifier
      assert.strictEqual(
        isModelResolutionOrProviderError(
          'Model "kilo/openrouter/free:high" not found. Use --list-models to see available models.',
        ),
        true,
      );
      assert.strictEqual(
        isModelResolutionOrProviderError('Unknown provider "kilo"'),
        true,
      );
      assert.strictEqual(
        isModelResolutionOrProviderError("rate limit 429 exceeded"),
        true,
      );
      assert.strictEqual(
        isModelResolutionOrProviderError("mutation-capable tool was called"),
        false,
      );

      // resolveDefaultReviewModel with ctx
      const ctxKilo = {
        model: { provider: "kilo", id: "openrouter/free" },
      } as any;
      assert.strictEqual(
        resolveDefaultReviewModel(ctxKilo),
        "openrouter/openrouter/free",
      );

      const ctxOpenRouter = {
        model: { provider: "openrouter", id: "openrouter/free" },
      } as any;
      assert.strictEqual(
        resolveDefaultReviewModel(ctxOpenRouter),
        "openrouter/openrouter/free",
      );
    },
  );

  await t.step(
    "26. PiSubagentReviewer.review() recovers from model not found via multi-tier fallback",
    async () => {
      const attemptedModels: string[] = [];

      const mockRunner: any = async (_task: string, options: any) => {
        attemptedModels.push(options?.model || "<undefined>");
        if (options?.model === "kilo/openrouter/free") {
          const err: any = new Error(
            'Model "kilo/openrouter/free:high" not found. Use --list-models to see available models.',
          );
          err.timeoutLayer = "provider_model_timeout";
          throw err;
        }
        // Fallback model succeeds
        return {
          text:
            "VERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None",
        };
      };

      const reviewer = new PiSubagentReviewer(undefined, undefined, mockRunner);
      const res = await reviewer.review({
        kind: "plan_review",
        questSlug: "model-fallback-test",
        context: {
          originalRequest: "test request",
          refinements: [],
          planConfidence: "medium",
        },
        model: "kilo/openrouter/free",
      } as any);

      assert.strictEqual(res.verdict, "APPROVE");
      assert.ok(
        attemptedModels.length >= 2,
        `expected multiple model attempts, got: ${JSON.stringify(attemptedModels)}`,
      );
      assert.strictEqual(attemptedModels[0], "kilo/openrouter/free");
      assert.strictEqual(attemptedModels[1], "openrouter/openrouter/free");
    },
  );

  await t.step(
    "27. PiSubagentReviewer.review() falls back to unconstrained host model when explicit candidate fails",
    async () => {
      const attemptedModels: Array<string | undefined> = [];

      const mockRunner: any = async (_task: string, options: any) => {
        attemptedModels.push(options?.model);
        if (options?.model) {
          const err: any = new Error('Unknown provider "custom_prov"');
          err.timeoutLayer = "provider_model_timeout";
          throw err;
        }
        // Unconstrained fallback (model: undefined) succeeds
        return {
          text:
            "VERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None",
        };
      };

      const reviewer = new PiSubagentReviewer(undefined, undefined, mockRunner);
      const res = await reviewer.review({
        kind: "plan_review",
        questSlug: "unconstrained-fallback-test",
        context: {
          originalRequest: "test request",
          refinements: [],
          planConfidence: "medium",
        },
        model: "custom_prov/custom_model",
      } as any);

      assert.strictEqual(res.verdict, "APPROVE");
      assert.ok(
        attemptedModels.includes(undefined),
        `expected attempt with model: undefined (unconstrained), got: ${JSON.stringify(attemptedModels)}`,
      );
    },
  );

  // -----------------------------------------------------------------------
  // T-DRAFT-PROMPT-ISOLATION: User prompt during drafting does NOT trigger plan_review; draft-file edit DOES
  // -----------------------------------------------------------------------
  await t.step(
    "28. user prompt during drafting does NOT trigger plan_review, but draft-file edit DOES",
    async () => {
      const { mockPi, setAllTools, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_28");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "prompt-vs-edit-test";
      const qid = `qid_28_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];
      s.draftPrompts = ["initial prompt"];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(
        draftPath,
        `# Draft: ${slug}\n\n## Requirements\n- req 1\n\n## Implementation Plan\n1. Initial plan step\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      let reviewCalls = 0;
      setCustomSubagentRunner(async () => {
        reviewCalls++;
        return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      });

      // 1. Send a user prompt (refinement / discussion) during drafting via before_agent_start
      const beforeHandlers = handlers["before_agent_start"] || [];
      for (const handler of beforeHandlers) {
        await asyncContext.run(ctx, () =>
          handler({ prompt: "Also make sure to handle edge cases" }, ctx)
        );
      }

      // Wait a moment for any async calls to potentially fire
      await new Promise((r) => setTimeout(r, 100));

      // User prompt must NOT have triggered plan_review
      assert.strictEqual(
        reviewCalls,
        0,
        "user prompt during drafting must NOT trigger plan reviewer",
      );

      // 2. Now simulate editing the draft file via handleToolResult
      await asyncContext.run(ctx, () =>
        handleToolResult(
          {
            toolName: "edit",
            input: { path: draftPath, oldString: "Initial plan step", newString: "Updated plan step" },
            content: "edited",
            isError: false,
          },
          ctx,
          mockPi as any,
        )
      );

      // Wait for review to trigger from draft-file edit
      for (let i = 0; i < 50; i++) {
        if (reviewCalls >= 1) break;
        await new Promise((r) => setTimeout(r, 50));
      }

      assert.ok(
        reviewCalls >= 1,
        "draft-file edit MUST trigger plan reviewer",
      );

      setCustomSubagentRunner(null);
      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(futureDir, { recursive: true, force: true });
    },
  );

  // -----------------------------------------------------------------------
  // T-REVIEWER-TURN-RESUMPTION: Reviewer completion wakes idle main agent
  // -----------------------------------------------------------------------
  await t.step(
    "29. reviewer completion dispatches message with triggerTurn: true and deliverAs: followUp to wake idle agent",
    async () => {
      const { mockPi, setAllTools, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_29");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "turn-resumption-test";
      const qid = `qid_29_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(
        draftPath,
        `# Draft: ${slug}\n\n## Requirements\n- req 1\n\n## Implementation Plan\n1. Step one\n2. Step two\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });
      const logPath = getQuestLogPath(qid, currentDir);
      await rm(logPath, { force: true });

      // Case A: Reviewer APPROVE triggers turn resumption with deliverAs: followUp and triggerTurn: true
      agentMessages.length = 0;
      setCustomSubagentRunner(async () => {
        return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      });

      await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );

      // Wait for review completion and message delivery
      for (let i = 0; i < 50; i++) {
        if (agentMessages.length >= 1) break;
        await new Promise((r) => setTimeout(r, 20));
      }

      assert.ok(
        agentMessages.length >= 1,
        "completion message must be delivered to agentMessages",
      );
      const approveMsg = agentMessages.find((m) =>
        String(m.msg).includes("auto-promoted") || String(m.msg).includes("APPROVED")
      );
      assert.ok(approveMsg, "must deliver approval message");
      assert.strictEqual(
        approveMsg.options?.deliverAs,
        "followUp",
        "approval completion must use deliverAs: 'followUp'",
      );
      assert.strictEqual(
        approveMsg.options?.triggerTurn,
        true,
        "approval completion must set triggerTurn: true to wake idle agent",
      );
      assert.strictEqual(
        approveMsg.display,
        true,
        "approval completion must set display: true for visibility in chat",
      );

      // Case B: Reviewer REVISE triggers turn resumption with deliverAs: followUp and triggerTurn: true
      const reviseSlug = "turn-resumption-revise";
      const reviseQid = `qid_29_${reviseSlug}`;
      s.questId = reviseQid;
      s.activeDraft = reviseSlug;
      s.lastDraftReviewRequestKey = null;
      s.lastPlanReviewApproval = null;
      s.draftLastReviewKey = null;
      const reviseDraftPath = `${futureDir}/${reviseSlug}.md`;
      await writeFile(
        reviseDraftPath,
        `# Draft: ${reviseSlug}\n\n## Requirements\n- req 1\n\n## Implementation Plan\n1. Incomplete step\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${reviseQid}`, { recursive: true });

      agentMessages.length = 0;
      setCustomSubagentRunner(async () => {
        return `PASS 1:\nProvisional Judgment: REVISE\nPASS 2:\n- Revised Judgment: REVISE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: UNSATISFIED\nVERDICT: REVISE\nSEVERITY: CRITICAL\nFINDINGS:\n- Missing validation\nREQUIRED REVISIONS:\n- Add validation`;
      });

      await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );

      for (let i = 0; i < 50; i++) {
        if (agentMessages.some((m) => String(m.msg).includes("REVISE"))) break;
        await new Promise((r) => setTimeout(r, 20));
      }

      const reviseMsg = agentMessages.find((m) =>
        String(m.msg).includes("REVISE")
      );
      assert.ok(reviseMsg, "must deliver revise message");
      assert.strictEqual(
        reviseMsg.options?.deliverAs,
        "followUp",
        "revise completion must use deliverAs: 'followUp'",
      );
      assert.strictEqual(
        reviseMsg.options?.triggerTurn,
        true,
        "revise completion must set triggerTurn: true to wake idle agent",
      );
      assert.strictEqual(
        reviseMsg.display,
        true,
        "revise completion must set display: true for visibility in chat",
      );

      // Case C: sendInternalAgentMessage default behavior across deliverAs modes
      const testTransport: any = {
        messages: [] as any[],
        sendMessage(msg: any, opts: any) {
          this.messages.push({ msg, opts });
        },
      };

      const { sendInternalAgentMessage } = await import("../src/messaging.ts");
      sendInternalAgentMessage(testTransport, "followUp msg", "followUp");
      sendInternalAgentMessage(testTransport, "steer msg", "steer");
      sendInternalAgentMessage(testTransport, "nextTurn msg", "nextTurn");
      sendInternalAgentMessage(testTransport, "explicit true", "followUp", undefined, undefined, { triggerTurn: true });
      sendInternalAgentMessage(testTransport, "explicit false", "followUp", undefined, undefined, { triggerTurn: false });

      assert.strictEqual(testTransport.messages[0].opts.triggerTurn, false, "followUp defaults safely to triggerTurn: false");
      assert.strictEqual(testTransport.messages[1].opts.triggerTurn, false, "steer defaults safely to triggerTurn: false");
      assert.strictEqual(testTransport.messages[2].opts.triggerTurn, false, "nextTurn defaults safely to triggerTurn: false");
      assert.strictEqual(testTransport.messages[3].opts.triggerTurn, true, "explicit triggerTurn: true is respected");
      assert.strictEqual(testTransport.messages[4].opts.triggerTurn, false, "explicit triggerTurn: false is respected");

      setCustomSubagentRunner(null);
      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(`${currentDir}/${reviseQid}`, { recursive: true, force: true });
      await rm(draftPath, { force: true });
      await rm(reviseDraftPath, { force: true });
      await mkdir(futureDir, { recursive: true });
    },
  );

  // -----------------------------------------------------------------------
  await t.step(
    "30. draft plan review is not superseded by session saveCount/stateHash advance, and genuine supersede notifies agent",
    async () => {
      const { mockPi, setAllTools, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_30");
      plugin(mockPi);
      setAllTools([{ name: "subagent", description: "Subagent runner" }]);

      const s = getState(ctx);
      const slug = "draft-not-superseded-test";
      const qid = `qid_30_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];
      s.lastPlanReviewApproval = null;
      s.draftLastReviewKey = null;
      s.lastDraftReviewRequestKey = null;
      s.saveCount = 1;
      s.lastSavedHash = "hash_initial";

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(
        draftPath,
        `# Draft: ${slug}\n\n## Requirements\n- req 1\n\n## Implementation Plan\n1. Step one\n2. Step two\n`,
        "utf8",
      );
      await mkdir(`${currentDir}/${qid}`, { recursive: true });

      // Simulate: while the subagent runs, main agent increments saveCount and updates lastSavedHash
      let ranSubagent = false;
      setCustomSubagentRunner(async () => {
        ranSubagent = true;
        // Main agent called quest_mark_saved in background
        s.saveCount = 5;
        s.lastSavedHash = "hash_advanced_by_quest_mark_saved";
        return `PASS 1:\nProvisional Judgment: APPROVE\nPASS 2:\n- Revised Judgment: APPROVE\nPROMPT-COMPLIANCE:\n- Requirement: plan -> Status: SATISFIED\nVERDICT: APPROVE\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED REVISIONS:\n- None`;
      });

      agentMessages.length = 0;
      const result = await asyncContext.run(
        ctx,
        () => checkAndTriggerPlanReview(mockPi, ctx, "draft"),
      );

      assert.ok(ranSubagent, "subagent must run");
      assert.strictEqual(result?.superseded, undefined, "review must not be superseded by session save count advance");
      assert.strictEqual(result?.review?.verdict, "APPROVE", "verdict must be APPROVE");

      // Auto-promote should have succeeded
      assert.ok(
        agentMessages.some((m) => String(m.msg).includes("auto-promoted")),
        "draft must be auto-promoted after APPROVE despite saveCount advance",
      );

      // Part B: Genuine draft file change DOES supersede and DISPATCHES notification
      const genSlug = "genuine-supersede-test";
      const genQid = `qid_30_${genSlug}`;
      s.questId = genQid;
      s.activeDraft = genSlug;
      s.lastPlanReviewApproval = null;
      s.draftLastReviewKey = null;
      s.lastDraftReviewRequestKey = null;

      const genDraftPath = `${futureDir}/${genSlug}.md`;
      await writeFile(
        genDraftPath,
        `# Draft: ${genSlug}\n\n## Implementation Plan\n1. Step A\n`,
        "utf8",
      );

      const snapshot: any = {
        questId: genSlug,
        sessionId: "sess_30",
        reviewId: "rev_test_30_gen",
        reviewKind: "plan_review",
        planVersion: 1,
        boundaryKey: `draft:${genSlug}:oldhash12345`,
        saveGeneration: 1,
        stateHash: "somehash",
      };

      const { isReviewSnapshotCurrent } = await import("../src/critical_agent/snapshot.ts");
      const currentness = isReviewSnapshotCurrent(snapshot, s);
      assert.strictEqual(currentness.current, false, "draft review must be superseded when draft boundary drifted");

      // Reconcile path must mark review as superseded and NOT deliver stale findings to agent (Invariant 4)
      agentMessages.length = 0;
      const { reconcileReviewResult } = await import("../src/critical_agent/policy/reconcile.ts");
      s.awaitingReview = {
        kind: "plan_review",
        reviewId: "rev_test_30_gen",
        since: Date.now(),
      };

      const recRes = await reconcileReviewResult(
        snapshot,
        { verdict: "APPROVE", confidence: 9, reasoning: "looks good", issues: [] },
        s,
        "rev_test_30_gen",
        mockPi,
        ctx,
      );

      assert.strictEqual(recRes.superseded, true, "reconcile must flag review as superseded");
      assert.strictEqual(s.awaitingReview, null, "awaitingReview must be cleared");
      assert.strictEqual(agentMessages.length, 0, "stale findings must not be delivered to agent messages");

      // Cleanup
      setCustomSubagentRunner(null);
      await rm(draftPath, { force: true });
      await rm(genDraftPath, { force: true });
      await rm(`${currentDir}/${qid}`, { recursive: true, force: true });
      await rm(`${currentDir}/${genQid}`, { recursive: true, force: true });
      await mkdir(".pi/quest/future", { recursive: true });
    },
  );

  await t.step(
    "31. draft file write permissions, absolute path classification, and draft_revision phase preservation",
    async () => {
      const { mockPi, handlers } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_31");
      plugin(mockPi);

      const emitToolCall = async (toolName: string, input: any) => {
        for (const cb of handlers["tool_call"] || []) {
          const res = await cb({ toolName, input }, ctx);
          if (res?.block) return res;
        }
        return undefined;
      };

      const s = getState(ctx);
      const slug = "test-draft-perms";
      const qid = `qid_31_${slug}`;
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const absDraftPath = `/home/quirinpa/site/.pi/quest/future/${slug}.md`;
      const relDraftPath = `.pi/quest/future/${slug}.md`;

      // 1. isFutureDraftPath helper checks
      assert.strictEqual(isFutureDraftPath(relDraftPath, slug), true, "relative path must match");
      assert.strictEqual(isFutureDraftPath(absDraftPath, slug), true, "absolute path must match");
      assert.strictEqual(isFutureDraftPath(absDraftPath, null), true, "absolute path must match even with null activeDraft");
      assert.strictEqual(isFutureDraftPath("src/main.c", slug), false, "code path must not match");

      // 2. classifyToolCall checks
      assert.strictEqual(
        classifyToolCall("edit", { path: absDraftPath }),
        "journal",
        "absolute draft path edit must be classified as journal",
      );
      assert.strictEqual(
        classifyToolCall("write", { file: absDraftPath }),
        "journal",
        "absolute draft path write via file prop must be classified as journal",
      );
      assert.strictEqual(
        classifyToolCall("edit", { path: "src/main.c" }),
        "implementation",
        "regular code edit must remain implementation",
      );

      // 3. Tool gating: draft edits allowed during AWAITING_REVIEW
      s.awaitingReview = {
        kind: "plan_review",
        reviewId: "rev_31_active",
        since: Date.now(),
      };

      const awaitingBlock = await asyncContext.run(
        ctx,
        () => emitToolCall("edit", { path: absDraftPath }),
      );
      assert.strictEqual(
        awaitingBlock,
        undefined,
        "draft edit must NOT be blocked during AWAITING_REVIEW",
      );

      // Verify code edit IS blocked during AWAITING_REVIEW
      const codeBlock = await asyncContext.run(
        ctx,
        () => emitToolCall("edit", { path: "src/main.c" }),
      );
      assert.ok(codeBlock && codeBlock.block, "code edit must still be blocked during AWAITING_REVIEW");

      // 4. Tool gating: draft edits allowed during RESEARCH_PENDING / when promoted
      s.awaitingReview = null;
      s.activeDraft = null;
      s.active = "other-quest";
      s.researchRequired = true;
      s.researchComplete = false;

      const researchBlock = await asyncContext.run(
        ctx,
        () => emitToolCall("edit", { path: absDraftPath }),
      );
      assert.strictEqual(
        researchBlock,
        undefined,
        "draft edit must NOT be blocked during RESEARCH_PENDING",
      );

      // 5. Gating state: DRAFT_PENDING vs DRAFT_REVISION_PENDING
      s.active = "";
      s.activeDraft = slug;
      s.lastCriticalReview = null;
      s.lastPlanReviewApproval = null;

      const draftPendingReason = getImplementationBlockReason(s, ctx);
      assert.strictEqual(
        draftPendingReason.stateName,
        "DRAFT_PENDING",
        "unreviewed draft must return DRAFT_PENDING",
      );
      assert.strictEqual(
        draftPendingReason.code,
        QuestErrorCode.DRAFT_REVIEW_REQUIRED,
        "code must be DRAFT_REVIEW_REQUIRED",
      );

      // Simulate reviewer returning REVISE
      s.lastCriticalReview = {
        id: "rev_31_revised",
        questId: qid,
        kind: "plan_review",
        reviewedStateVersion: { planVersion: 1, saveHash: null, saveCount: 0 },
        verdict: "REVISE",
        severity: "MAJOR",
        findings: [{ issue: "Needs concrete targets", evidence: "index.c:42" }],
        requiredActions: ["Add targets"],
        resolved: false,
        timestamp: Date.now(),
        snapshot: {
          questId: qid, sessionId: "s", reviewId: "rev_31_revised",
          reviewKind: "plan_review", planVersion: 1,
          boundaryKey: `draft:${slug}:initialhash`, saveGeneration: 0,
          stateHash: null, originalUserRequest: "test",
          currentUnderstanding: "", assumptions: "", plan: "",
          planRevisions: "", findings: "", filesChanged: "",
          relevantDiff: "", testStatus: "", nextAction: "",
          createdAt: Date.now(),
        },
      };

      const reviseReason = getImplementationBlockReason(s, ctx);
      assert.strictEqual(
        reviseReason.stateName,
        "DRAFT_REVISION_PENDING",
        "draft after REVISE must return DRAFT_REVISION_PENDING",
      );

      // Simulate file edit (hash changes, snapshot is no longer current)
      // Must still remain DRAFT_REVISION_PENDING until an approval arrives!
      if (s.lastCriticalReview?.snapshot) {
        s.lastCriticalReview.snapshot.boundaryKey = `draft:${slug}:old_stale_hash`;
      }
      const afterEditReason = getImplementationBlockReason(s, ctx);
      assert.strictEqual(
        afterEditReason.stateName,
        "DRAFT_REVISION_PENDING",
        "draft must remain DRAFT_REVISION_PENDING even after file edit drift until approved",
      );

      // Once approved, revision is no longer outstanding
      s.lastPlanReviewApproval = {
        questId: qid,
        planVersion: 1,
        reviewId: "rev_31_approved",
        boundaryKey: `draft:${slug}:newhash`,
        saveHash: "newhash",
        saveCount: 1,
        timestamp: Date.now(),
      };
      const approvedReason = getImplementationBlockReason(s, ctx);
      assert.strictEqual(
        approvedReason.stateName,
        "DRAFT_PENDING",
        "draft after approval must return DRAFT_PENDING, not DRAFT_REVISION_PENDING",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 32. quest_update_state authors draft content directly into future/
  // without promoting to active, triggers draft review, and quest_mark_saved preserves activeDraft
  // -----------------------------------------------------------------------
  await t.step(
    "32. quest_update_state authors draft content in future/ without promoting to active, triggers draft review, and quest_mark_saved preserves activeDraft",
    async () => {
      const { mockPi, handlers, agentMessages } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_32");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "author-draft-test";
      const qid = "1788550749";
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];
      s.reassessmentRequired = false;

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(
        draftPath,
        `# Draft: ${slug}\n\n## Implementation Plan\n1. Initial skeleton\n`,
        "utf8",
      );

      let reviewerCalled = false;
      setCustomSubagentRunner(async () => {
        reviewerCalled = true;
        return `PASS 1:\nProvisional Judgment: REVISE\nPASS 2:\n- Revised Judgment: REVISE\nPROMPT-COMPLIANCE:\n- Requirement: error handling -> Status: UNSATISFIED\nVERDICT: REVISE\nSEVERITY: MAJOR\nFINDINGS:\n- Plan missing error handling\nREQUIRED REVISIONS:\n- Add error handling`;
      });

      // 1. Verify before_agent_start does NOT emit "establish durable quest" when drafting
      const beforeHandlers = handlers["before_agent_start"] || [];
      agentMessages.length = 0;
      let promptRes: any = null;
      for (const h of beforeHandlers) {
        promptRes = await asyncContext.run(ctx, () =>
          h({ systemPrompt: "base prompt" }, ctx)
        );
      }
      assert.strictEqual(
        agentMessages.some((m) =>
          typeof m.msg === "string" &&
          m.msg.includes("establish durable quest via quest_update_state now")
        ),
        false,
        "before_agent_start must not tell agent to establish durable quest when activeDraft is set",
      );
      assert.ok(
        agentMessages.some((m) =>
          typeof m.msg === "string" &&
          m.msg.includes(`Draft active for '${slug}'`)
        ),
        "before_agent_start should steer agent to author proposal in future draft file",
      );
      assert.ok(
        promptRes?.systemPrompt?.includes(`DRAFT ACTIVE for '${slug}'`),
        "before_agent_start should include draft skill hint in system prompt",
      );

      // 2. Call quest_update_state with draft implementation plan and research findings
      const updateRes = await asyncContext.run(ctx, () =>
        executeUpdateStateTool(
          {
            name: slug,
            goal: "Refactor consumer side complexity",
            requirements: ["Decouple renderer", "Pure C state"],
            plan: [
              "1. Inspect consumer dependencies",
              "2. Introduce boundary abstraction",
              "3. Verify SSR rendering",
            ],
            findings: [
              "Renderer calls axil directly in 3 places",
              "Boundary abstraction reduces coupling",
            ],
          },
          mockPi,
          ctx,
        )
      );

      assert.strictEqual(
        updateRes.details?.success,
        true,
        `quest_update_state should succeed for draft: ${JSON.stringify(updateRes)}`,
      );
      assert.strictEqual(
        s.activeDraft,
        slug,
        "state.activeDraft must remain set to draft slug",
      );
      assert.strictEqual(
        s.active,
        "",
        "state.active must NOT be set or promoted before approval",
      );

      // Verify draft file content on disk has updated sections
      const updatedContent = await readFile(draftPath, "utf8");
      assert.ok(
        updatedContent.includes("## Goals & Scope"),
        "draft file must contain Goals & Scope",
      );
      assert.ok(
        updatedContent.includes("Refactor consumer side complexity"),
        "draft file must contain updated goal",
      );
      assert.ok(
        updatedContent.includes("## Implementation Plan"),
        "draft file must contain Implementation Plan",
      );
      assert.ok(
        updatedContent.includes("1. Inspect consumer dependencies"),
        "draft file must contain updated plan steps",
      );
      assert.ok(
        updatedContent.includes("## Research Findings"),
        "draft file must contain Research Findings",
      );
      assert.ok(
        updatedContent.includes("Renderer calls axil directly in 3 places"),
        "draft file must contain research findings",
      );

      // Verify reviewer was triggered
      assert.strictEqual(
        reviewerCalled,
        true,
        "adversarial plan review must be triggered when draft plan is authored via quest_update_state",
      );

      // 3. Call executeMarkTool and verify activeDraft is preserved and state.active is not corrupted
      const markRes = await asyncContext.run(ctx, () =>
        executeMarkTool(
          { name: "consumer-complexity-abstraction" },
          mockPi,
          ctx,
        )
      );

      assert.ok(
        !markRes.details?.error,
        `executeMarkTool must not fail for draft: ${JSON.stringify(markRes)}`,
      );
      assert.ok(
        markRes.details?.hash,
        "executeMarkTool must return hash for draft",
      );
      assert.ok(
        markRes.content[0]?.text?.includes(slug),
        "executeMarkTool response must reference draft slug",
      );
      assert.strictEqual(
        s.activeDraft,
        slug,
        "state.activeDraft must remain set after quest_mark_saved",
      );
      assert.strictEqual(
        s.active,
        "",
        "state.active must NOT be corrupted to arbitrary name during drafting",
      );

      // Cleanup
      setCustomSubagentRunner(null);
      await rm(draftPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 33. awaitingReview cleared on review approval (no persistent hourglass)
  // and questId strictly protected as numeric timestamp against slug corruption
  // -----------------------------------------------------------------------
  await t.step(
    "33. awaitingReview cleared on review approval (no persistent hourglass) and questId strictly protected as numeric timestamp against slug corruption",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctx = createMockContext(50000, "session_plan_33");
      plugin(mockPi);

      const s = getState(ctx);
      const slug = "look-consumer-side-code-lot-complexity";
      const qid = "1788552255";
      s.questId = qid;
      s.activeDraft = slug;
      s.active = "";
      s.stack = [];

      const futureDir = ".pi/quest/future";
      await mkdir(futureDir, { recursive: true });
      const draftPath = `${futureDir}/${slug}.md`;
      await writeFile(
        draftPath,
        `# Draft: ${slug}\n\n## Implementation Plan\n1. Step A\n`,
        "utf8",
      );

      // Part A: executeMarkTool on draft does NOT mutate questId to the draft slug
      const markRes = await asyncContext.run(ctx, () =>
        executeMarkTool({ name: slug }, mockPi, ctx)
      );
      assert.ok(!markRes.details?.error, "executeMarkTool should succeed for draft");
      assert.strictEqual(
        s.questId,
        qid,
        "s.questId must remain the numeric timestamp and not be overwritten by the draft slug",
      );
      const formattedStatus = formatQuestShort(s, true, ctx);
      assert.ok(
        formattedStatus.includes(`#${qid}`),
        `status bar must format with numeric qid #${qid}: got ${formattedStatus}`,
      );
      assert.strictEqual(
        formattedStatus.includes(`#${slug}`),
        false,
        `status bar must NOT format with draft slug: got ${formattedStatus}`,
      );

      // Part B: Review reconciliation clears awaitingReview and inCriticalReview
      const revId = "rev_test_33_approval";
      s.inCriticalReview = true;
      s.awaitingReview = {
        kind: "plan_review",
        reviewId: revId,
        since: Date.now(),
      };

      // While awaiting review, barIcon must return hourglass ⏳
      assert.strictEqual(barIcon(s, true), "⏳", "barIcon must be hourglass while awaiting review");

      const { createHash } = await import("node:crypto");
      const draftHash = createHash("sha256")
        .update(`# Draft: ${slug}\n\n## Implementation Plan\n1. Step A\n`)
        .digest("hex")
        .slice(0, 12);

      const snapshot: any = {
        questId: slug,
        sessionId: "sess_33",
        reviewId: revId,
        reviewKind: "plan_review",
        planVersion: 1,
        boundaryKey: `draft:${slug}:${draftHash}`,
        saveGeneration: 1,
        stateHash: draftHash,
      };

      const { reconcileReviewResult } = await import(
        "../src/critical_agent/policy/reconcile.ts"
      );
      const recRes = await reconcileReviewResult(
        snapshot,
        {
          verdict: "APPROVE",
          severity: "NONE",
          confidence: 9,
          reasoning: "plan looks solid",
          findings: [],
          requiredActions: [],
        },
        s,
        revId,
        mockPi,
        ctx,
      );

      assert.strictEqual(recRes.success, true, "reconciliation must succeed for APPROVE");
      assert.strictEqual(s.awaitingReview, null, "awaitingReview must be cleared to null upon APPROVE");
      assert.strictEqual(s.inCriticalReview, false, "inCriticalReview must be false upon APPROVE");

      // Once cleared, barIcon must NOT be hourglass ⏳; it must be 📝 for approved draft
      const postApprovalIcon = barIcon(s, true);
      assert.strictEqual(
        postApprovalIcon,
        "📝",
        `barIcon must be 📝 for approved draft, not ⏳: got ${postApprovalIcon}`,
      );
      const postApprovalStatus = formatQuestShort(s, true, ctx);
      assert.strictEqual(
        postApprovalStatus.includes("⏳"),
        false,
        `post-approval status must NOT contain hourglass ⏳: got ${postApprovalStatus}`,
      );

      // Part C: Session reconstruction heals corrupted slug questId
      const branchCtx = createMockContext(50000, "session_plan_33_reconstruct");
      const branchManager = branchCtx.sessionManager;
      // Append initial entry with valid numeric questId
      branchManager.appendCustomEntry("quest_journal", {
        questId: "1788552255",
        activeDraft: slug,
        active: null,
      });
      // Append subsequent corrupted entry with slug as questId
      branchManager.appendCustomEntry("quest_journal", {
        questId: slug,
        activeDraft: slug,
        active: null,
      });

      const reconstructed = reconstruct(branchCtx);
      assert.strictEqual(
        reconstructed.questId,
        "1788552255",
        "reconstruct must heal corrupted slug questId to the latest numeric timestamp from branch history",
      );

      // Cleanup
      await rm(draftPath, { force: true });
    },
  );

  // Clean up
  await rm(currentDir, { recursive: true, force: true });
  await mkdir(".pi/quest/future", { recursive: true });
});

