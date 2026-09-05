import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension, { QuestLifecycleState } from "../index.ts";
import { resolveQuestRecordBySlug } from "../src/paths.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function getQuestContentBySlug(slug: string): Promise<string> {
  const rec = await resolveQuestRecordBySlug(slug);
  if (!rec) throw new Error(`Quest record not found for slug: ${slug}`);
  return readFile(rec.path, "utf8");
}

Deno.test("quest_journal_iterative_research: complete verification of all 10 state transitions", async (t) => {
  const currentDir = ".pi/quest/current";
  const archiveDir = ".pi/quest/archive";
  await rm(currentDir, { recursive: true, force: true });
  await rm(archiveDir, { recursive: true, force: true });
  await mkdir(currentDir, { recursive: true });
  await mkdir(archiveDir, { recursive: true });

  const rootQuestSlug = "test-root-iterative-quest";
  const childQuestSlug = "test-child-investigation-quest";

  const handlers: Record<string, EventCallback[]> = {};
  const tools: Record<string, any> = {};
  const commands: Record<string, any> = {};
  let allMessages: Array<{ text: string; options?: any }> = [];
  let entries: any[] = [];

  const mockPi: any = {
    on(event: string, callback: EventCallback) {
      if (!handlers[event]) handlers[event] = [];
      handlers[event].push(callback);
    },
    appendEntry(type: string, data: any) {
      entries.push({ type, data });
    },
    registerEntryRenderer() {},
    registerTool(toolDef: any) {
      tools[toolDef.name] = toolDef;
    },
    registerCommand(name: string, commandDef: any) {
      commands[name] = commandDef;
    },
    sendUserMessage(msg: any, options?: any) {
      const text = typeof msg === "string"
        ? msg
        : msg?.content || JSON.stringify(msg);
      allMessages.push({ text, options });
    },
    sendMessage(msg: any, options?: any) {
      const text = typeof msg === "string"
        ? msg
        : msg?.content || JSON.stringify(msg);
      allMessages.push({ text, options });
    },
  };

  questJournalExtension(mockPi);

  const mockCtx: any = {
    cwd: process.cwd(),
    getContextUsage: () => ({
      tokens: 50_000,
      contextWindow: 200_000,
      percent: 25,
    }),
    sessionManager: {
      getBranch: () =>
        entries.map((e) => ({
          type: "custom",
          customType: "quest_journal",
          data: e.data,
        })),
    },
    ui: {
      notify() {},
      setStatus() {},
      setWidget() {},
    },
    hasUI: true,
    mode: "tui",
    compact: (options: any) => {
      if (options?.onComplete) options.onComplete();
    },
  };

  // -----------------------------------------------------------------------
  // 1. New root quest starts in RESEARCH_PENDING
  // -----------------------------------------------------------------------
  await t.step(
    "1. New root quest starts in RESEARCH_PENDING with Low confidence",
    async () => {
      allMessages = [];

      await commands["quest"].handler("test-root-iterative-quest", mockCtx);
      assert.ok(
        allMessages.length > 0,
        "Turn 1 start message should be queued",
      );
      const startMsg = allMessages[0].text;
      assert.ok(
        startMsg.includes(
          "Iterative Research, Planning & Falsification Protocol",
        ),
        "Must use iterative protocol",
      );

      const state = entries[entries.length - 1].data;
      assert.strictEqual(state.active, rootQuestSlug);
      assert.strictEqual(
        state.researchComplete,
        false,
        "Initial researchComplete must be false",
      );
      assert.strictEqual(
        state.researchRequired,
        true,
        "Initial researchRequired must be true",
      );
      assert.strictEqual(
        state.planConfidence,
        "low",
        "Initial plan confidence must be low",
      );
      assert.strictEqual(state.planVersion, 1, "Initial planVersion must be 1");
    },
  );

  // -----------------------------------------------------------------------
  // 2. Structural validation prevents claiming researchComplete with placeholders
  // -----------------------------------------------------------------------
  await t.step(
    "2. Structural validation prevents claiming researchComplete with empty/placeholder sections",
    async () => {
      // Attempt to set researchComplete = true while sections are still template placeholders
      const res = await tools["quest_update_state"].execute(
        "call_check_placeholders",
        {
          name: rootQuestSlug,
          status: "Prematurely claiming research complete",
          researchComplete: true,
        },
        null,
        null,
        mockCtx,
      );

      assert.ok(
        res.content[0].text.includes("researchComplete refused"),
        "Must refuse premature researchComplete transition",
      );
      const state = entries[entries.length - 1].data;
      assert.strictEqual(
        state.researchComplete,
        false,
        "researchComplete must remain false",
      );
      assert.strictEqual(
        state.researchRequired,
        true,
        "researchRequired must remain true",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 3. Falsification & Plan Revision automatically increments planVersion
  // -----------------------------------------------------------------------
  await t.step(
    "3. Falsification & Plan Revision automatically increments planVersion",
    async () => {
      for (const cb of handlers["tool_call"] || []) {
        await cb(
          { toolName: "read", input: { path: "mods/song/module_y.c" } },
          mockCtx,
        );
      }
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/module_y.c" },
          output: "module y code",
          isError: false,
        }, mockCtx);
      }

      // Agent populates all required epistemic sections with real findings and revises plan from hypothesis test
      await tools["quest_update_state"].execute(
        "call_populate_epistemic",
        {
          name: rootQuestSlug,
          status:
            "Hypothesis tested: Module Y is single-threaded; plan revised to use async queue",
          understanding:
            "Module Y is strictly single-threaded and requires async message dispatch.",
          assumptions: [
            "[x] Module Y requires async queue dispatch (verified in module_y.c)",
          ],
          openQuestions: [
            "- All material uncertainties resolved in research round 1.",
          ],
          findings: [
            "Inspected module_y.c: found strict thread-affinity checks",
          ],
          plan: [
            "1. Route requests through async event queue",
            "2. Dispatch to module Y on dedicated worker",
            "3. Add concurrency integration tests",
          ],
          planConfidence: "high",
          planRevisions: [
            "v1 -> v2: Direct calls rejected because module Y is not thread-safe; switched to async queue architecture.",
          ],
          rejectedApproaches: [
            "Direct synchronous invocation of module Y (caused thread affinity panic in inspection).",
          ],
          researchComplete: true,
          exactNextAction: "Implement async queue dispatcher for module Y",
        },
        null,
        null,
        mockCtx,
      );

      const state = entries[entries.length - 1].data;
      assert.strictEqual(
        state.planVersion,
        2,
        "planVersion must auto-increment to 2",
      );
      assert.strictEqual(
        state.planConfidence,
        "high",
        "planConfidence must be high",
      );
      assert.strictEqual(
        state.researchComplete,
        true,
        "researchComplete should now be true",
      );
      assert.strictEqual(
        state.researchRequired,
        false,
        "researchRequired should now be false",
      );

      const content = await getQuestContentBySlug(rootQuestSlug);
      assert.ok(
        content.includes("## Plan Revisions"),
        "Must persist Plan Revisions",
      );
      assert.ok(
        content.includes("## Rejected Approaches"),
        "Must persist Rejected Approaches",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 4. Meaningful failure invalidates research readiness and sets reassessmentRequired
  // -----------------------------------------------------------------------
  await t.step(
    "4. Test failure in turn_end invalidates research readiness and triggers reassessment",
    async () => {
      allMessages = [];

      const turnEndEvent = {
        toolResults: [
          {
            toolName: "bash",
            input: { command: "make test" },
            output:
              "make: *** [Makefile:42: test] Error 1\nFAIL: test_queue_dispatch assertion failed",
            isError: true,
          },
        ],
      };

      for (const cb of handlers["turn_end"] || []) {
        await cb(turnEndEvent, mockCtx);
      }

      const state = entries[entries.length - 1].data;
      assert.strictEqual(
        state.reassessmentRequired,
        true,
        "reassessmentRequired must be true",
      );
      assert.strictEqual(
        state.consecutiveFailures,
        1,
        "consecutiveFailures must be 1",
      );
      assert.strictEqual(
        state.researchComplete,
        false,
        "researchComplete must be invalidated to false",
      );
      assert.strictEqual(
        state.researchRequired,
        true,
        "researchRequired must be reset to true",
      );
      // A1: confidence preserved — not forced to low on trigger
      assert.notStrictEqual(
        state.planConfidence,
        "low",
        "planConfidence must be preserved (not forced to low)",
      );
      assert.strictEqual(
        state.researchRound,
        2,
        "researchRound must increment to 2",
      );
      assert.strictEqual(
        state.reassessmentVersion,
        1,
        "reassessmentVersion must increment to 1",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 5. Saving quest does NOT clear reassessmentRequired (only full valid reassessmentComplete does)
  // -----------------------------------------------------------------------
  await t.step(
    "5. Saving quest does NOT clear reassessmentRequired, and partial reassessment is refused",
    async () => {
      // Ordinary save (e.g. marking progress or logging touched files)
      await tools["quest_update_state"].execute(
        "call_ordinary_save",
        {
          name: rootQuestSlug,
          status: "Analyzing failure logs",
          filesExamined: ["src/queue_dispatcher.c"],
        },
        null,
        null,
        mockCtx,
      );

      let state = entries[entries.length - 1].data;
      assert.strictEqual(
        state.reassessmentRequired,
        true,
        "Ordinary save must NOT clear reassessmentRequired",
      );
      assert.strictEqual(
        state.consecutiveFailures,
        1,
        "Ordinary save must NOT reset consecutiveFailures",
      );

      // Attempt incomplete reassessment with low confidence and missing plan validation
      const prematureRes = await tools["quest_update_state"].execute(
        "call_premature_reassessment",
        {
          name: rootQuestSlug,
          status: "Premature reassessment attempt",
          understanding: "Quick belief without full epistemic state.",
          planConfidence: "low",
          reassessmentComplete: true,
          exactNextAction: "Jump straight into implementation",
        },
        null,
        null,
        mockCtx,
      );

      assert.ok(
        prematureRes.content[0].text.includes("reassessmentComplete refused"),
        "Must refuse incomplete reassessment",
      );
      state = entries[entries.length - 1].data;
      assert.strictEqual(
        state.reassessmentRequired,
        true,
        "reassessmentRequired must remain true on failed validation",
      );
      assert.strictEqual(
        state.researchComplete,
        false,
        "researchComplete must remain false on failed validation",
      );
      assert.strictEqual(
        state.planConfidence,
        "low",
        "planConfidence must remain low on failed validation",
      );

      for (const cb of handlers["tool_call"] || []) {
        await cb(
          { toolName: "read", input: { path: "mods/song/ring_pop.c" } },
          mockCtx,
        );
      }
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/ring_pop.c" },
          output: "ring pop code",
          isError: false,
        }, mockCtx);
      }

      // Now explicitly resolve reassessment with complete replacement epistemic state
      await tools["quest_update_state"].execute(
        "call_resolve_reassessment",
        {
          name: rootQuestSlug,
          status: "Fixed queue synchronization bug; all tests passing",
          understanding:
            "Queue dispatcher requires memory barrier on ring buffer pop.",
          assumptions: [
            "[x] Memory barrier prevents stale reads on worker thread",
          ],
          findings: ["Identified missing atomic fence in ring_pop()"],
          openQuestions: ["- None."],
          plan: ["1. Add memory barrier to ring_pop()", "2. Run stress tests"],
          planConfidence: "high",
          planConfidenceReason:
            "Stress test passed 10,000 iterations without deadlock",
          reassessmentConclusion:
            "Investigation identified race condition in ring_pop(); added memory barrier and verified with 10,000 stress test iterations.",
          reassessmentComplete: true,
          exactNextAction: "Proceed to sub-quest for cache integration",
        },
        null,
        null,
        mockCtx,
      );

      state = entries[entries.length - 1].data;
      assert.strictEqual(
        state.reassessmentRequired,
        false,
        "reassessmentComplete: true must clear reassessmentRequired",
      );
      assert.strictEqual(
        state.researchComplete,
        true,
        "researchComplete should be true after valid reassessment",
      );
      assert.strictEqual(
        state.consecutiveFailures,
        0,
        "reassessmentComplete: true must reset consecutiveFailures",
      );
      assert.strictEqual(
        state.resolvedReassessmentVersion,
        1,
        "resolvedReassessmentVersion must match reassessmentVersion",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 6. Failure history survives reconstruction across session reloads
  // -----------------------------------------------------------------------
  await t.step(
    "6. consecutiveFailures and researchRound survive session reconstruction",
    async () => {
      // Set consecutiveFailures = 2
      const stateBefore = entries[entries.length - 1].data;
      stateBefore.consecutiveFailures = 2;
      stateBefore.researchRound = 3;

      // Trigger session_start reconstruction
      for (const cb of handlers["session_start"] || []) {
        await cb({}, mockCtx);
      }

      // Check reconstructed state
      const reconstructedState = entries[entries.length - 1].data;
      assert.strictEqual(
        reconstructedState.consecutiveFailures,
        2,
        "consecutiveFailures must survive reconstruction",
      );
      assert.strictEqual(
        reconstructedState.researchRound,
        3,
        "researchRound must survive reconstruction",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 7. Subquest independently investigates and disproves parent assumption
  // -----------------------------------------------------------------------
  await t.step(
    "7. Subquest independently investigates and disproves parent assumption",
    async () => {
      allMessages = [];

      await tools["quest_subquest"].execute(
        "call_spawn_child",
        {
          name: childQuestSlug,
          goal: "Verify cache layer key sizing",
          parentName: rootQuestSlug,
          switchNow: true,
        },
        null,
        null,
        mockCtx,
      );

      assert.ok(allMessages.length > 0, "Subquest directive should be queued");
      assert.ok(
        allMessages[allMessages.length - 1].text.includes(
          "Sub-Quest Iterative Research",
        ),
      );

      for (const cb of handlers["tool_call"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/hyle_cache.c" },
        }, mockCtx);
      }
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/hyle_cache.c" },
          output: "hyle cache code",
          isError: false,
        }, mockCtx);
      }

      // Child completes research with independent finding
      await tools["quest_update_state"].execute(
        "call_child_research",
        {
          name: childQuestSlug,
          status: "Investigation complete: 32-bit keys required",
          understanding: "Cache layer uses 32-bit keys (hyle_cache.c:42).",
          assumptions: ["[x] 64-bit keys must be folded to 32-bit"],
          openQuestions: ["- None."],
          findings: ["Found uint32_t type constraint in cache driver."],
          rejectedApproaches: ["Direct 64-bit key indexing without folding."],
          plan: ["1. Implement MurmurHash3 folding", "2. Add test"],
          planConfidence: "high",
          researchComplete: true,
          exactNextAction: "Archive child subquest",
        },
        null,
        null,
        mockCtx,
      );

      const childContent = await getQuestContentBySlug(childQuestSlug);
      assert.ok(
        childContent.includes("Cache layer uses 32-bit keys"),
        "Child must record independent findings",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 8. Subquest archive returns to parent with child findings evaluation
  // -----------------------------------------------------------------------
  await t.step(
    "8. Subquest archive returns to parent with child findings evaluation",
    async () => {
      allMessages = [];

      const archiveRes = await tools["quest_archive"].execute(
        "call_archive_child",
        {
          questName: childQuestSlug,
          compact: false,
        },
        null,
        null,
        mockCtx,
      );

      assert.strictEqual(
        archiveRes.details.nextActive,
        rootQuestSlug,
        "Parent must be resumed",
      );
      const parentState = entries[entries.length - 1].data;
      assert.strictEqual(parentState.active, rootQuestSlug);

      assert.ok(
        allMessages.length > 0,
        "Parent directive with child findings must be queued",
      );
      const parentPrompt = allMessages[allMessages.length - 1].text;
      assert.ok(
        parentPrompt.includes("Child Sub-Quest Results & Established Findings"),
        "Must deliver child findings",
      );
      assert.ok(
        parentPrompt.includes("32-bit"),
        "Must include specific child discovery",
      );
      assert.ok(
        parentPrompt.includes("Parent Evaluation & Resumption Protocol"),
        "Must instruct parent evaluation",
      );

      // Parent evaluates child findings: child proved uint32 constraint affecting cache architecture
      // Parent enters reassessment and updates plan
      for (const cb of handlers["tool_call"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/hyle_cache.c" },
        }, mockCtx);
      }
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/hyle_cache.c" },
          output: "hyle cache code",
          isError: false,
        }, mockCtx);
      }

      await tools["quest_update_state"].execute(
        "call_parent_reassess_done",
        {
          name: rootQuestSlug,
          status: "Parent plan updated with child 32-bit folded cache",
          understanding:
            "Subsystem X uses async queue + 32-bit folded cache keys.",
          assumptions: ["[x] 32-bit key folding validated in child subquest"],
          findings: [
            "Child subquest proved uint32 constraint in cache driver.",
          ],
          openQuestions: ["- None."],
          plan: [
            "1. Integrate async queue",
            "2. Integrate 32-bit folded cache bridge",
            "3. Run full test suite",
          ],
          planConfidence: "high",
          planConfidenceReason: "Child integration verified end-to-end",
          planRevisions: [
            "v2 -> v3: Integrated 32-bit folded cache design from child subquest.",
          ],
          reassessmentConclusion:
            "Child findings confirm cache driver uint32 constraint; parent plan v3 integrates 32-bit folded cache bridge.",
          reassessmentComplete: true,
          exactNextAction: "Integrate folded cache bridge",
        },
        null,
        null,
        mockCtx,
      );

      const finalParentState = entries[entries.length - 1].data;
      assert.strictEqual(
        finalParentState.reassessmentRequired,
        false,
        "Parent reassessment should be resolved",
      );
      assert.strictEqual(
        finalParentState.planVersion,
        3,
        "Plan version should be 3",
      );

      // Test unaffected child return: launch child 2, complete work that does NOT affect parent plan
      const child2Slug = "verify-logger-formatting";
      await tools["quest_subquest"].execute(
        "call_spawn_child2",
        {
          name: child2Slug,
          goal: "Verify logger timestamp formatting",
          parentName: rootQuestSlug,
          switchNow: true,
        },
        null,
        null,
        mockCtx,
      );

      for (const cb of handlers["tool_call"] || []) {
        await cb(
          { toolName: "read", input: { path: "mods/song/logger.c" } },
          mockCtx,
        );
      }

      await tools["quest_update_state"].execute(
        "call_child2_done",
        {
          name: child2Slug,
          status: "Logger timestamp verified: standard ISO-8601 format",
          understanding:
            "Logger outputs ISO-8601 timestamps without millisecond truncation.",
          assumptions: ["[x] ISO-8601 format verified in hyle_log.c:12"],
          openQuestions: ["- None."],
          findings: ["No changes needed to logger format."],
          plan: ["1. Inspect logger", "2. Verify output"],
          planConfidence: "high",
          researchComplete: true,
          exactNextAction: "Archive child2 subquest",
        },
        null,
        null,
        mockCtx,
      );

      allMessages = [];
      const archiveRes2 = await tools["quest_archive"].execute(
        "call_archive_child2",
        {
          questName: child2Slug,
          compact: false,
        },
        null,
        null,
        mockCtx,
      );

      assert.strictEqual(
        archiveRes2.details.nextActive,
        rootQuestSlug,
        "Parent must be resumed after child2",
      );
      const parentStateAfterChild2 = entries[entries.length - 1].data;
      assert.strictEqual(
        parentStateAfterChild2.reassessmentRequired,
        false,
        "Parent must NOT have fabricated reassessmentRequired",
      );

      // Parent determines plan is not affected: records findings and continues without fabricating reassessment
      await tools["quest_update_state"].execute(
        "call_parent_unaffected_update",
        {
          name: rootQuestSlug,
          findings: [
            "Child subquest proved uint32 constraint in cache driver.",
            "Child2 verified ISO-8601 logger formatting (no parent plan change needed).",
          ],
          exactNextAction: "Integrate folded cache bridge",
        },
        null,
        null,
        mockCtx,
      );

      const parentStateAfterUnaffected = entries[entries.length - 1].data;
      assert.strictEqual(
        parentStateAfterUnaffected.reassessmentRequired,
        false,
      );
      assert.strictEqual(
        parentStateAfterUnaffected.planVersion,
        3,
        "Plan version remains 3 without fabricated revision",
      );
      assert.strictEqual(
        parentStateAfterUnaffected.planConfidence,
        "high",
        "High confidence preserved",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 9. Post-compaction recovery branches on state
  // -----------------------------------------------------------------------
  await t.step("9. Post-compaction recovery branches on state", async () => {
    allMessages = [];

    // Compaction with plan established
    for (const cb of handlers["session_before_compact"] || []) {
      await cb({}, mockCtx);
    }
    for (const cb of handlers["session_compact"] || []) {
      await cb({}, mockCtx);
    }

    assert.ok(allMessages.length > 0, "Post-compaction prompt should be sent");
    const establishedMsg = allMessages[allMessages.length - 1].text;
    assert.ok(
      establishedMsg.includes("State: PLAN_ESTABLISHED"),
      "Should identify PLAN_ESTABLISHED state",
    );
    assert.ok(
      establishedMsg.includes(
        "Validate whether the current plan is still supported",
      ),
      "Should instruct plan validation",
    );
  });

  // -----------------------------------------------------------------------
  // 10. Meaningful failure in turn_end updates state without unprompted follow-up spam
  // -----------------------------------------------------------------------
  await t.step(
    "10. Meaningful failure in turn_end updates state without unprompted follow-up spam",
    async () => {
      allMessages = [];

      // Simulate turn_end receiving a failing test execution
      const failureEvent = {
        toolResults: [
          {
            toolName: "bash",
            input: { command: "make test_cache_bridge" },
            output:
              "make: *** [Makefile:42: test] Error 1\nFAIL: test_cache_bridge assertion failed",
            isError: true,
          },
        ],
      };

      for (const cb of handlers["turn_end"] || []) {
        await cb(failureEvent, mockCtx);
      }

      const stateAfterFailure = entries[entries.length - 1].data;
      assert.strictEqual(
        stateAfterFailure.reassessmentRequired,
        true,
        "Failure must set reassessmentRequired",
      );
      assert.strictEqual(
        stateAfterFailure.researchComplete,
        false,
        "Failure must reset researchComplete",
      );
      assert.strictEqual(
        allMessages.length,
        0,
        "turn_end must not queue out-of-band follow-ups that preempt user interaction",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 11. User refinement triggers reassessment and increments reassessmentVersion
  // -----------------------------------------------------------------------
  await t.step(
    "11. User refinement triggers reassessment and increments reassessmentVersion",
    async () => {
      await commands["quest-refine"].handler(
        "Support TLS 1.3 encryption on worker channel",
        mockCtx,
      );

      const stateAfterRefine = entries[entries.length - 1].data;
      assert.strictEqual(
        stateAfterRefine.reassessmentRequired,
        true,
        "Refinement must set reassessmentRequired",
      );
      assert.strictEqual(
        stateAfterRefine.researchComplete,
        false,
        "Refinement must invalidate researchComplete",
      );
      // A1: confidence preserved — refinement no longer forces low
      assert.notStrictEqual(
        stateAfterRefine.planConfidence,
        "low",
        "Refinement must preserve confidence (not force low)",
      );
      assert.ok(
        stateAfterRefine.reassessmentVersion > 1,
        "reassessmentVersion must increment on refinement",
      );

      for (const cb of handlers["tool_call"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/tls_worker.c" },
        }, mockCtx);
      }
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/tls_worker.c" },
          output: "tls worker code",
          isError: false,
        }, mockCtx);
      }

      // Resolve refinement reassessment
      await tools["quest_update_state"].execute(
        "call_resolve_refine",
        {
          name: rootQuestSlug,
          status: "Evaluated TLS 1.3 requirement: added openssl worker wrapper",
          understanding:
            "Worker channel uses TLS 1.3 socket wrapper (tls_worker.c).",
          assumptions: ["[x] TLS 1.3 supported via OpenSSL libssl"],
          findings: ["Verified TLS 1.3 socket handshake with mock server"],
          openQuestions: ["- None."],
          plan: [
            "1. Setup TLS socket",
            "2. Connect worker",
            "3. Verify handshake",
          ],
          planConfidence: "high",
          planConfidenceReason: "Tested with local mock TLS server",
          reassessmentConclusion:
            "TLS 1.3 integration verified; socket wrapper connects without blocking worker thread.",
          reassessmentComplete: true,
          exactNextAction: "Verify TLS handshake in test suite",
        },
        null,
        null,
        mockCtx,
      );

      const resolvedState = entries[entries.length - 1].data;
      assert.strictEqual(
        resolvedState.reassessmentRequired,
        false,
        "Reassessment from refinement resolved",
      );
      assert.strictEqual(
        resolvedState.resolvedReassessmentVersion,
        stateAfterRefine.reassessmentVersion,
      );
    },
  );

  // -----------------------------------------------------------------------
  // 12. Switching quests preserves mature epistemic state (Scenario E)
  // -----------------------------------------------------------------------
  await t.step(
    "12. Switching quests preserves mature epistemic state across switches",
    async () => {
      const tempQuestSlug = "test-temp-quest-b";
      await commands["quest"].handler(tempQuestSlug, mockCtx);
      const stateB = entries[entries.length - 1].data;
      assert.strictEqual(stateB.active, tempQuestSlug);

      // Switch back to Quest A
      await commands["quest"].handler(rootQuestSlug, mockCtx);
      const stateARecovered = entries[entries.length - 1].data;
      assert.strictEqual(stateARecovered.active, rootQuestSlug);
      assert.strictEqual(
        stateARecovered.planVersion,
        3,
        "Quest A must recover planVersion 3",
      );
      assert.strictEqual(
        stateARecovered.researchComplete,
        true,
        "Quest A must recover researchComplete = true",
      );
      assert.strictEqual(
        stateARecovered.planConfidence,
        "high",
        "Quest A must recover planConfidence = high",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 13. Existing sub-quest preserves epistemic state when switched back
  // -----------------------------------------------------------------------
  await t.step(
    "13. Existing sub-quest preserves epistemic state when switched back",
    async () => {
      const subQuestCSlug = "test-subquest-c-monitoring";
      const subQuestCPath = `${currentDir}/${subQuestCSlug}.md`;

      // 1. Create subquest C
      await tools["quest_subquest"].execute(
        "call_create_child_c",
        {
          name: subQuestCSlug,
          goal: "Setup telemetry and worker monitoring",
          parentName: rootQuestSlug,
          switchNow: true,
        },
        null,
        null,
        mockCtx,
      );

      // 2. Complete research on subquest C
      for (const cb of handlers["tool_call"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/monitoring.c" },
        }, mockCtx);
      }
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/monitoring.c" },
          output: "monitoring code",
          isError: false,
        }, mockCtx);
      }

      await tools["quest_update_state"].execute(
        "call_child_c_research",
        {
          name: subQuestCSlug,
          status: "Monitoring research complete",
          understanding: "Worker channel emits stats via atomic counters.",
          assumptions: ["[x] Atomic counters have zero lock contention"],
          findings: ["Benchmarked ring buffer counters at 50M ops/sec"],
          openQuestions: ["- None."],
          plan: ["1. Export atomic metrics", "2. Connect prometheus scraper"],
          planConfidence: "high",
          planConfidenceReason: "Benchmarked on target architecture",
          planVersion: 2,
          researchComplete: true,
          exactNextAction: "Implement prometheus metrics exporter",
        },
        null,
        null,
        mockCtx,
      );

      // 3. Switch away to root quest
      await commands["quest"].handler(rootQuestSlug, mockCtx);
      const rootSwitched = entries[entries.length - 1].data;
      assert.strictEqual(rootSwitched.active, rootQuestSlug);

      // 4. Switch back to subquest C
      await commands["quest"].handler(subQuestCSlug, mockCtx);
      const childCRecovered = entries[entries.length - 1].data;
      assert.strictEqual(childCRecovered.active, subQuestCSlug);
      assert.strictEqual(
        childCRecovered.planVersion,
        2,
        "Subquest C must preserve planVersion 2",
      );
      assert.strictEqual(
        childCRecovered.researchComplete,
        true,
        "Subquest C must preserve researchComplete = true",
      );
      assert.strictEqual(
        childCRecovered.planConfidence,
        "high",
        "Subquest C must preserve planConfidence = high",
      );

      // Switch back to root quest for remaining steps
      await commands["quest"].handler(rootQuestSlug, mockCtx);
    },
  );

  // -----------------------------------------------------------------------
  // 14. ensureRootQuestForPrompt preserves existing quest state
  // -----------------------------------------------------------------------
  await t.step(
    "14. ensureRootQuestForPrompt preserves existing quest state",
    async () => {
      // Create a fresh session context with no active quest
      const freshSessionCtx: any = {
        cwd: process.cwd(),
        getContextUsage: () => ({
          tokens: 10_000,
          contextWindow: 200_000,
          percent: 5,
        }),
        sessionManager: {
          id: "session_fresh_root_test",
          getBranch: () => [],
        },
        ui: {
          notify() {},
          setStatus() {},
          setWidget() {},
        },
        hasUI: true,
        mode: "tui",
      };

      for (const cb of handlers["before_agent_start"] || []) {
        await cb({
          prompt: "Implement test-root-iterative-quest in the codebase",
        }, freshSessionCtx);
      }

      const recoveredRootState = entries[entries.length - 1].data;
      assert.strictEqual(recoveredRootState.active, rootQuestSlug);
      assert.strictEqual(
        recoveredRootState.planVersion,
        3,
        "ensureRootQuestForPrompt must preserve planVersion 3",
      );
      assert.strictEqual(
        recoveredRootState.researchComplete,
        true,
        "ensureRootQuestForPrompt must preserve researchComplete",
      );
      assert.strictEqual(
        recoveredRootState.planConfidence,
        "high",
        "ensureRootQuestForPrompt must preserve high confidence",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 15. Compaction while reassessment is pending prioritizes reassessment
  // -----------------------------------------------------------------------
  await t.step(
    "15. Compaction while reassessment is pending prioritizes reassessment over stale next action",
    async () => {
      allMessages = [];

      // Trigger reassessment
      const failureEvent = {
        toolResults: [
          {
            toolName: "bash",
            input: { command: "make test_tls" },
            output: "FAIL: TLS handshake timeout",
            isError: true,
          },
        ],
      };

      for (const cb of handlers["turn_end"] || []) {
        await cb(failureEvent, mockCtx);
      }

      const stateBeforeCompact = entries[entries.length - 1].data;
      assert.strictEqual(stateBeforeCompact.reassessmentRequired, true);

      // Save quest so compaction checkpoint is clean & prepared
      await tools["quest_mark_saved"].execute(
        "call_save_before_compact",
        { name: rootQuestSlug },
        {},
        () => {},
        mockCtx,
      );

      allMessages = [];

      // Simulate compaction
      for (const cb of handlers["session_before_compact"] || []) {
        await cb({}, mockCtx);
      }
      for (const cb of handlers["session_compact"] || []) {
        await cb({}, mockCtx);
      }

      assert.ok(
        allMessages.length > 0,
        "Post compaction directive must be sent",
      );
      const compactPrompt = allMessages[allMessages.length - 1].text;
      assert.ok(
        compactPrompt.includes("State: REASSESSMENT_PENDING"),
        "Prompt must instruct REASSESSMENT_PENDING",
      );
      assert.ok(
        compactPrompt.includes("Do NOT jump into implementation"),
        "Must warn against blind implementation",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 16. lastResearchAt and lastPlanRevisionAt are preserved across quest switches
  // -----------------------------------------------------------------------
  await t.step(
    "16. lastResearchAt and lastPlanRevisionAt are preserved across quest switches",
    async () => {
      const questASlug = "test-timestamp-quest-a";
      const questAPath = `${currentDir}/${questASlug}.md`;
      const questBSlug = "test-timestamp-quest-b";
      const questBPath = `${currentDir}/${questBSlug}.md`;

      await rm(questAPath, { force: true });
      await rm(questBPath, { force: true });

      // 1. Initialize Quest A and complete research at T1
      await commands["quest"].handler(questASlug, mockCtx);
      await tools["quest_update_state"].execute(
        "call_update_quest_a",
        {
          name: questASlug,
          status: "Quest A research complete",
          understanding: "Architecture A understands protocol v1.",
          assumptions: ["[x] Protocol v1 stable"],
          openQuestions: ["- None."],
          findings: ["Verified protocol v1 packet format"],
          plan: ["1. Build protocol parser", "2. Add tests"],
          planConfidence: "high",
          planConfidenceReason: "Packet specs verified",
          planRevisions: ["v1 -> v2: Revised to async parser."],
          researchComplete: true,
          exactNextAction: "Build parser",
        },
        null,
        null,
        mockCtx,
      );

      const stateA_T1 = entries[entries.length - 1].data;
      const T1_research = stateA_T1.lastResearchAt;
      const T1_plan = stateA_T1.lastPlanRevisionAt;
      assert.ok(typeof T1_research === "number" && T1_research > 0);
      assert.ok(typeof T1_plan === "number" && T1_plan > 0);

      // Wait 15ms so timestamps are strictly distinguishable
      await new Promise((r) => setTimeout(r, 15));

      // 2. Switch to Quest B and complete research at T2
      await commands["quest"].handler(questBSlug, mockCtx);
      await tools["quest_update_state"].execute(
        "call_update_quest_b",
        {
          name: questBSlug,
          status: "Quest B research complete",
          understanding: "Architecture B understands protocol v2.",
          assumptions: ["[x] Protocol v2 stable"],
          openQuestions: ["- None."],
          findings: ["Verified protocol v2 stream format"],
          plan: ["1. Build streaming parser", "2. Add tests"],
          planConfidence: "high",
          planConfidenceReason: "Stream specs verified",
          planRevisions: ["v1 -> v2: Revised to ring buffer stream."],
          researchComplete: true,
          exactNextAction: "Build streaming parser",
        },
        null,
        null,
        mockCtx,
      );

      const stateB_T2 = entries[entries.length - 1].data;
      const T2_research = stateB_T2.lastResearchAt;
      const T2_plan = stateB_T2.lastPlanRevisionAt;
      assert.ok(typeof T2_research === "number" && T2_research > T1_research);
      assert.ok(typeof T2_plan === "number" && T2_plan > T1_plan);

      // 3. Switch back to Quest A -> A still reports T1
      await commands["quest"].handler(questASlug, mockCtx);
      const stateA_recovered = entries[entries.length - 1].data;
      assert.strictEqual(stateA_recovered.active, questASlug);
      assert.strictEqual(
        stateA_recovered.lastResearchAt,
        T1_research,
        "Quest A must restore exact T1 research timestamp",
      );
      assert.strictEqual(
        stateA_recovered.lastPlanRevisionAt,
        T1_plan,
        "Quest A must restore exact T1 plan revision timestamp",
      );

      // 4. Switch back to Quest B -> B still reports T2
      await commands["quest"].handler(questBSlug, mockCtx);
      const stateB_recovered = entries[entries.length - 1].data;
      assert.strictEqual(stateB_recovered.active, questBSlug);
      assert.strictEqual(
        stateB_recovered.lastResearchAt,
        T2_research,
        "Quest B must restore exact T2 research timestamp",
      );
      assert.strictEqual(
        stateB_recovered.lastPlanRevisionAt,
        T2_plan,
        "Quest B must restore exact T2 plan revision timestamp",
      );

      await rm(questAPath, { force: true });
      await rm(questBPath, { force: true });
    },
  );

  // Cleanup
  await rm(currentDir, { recursive: true, force: true });
  await rm(archiveDir, { recursive: true, force: true });
});
