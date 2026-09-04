import {
  assert,
  canImplement,
  createMockContext,
  createMockExtensionAPI,
  getAllMessages,
  getImplementationBlockReason,
  getState,
  mkdir,
  plugin,
  QuestErrorCode,
  recordObservedInvestigation,
  rm,
  writeFile,
} from "./compaction_test_helpers.ts";
import { questPath } from "../src/paths.ts";

Deno.test("quest_journal_compaction_external: external compaction handling, out-of-band events, race isolation, and recovery", async (t) => {
  const QUEST_DIR = ".pi/quest/current";
  await mkdir(QUEST_DIR, { recursive: true });

  // -----------------------------------------------------------------------
  // 4. Unknown external compaction
  // -----------------------------------------------------------------------
  await t.step(
    "4. Unknown external compaction: marks transaction inconsistent, delivers RESUME_STATE_INCONSISTENT, does not emit normal resume, preserves implementation block until explicit reconciliation",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_compaction_unknown");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "external-compaction-quest";
      await commands["quest"].handler(slug, ctx);
      const p = questPath(getState(ctx as any).questId);
      await tools["quest_mark_saved"].execute(
        "call_save",
        { name: slug },
        {},
        () => {},
        ctx,
      );

      const state = getState(ctx as any);
      state.saveCount = 5;
      state.activeTransaction = null;
      state.activeCompactionId = null;

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Fire session_compact without a prepared transaction
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Active transaction enters inconsistent recovery state
      assert.ok(
        state.activeTransaction,
        "Must create activeTransaction in recovery state",
      );
      assert.strictEqual(
        (state.activeTransaction as any).phase,
        "inconsistent",
        "Must enter inconsistent recovery state on unknown external compaction",
      );
      assert.strictEqual(state.reassessmentRequired, true);

      // 2. RESUME_STATE_INCONSISTENT is delivered
      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) => m.includes("RESUME_STATE_INCONSISTENT")),
        "RESUME_STATE_INCONSISTENT error must be reported to agent",
      );

      // 3. NO normal post-compaction resume is delivered
      const resumes = msgs.filter((m) =>
        m.includes("Post-Compaction Autonomous Resumption Directive")
      );
      assert.strictEqual(
        resumes.length,
        0,
        "Normal resume must NOT be emitted for unknown compaction",
      );

      // 4. Implementation remains blocked while the transaction is inconsistent
      assert.strictEqual(
        canImplement(state, ctx as any),
        false,
        "Implementation must be blocked while transaction is inconsistent",
      );
      const blockReason = getImplementationBlockReason(state, ctx as any);
      assert.strictEqual(blockReason.blocked, true);
      assert.strictEqual(
        blockReason.code,
        QuestErrorCode.RESUME_STATE_INCONSISTENT,
      );

      // Tool call gate blocks mutating tools
      let blockedEvent: any = null;
      for (const cb of api.handlers["tool_call"] || []) {
        const res = await cb({
          toolName: "edit",
          input: { path: "some/file.c" },
        }, ctx);
        if (res?.block) blockedEvent = res;
      }
      assert.ok(
        blockedEvent?.block,
        "Tool call gate must block edit while transaction is inconsistent",
      );

      // 6. External compaction does not invent a checkpoint hash/save-count identity
      assert.strictEqual(
        (state.activeTransaction as any).checkpointSaveCount,
        undefined,
        "checkpointSaveCount must not be invented for unknown compaction",
      );
      assert.strictEqual(
        (state.activeTransaction as any).checkpointHash,
        undefined,
        "checkpointHash must not be invented for unknown compaction",
      );
      assert.strictEqual(
        (state.activeTransaction as any).questPath,
        undefined,
        "questPath must not be invented for unknown compaction",
      );
      assert.strictEqual(
        (state.activeTransaction as any).observedSaveCount,
        5,
        "observedSaveCount can be stored for diagnostics",
      );

      // 5. Explicit reconciliation succeeds -> transaction resolves -> appropriate continuation message is delivered
      api.agentMessages.length = 0;
      api.userMessages.length = 0;
      recordObservedInvestigation(state, "read", { path: p }, "content", false);
      await tools["quest_update_state"].execute(
        "call_reconcile_unknown",
        {
          name: slug,
          goal: "External test",
          understanding: "State recovered following external compaction",
          assumptions: ["Assumptions verified"],
          openQuestions: ["None"],
          findings: ["Recovered cleanly"],
          plan: ["Proceed"],
          planConfidence: "high",
          exactNextAction: "Continue",
          reassessmentComplete: true,
          reassessmentConclusion:
            "Investigated external compaction and validated durable state.",
        },
        {},
        () => {},
        ctx,
      );

      assert.strictEqual(
        state.activeTransaction,
        null,
        "Transaction must resolve after explicit reconciliation",
      );
      assert.strictEqual(state.reassessmentRequired, false);
      assert.strictEqual(
        canImplement(state, ctx as any),
        true,
        "Implementation gate must open after explicit reconciliation",
      );

      const postReconcileMsgs = getAllMessages(api);
      assert.ok(
        postReconcileMsgs.some((m) =>
          m.includes("REASSESSMENT RESOLVED — IMPLEMENTATION GATE OPEN")
        ),
        "Appropriate continuation message (steer) must be delivered upon successful reconciliation",
      );

      await rm(p, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 25. Unexpected external compaction enters explicit recovery state
  // -----------------------------------------------------------------------
  await t.step(
    "25. Regression Test: Unexpected external compaction enters recovery state without fake clean success",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_external_compaction_recovery",
      );
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-external-compaction-quest";
      await commands["quest"].handler(slug, ctx);
      const p = questPath(getState(ctx as any).questId);
      await tools["quest_mark_saved"].execute(
        "call_save_init",
        { name: slug },
        {},
        () => {},
        ctx,
      );

      const state = getState(ctx as any);
      state.saveCount = 5;
      state.compactCount = 2;
      state.dirty = true;
      state.activeTransaction = null; // No prepared or in-flight transaction

      // Fire unexpected session_compact directly (no session_before_compact)
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Transaction entered inconsistent recovery state
      assert.ok(
        state.activeTransaction,
        "activeTransaction must be created in recovery state",
      );
      assert.strictEqual(
        (state.activeTransaction as any).phase,
        "inconsistent",
        "Transaction must be marked inconsistent",
      );
      assert.strictEqual(
        state.reassessmentRequired,
        true,
        "reassessmentRequired must be set",
      );

      // 2. Normal clean success bookkeeping was NOT performed
      assert.strictEqual(
        state.compactCount,
        2,
        "compactCount must NOT be advanced to saveCount",
      );
      assert.strictEqual(
        state.dirty,
        true,
        "dirty flag must NOT be reset to false",
      );

      // 3. Implementation gate is blocked
      assert.strictEqual(
        canImplement(state, ctx as any),
        false,
        "Implementation gate must be blocked",
      );
      const reason = getImplementationBlockReason(state, ctx as any);
      assert.strictEqual(reason.blocked, true);
      assert.strictEqual(reason.code, QuestErrorCode.RESUME_STATE_INCONSISTENT);

      // Explicit reconciliation via quest_update_state opens the gate
      recordObservedInvestigation(state, "read", { path: p }, "content", false);
      await tools["quest_update_state"].execute(
        "call_reconcile_external",
        {
          name: slug,
          goal: "Test external compaction",
          understanding: "State recovered following external compaction",
          assumptions: ["Assumptions verified"],
          openQuestions: ["None"],
          findings: ["Recovered cleanly"],
          plan: ["Proceed"],
          planConfidence: "high",
          exactNextAction: "Continue",
          reassessmentComplete: true,
          reassessmentConclusion:
            "Investigated external compaction and validated durable state.",
        },
        {},
        () => {},
        ctx,
      );

      assert.strictEqual(
        state.activeTransaction,
        null,
        "Transaction resolved after explicit reassessment",
      );
      assert.strictEqual(state.reassessmentRequired, false);

      // Marking saved establishes fresh clean checkpoint
      await tools["quest_mark_saved"].execute(
        "call_save_fresh",
        { name: slug },
        {},
        () => {},
        ctx,
      );
      assert.strictEqual(
        canImplement(state, ctx as any),
        true,
        "Implementation gate opens after save",
      );

      await rm(p, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 27. Unexpected external compaction preserves pendingResume
  // -----------------------------------------------------------------------
  await t.step(
    "27. Regression Test: Unexpected external compaction preserves pendingResume and blocks implementation",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_preserve_pending_resume");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slugA = "test-preserve-quest-a";
      await commands["quest"].handler(slugA, ctx);
      const pathA = questPath(getState(ctx as any).questId);
      const state = getState(ctx as any);

      // Establish a pending resume obligation
      state.pendingResume = {
        compactionId: "cmp_preserve_123",
        activeQuest: slugA,
        reason: "normal-compaction",
        checkpointSaveCount: 3,
        checkpointHash: "hash_preserve_333",
        checkpointQuestPath: pathA,
        attempts: 1,
        createdAt: Date.now() - 5000,
      };
      state.activeTransaction = null; // No prepared transaction -> unexpected external compaction

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Fire unexpected external session_compact
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. pendingResume was NOT deleted or overwritten
      assert.ok(
        state.pendingResume,
        "pendingResume must be preserved across unexpected external compaction",
      );
      assert.strictEqual(state.pendingResume.compactionId, "cmp_preserve_123");
      assert.strictEqual(state.pendingResume.activeQuest, slugA);
      assert.strictEqual(state.pendingResume.checkpointSaveCount, 3);
      assert.strictEqual(
        state.pendingResume.checkpointHash,
        "hash_preserve_333",
      );

      // 2. Transaction enters inconsistent recovery state
      assert.ok(state.activeTransaction);
      assert.strictEqual(
        (state.activeTransaction as any).phase,
        "inconsistent",
      );

      // 3. Implementation gate remains blocked
      assert.strictEqual(
        canImplement(state, ctx as any),
        false,
        "Implementation must remain blocked",
      );
      const blockReason = getImplementationBlockReason(state, ctx as any);
      assert.strictEqual(blockReason.blocked, true);
      assert.strictEqual(
        blockReason.code,
        QuestErrorCode.RESUME_STATE_INCONSISTENT,
      );

      // 4. RESUME_STATE_INCONSISTENT error delivered, no normal resume emitted
      const msgs = getAllMessages(api);
      assert.ok(msgs.some((m) => m.includes("RESUME_STATE_INCONSISTENT")));
      const resumes = msgs.filter((m) =>
        m.includes("Post-Compaction Autonomous Resumption Directive")
      );
      assert.strictEqual(
        resumes.length,
        0,
        "No normal resume emitted for unmanaged compaction",
      );

      await rm(pathA, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 28. Unexpected external compaction preserves pendingSubquestResume
  // -----------------------------------------------------------------------
  await t.step(
    "28. Regression Test: Unexpected external compaction preserves pendingSubquestResume",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_preserve_pending_subquest");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const parentSlug = "preserve-parent-quest";
      const childSlug = "preserve-child-quest";

      await commands["quest"].handler(parentSlug, ctx);
      const parentPath = questPath(getState(ctx as any).questId);
      const state = getState(ctx as any);
      state.pendingSubquestResume = childSlug;
      state.active = parentSlug; // Parent is active, child is pending but not yet archived
      state.activeTransaction = null; // Unexpected external compaction

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Fire unexpected external session_compact
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Child obligation remains pending without positive archive evidence
      assert.strictEqual(
        state.pendingSubquestResume,
        childSlug,
        "pendingSubquestResume must be preserved",
      );
      assert.strictEqual(state.pendingSubquestResumeResolution, null);

      // 2. Implementation gate remains blocked with PENDING_RESUME_INCONSISTENT
      assert.strictEqual(canImplement(state, ctx as any), false);
      const blockReason = getImplementationBlockReason(state, ctx as any);
      assert.strictEqual(blockReason.blocked, true);
      assert.strictEqual(
        blockReason.code,
        QuestErrorCode.PENDING_RESUME_INCONSISTENT,
      );

      await rm(parentPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 29. Multi-compaction transaction isolation and pending resume preservation
  // -----------------------------------------------------------------------
  await t.step(
    "29. Regression Test: Multi-compaction race isolates external transactions and preserves resume-pending obligations",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_multi_compaction_isolation",
      );
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-race-isolation-quest";
      await commands["quest"].handler(slug, ctx);
      const p = questPath(getState(ctx as any).questId);
      const state = getState(ctx as any);

      // Prepare first transaction and establish resume-pending state
      const originalTxId = "cmp_orig_tx_111";
      state.activeTransaction = {
        id: originalTxId,
        phase: "resume-pending",
        activeQuest: slug,
        questPath: p,
        reason: "normal-compaction",
        checkpointSaveCount: 2,
        checkpointHash: "hash_orig_111",
        stack: [slug],
        researchRound: 1,
        reassessmentVersion: 0,
        planVersion: 1,
        createdAt: Date.now() - 10000,
      };
      state.activeCompactionId = originalTxId;
      state.pendingResume = {
        compactionId: originalTxId,
        activeQuest: slug,
        reason: "normal-compaction",
        checkpointSaveCount: 2,
        checkpointHash: "hash_orig_111",
        checkpointQuestPath: p,
        attempts: 0,
        createdAt: Date.now() - 10000,
      };

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Fire unexpected external session_compact
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. activeTransaction in phase 'resume-pending' was preserved and NOT overwritten
      assert.ok(state.activeTransaction, "activeTransaction must be preserved");
      assert.strictEqual(
        state.activeTransaction.id,
        originalTxId,
        "Original transaction ID must not be overwritten",
      );
      assert.strictEqual(
        state.activeTransaction.phase,
        "resume-pending",
        "Phase must remain resume-pending",
      );
      assert.strictEqual(
        state.activeTransaction.checkpointHash,
        "hash_orig_111",
      );

      // 2. pendingResume obligation was preserved and NOT overwritten
      assert.ok(state.pendingResume, "pendingResume must be preserved");
      assert.strictEqual(state.pendingResume.compactionId, originalTxId);
      assert.strictEqual(state.pendingResume.checkpointHash, "hash_orig_111");

      // 3. Agent received RESUME_STATE_INCONSISTENT error mentioning an external compaction ID != originalTxId
      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) => m.includes("RESUME_STATE_INCONSISTENT")),
        "Must emit RESUME_STATE_INCONSISTENT error",
      );

      // 4. Implementation gate remains blocked
      assert.strictEqual(
        canImplement(state, ctx as any),
        false,
        "Implementation must remain blocked",
      );

      await rm(p, { force: true });
    },
  );
});
