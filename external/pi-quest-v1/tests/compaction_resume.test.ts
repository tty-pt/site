import { readdir } from "node:fs/promises";
import {
  assert,
  canImplement,
  createMockContext,
  createMockExtensionAPI,
  createOrGetCompactionTransaction,
  getAllMessages,
  getImplementationBlockReason,
  getState,
  mkdir,
  plugin,
  QuestErrorCode,
  rm,
  snapshotState,
  writeFile,
} from "./compaction_test_helpers.ts";
import { questPath } from "../src/paths.ts";

Deno.test("quest_journal_compaction_resume: resume delivery, retries, deduplication, obligations, and subquest continuations", async (t) => {
  const QUEST_DIR = ".pi/quest/current";
  await mkdir(QUEST_DIR, { recursive: true });

  // -----------------------------------------------------------------------
  // 2. Pending resume cannot resurrect a quest: archived child is not resurrected by pending resume
  // -----------------------------------------------------------------------
  await t.step(
    "2. Pending resume cannot resurrect a quest: archived child is not resurrected by pending resume",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_compaction_no_resurrect");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const parentSlug = "parent-no-resurrect-quest";
      const childSlug = "child-no-resurrect-quest";

      await commands["quest"].handler(parentSlug, ctx);
      const parentPath = questPath(getState(ctx as any).questId);
      await tools["quest_subquest"].execute(
        "call_sub",
        { name: childSlug, switchNow: true },
        {},
        () => {},
        ctx,
      );

      const state = getState(ctx as any);
      // Simulate pending subquest resume
      state.pendingSubquestResume = childSlug;

      // Child archived: parent becomes authoritative active quest
      await tools["quest_archive"].execute(
        "call_arch",
        { questName: childSlug, compact: false },
        {},
        () => {},
        ctx,
      );

      assert.strictEqual(
        state.active,
        parentSlug,
        "Parent must be active after child archiving",
      );

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Simulate post-compaction callback / recovery
      for (const cb of api.handlers["session_before_compact"] || []) {
        await cb({}, ctx);
      }
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Parent remains active; child is NOT resurrected
      assert.strictEqual(
        state.active,
        parentSlug,
        "Parent MUST remain active; child must NOT be resurrected",
      );
      assert.strictEqual(
        state.pendingSubquestResume,
        null,
        "Obsolete pendingSubquestResume must be cleared",
      );

      // 2. Resume directive targets parent quest
      const msgs = getAllMessages(api);
      const resumes = msgs.filter((m) =>
        m.includes("Post-Compaction Autonomous Resumption Directive")
      );
      assert.strictEqual(resumes.length, 1);
      assert.ok(
        resumes[0].includes(parentPath),
        "Resume must target parent quest",
      );

      await rm(parentPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 3. Stale pending target: pending resume for A does not resume A when active quest is B
  // -----------------------------------------------------------------------
  await t.step(
    "3. Stale pending target: pending resume for A does not resume A when active quest is B",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_compaction_stale_target");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const questA = "stale-target-quest-a";
      const questB = "current-target-quest-b";

      await commands["quest"].handler(questA, ctx);
      const pathA = questPath(getState(ctx as any).questId);
      await tools["quest_mark_saved"].execute(
        "call_save_a",
        { name: questA },
        {},
        () => {},
        ctx,
      );

      // Fail delivery during compaction of Quest A so pendingResume is created
      api.mockPi.setThrowOnSend(true);
      for (const cb of api.handlers["session_before_compact"] || []) {
        await cb({}, ctx);
      }
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      const state = getState(ctx as any);
      assert.ok(state.pendingResume);
      assert.strictEqual(state.pendingResume.activeQuest, questA);

      // Authoritative active quest becomes B
      await commands["quest"].handler(questB, ctx);
      const pathB = questPath(getState(ctx as any).questId);
      await tools["quest_mark_saved"].execute(
        "call_save_b",
        { name: questB },
        {},
        () => {},
        ctx,
      );
      assert.strictEqual(state.active, questB);

      // Restore transport and retry
      api.mockPi.setThrowOnSend(false);
      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      for (const cb of api.handlers["turn_end"] || []) {
        await cb({ toolResults: [] }, ctx);
      }

      // ASSERTIONS:
      // 1. Resume is delivered for current active quest B, NOT stale quest A
      const msgs = getAllMessages(api);
      const resumes = msgs.filter((m) =>
        m.includes("Post-Compaction Autonomous Resumption Directive")
      );
      assert.strictEqual(
        resumes.length,
        1,
        "Exactly one resume delivered on retry",
      );
      assert.ok(
        resumes[0].includes(pathB),
        "Resume must target current active Quest B",
      );
      assert.ok(
        !resumes[0].includes(pathA),
        "Resume must NOT target stale Quest A",
      );
      assert.strictEqual(
        state.pendingResume,
        null,
        "pendingResume must be consumed",
      );
      assert.strictEqual(
        state.active,
        questB,
        "state.active must remain Quest B",
      );

      await rm(pathA, { force: true });
      await rm(pathB, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 10. Transport outage & recovery: pendingResume survives and is drained on turn_end
  // -----------------------------------------------------------------------
  await t.step(
    "10. Transport outage & recovery: pendingResume survives and is drained on turn_end",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_compaction_transport_recovery",
      );
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-transport-retry-quest";
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
      for (const cb of api.handlers["session_before_compact"] || []) {
        await cb({}, ctx);
      }

      // Disable all message transport
      api.mockPi.setThrowOnSend(true);

      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // Obligation is preserved in pendingResume
      assert.ok(state.pendingResume);
      assert.strictEqual(state.pendingResume.activeQuest, slug);
      assert.strictEqual(state.activeTransaction?.phase, "resume-pending");

      // Restore transport
      api.mockPi.setThrowOnSend(false);
      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      for (const cb of api.handlers["turn_end"] || []) {
        await cb({ toolResults: [] }, ctx);
      }

      assert.strictEqual(
        state.pendingResume,
        null,
        "pendingResume must be consumed",
      );
      assert.strictEqual(state.activeTransaction?.phase, "resume-delivered");
      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) =>
          m.includes("Post-Compaction Autonomous Resumption Directive")
        ),
      );

      await rm(p, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 14. Subquest launch compaction preserves pendingSubquestResume on transport error
  // -----------------------------------------------------------------------
  await t.step(
    "14. Subquest launch compaction preserves pendingSubquestResume on transport error",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_external_compaction_pending_subquest",
      );
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const parentSlug = "test-external-parent";
      const childSlug = "test-external-child";

      await commands["quest"].handler(parentSlug, ctx);
      const parentPath = questPath(getState(ctx as any).questId);
      await tools["quest_subquest"].execute(
        "call_sub",
        { name: childSlug, switchNow: true },
        {},
        () => {},
        ctx,
      );
      const childPath = questPath(getState(ctx as any).questId);

      const state = getState(ctx as any);
      state.pendingSubquestResume = childSlug;
      state.active = childSlug;
      state.subquestLaunchCompactionPending = true;

      // 1. Fire subquest launch compaction when transport fails
      api.mockPi.setThrowOnSend(true);
      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      for (const cb of api.handlers["session_before_compact"] || []) {
        await cb({}, ctx);
      }
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTION: pendingSubquestResume was NOT silently deleted on subquest launch compaction
      assert.strictEqual(
        state.pendingSubquestResume,
        childSlug,
        "pendingSubquestResume must NOT be silently deleted on subquest launch compaction",
      );
      assert.ok(
        state.pendingResume,
        "pendingResume must be recorded for delivery retry",
      );
      assert.strictEqual(state.pendingResume.reason, "subquest-launch");

      // 2. Restore transport and drain on turn_end
      api.mockPi.setThrowOnSend(false);
      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      for (const cb of api.handlers["turn_end"] || []) {
        await cb({ toolResults: [] }, ctx);
      }

      // ASSERTION: Delivered subquest directive and consumed obligation
      assert.strictEqual(
        state.pendingSubquestResume,
        null,
        "pendingSubquestResume must be consumed after successful delivery",
      );
      assert.strictEqual(
        state.pendingResume,
        null,
        "pendingResume must be consumed after delivery",
      );

      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) =>
          m.includes(
            `Compaction finished after launching sub-quest \`${childSlug}\``,
          )
        ),
        "Must deliver subquest launch post-compaction directive",
      );

      await rm(parentPath, { force: true });
      await rm(childPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 16. Pending resume does not resurrect stale target when authoritative active quest is null
  // -----------------------------------------------------------------------
  await t.step(
    "16. Pending resume does not resurrect stale target when authoritative active quest is null",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_no_resurrect_null_active");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slugA = "test-stale-obligation-a";
      await commands["quest"].handler(slugA, ctx);
      const pathA = questPath(getState(ctx as any).questId);
      await tools["quest_mark_saved"].execute(
        "call_save",
        { name: slugA },
        {},
        () => {},
        ctx,
      );

      const state = getState(ctx as any);

      // Record a pending resume for Quest A
      state.pendingResume = {
        compactionId: "cmp_stale_123",
        activeQuest: slugA,
        reason: "normal-compaction",
        checkpointSaveCount: 1,
        checkpointHash: "hash_stale_123",
        checkpointQuestPath: pathA,
        attempts: 1,
        createdAt: Date.now(),
      };

      // Current authoritative active quest becomes null (e.g. idle session or cleared state)
      state.active = null;

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Trigger turn_end retry
      for (const cb of api.handlers["turn_end"] || []) {
        await cb({ toolResults: [] }, ctx);
      }

      // ASSERTION: Quest A must NOT be resurrected as state.active!
      assert.strictEqual(
        state.active,
        null,
        "state.active must remain null; pendingResume must not resurrect quest A",
      );

      const msgs = getAllMessages(api);
      assert.strictEqual(
        msgs.filter((m) =>
          m.includes("Post-Compaction Autonomous Resumption Directive")
        ).length,
        0,
        "Must NOT deliver normal post-compaction resume for stale quest",
      );

      await rm(pathA, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 17. Pending resume obligation is preserved across subsequent compaction attempts until delivered and consumed
  // -----------------------------------------------------------------------
  await t.step(
    "17. Pending resume obligation is preserved across subsequent compaction attempts until delivered and consumed",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_preserve_pending_resume_across_compactions",
      );
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-compaction-preserve-resume";
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

      // 1. Enter compaction A
      for (const cb of api.handlers["session_before_compact"] || []) {
        await cb({}, ctx);
      }
      const txA = state.activeTransaction;
      assert.ok(txA, "Transaction A must exist");
      const idA = txA.id;

      // Simulate message delivery failure during compaction A completion
      api.mockPi.setThrowOnSend(true);
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTION: Compaction A succeeded, but resume delivery failed
      assert.ok(
        state.pendingResume,
        "pendingResume must exist for transaction A",
      );
      assert.strictEqual(
        state.pendingResume.compactionId,
        idA,
        "pendingResume must point to transaction A",
      );
      assert.strictEqual(
        state.activeTransaction?.id,
        idA,
        "activeTransaction must be transaction A",
      );
      assert.strictEqual(
        state.activeTransaction?.phase,
        "resume-pending",
        "activeTransaction must be in resume-pending phase",
      );

      // 2. Before A is delivered, attempt to start compaction B
      for (const cb of api.handlers["session_before_compact"] || []) {
        const res = await cb({}, ctx);
        if (res && res.cancel) {
          // Canceled as expected while obligation is pending
        }
      }

      // ASSERTION: Transaction A's pending obligation must NOT be overwritten silently by B
      assert.ok(state.pendingResume, "pendingResume A must still exist");
      assert.strictEqual(
        state.pendingResume.compactionId,
        idA,
        "pendingResume must still point to transaction A",
      );
      assert.strictEqual(
        state.activeTransaction?.id,
        idA,
        "activeTransaction must still be transaction A",
      );

      // 3. Restore message delivery
      api.mockPi.setThrowOnSend(false);
      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // 4. Trigger turn_end to retry delivery of A
      for (const cb of api.handlers["turn_end"] || []) {
        await cb({ toolResults: [] }, ctx);
      }

      // ASSERTION: Resume A was delivered and consumed
      assert.strictEqual(
        state.pendingResume,
        null,
        "pendingResume A must be consumed after successful delivery",
      );
      assert.ok(
        !state.activeTransaction ||
          (state.activeTransaction as any).phase === "resume-delivered",
        "Transaction A must be resolved or cleared",
      );

      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) =>
          m.includes("Post-Compaction Autonomous Resumption Directive")
        ),
        "Must have delivered post-compaction resume for A",
      );

      // 5. Mark saved so compactionReady() is true for subsequent compaction B
      await tools["quest_mark_saved"].execute(
        "call_save_b",
        { name: slug },
        {},
        () => {},
        ctx,
      );

      // Now that A is resolved and saved, a subsequent compaction B is permitted
      for (const cb of api.handlers["session_before_compact"] || []) {
        await cb({}, ctx);
      }
      const txB = state.activeTransaction;
      assert.ok(txB, "Transaction B must be created");
      assert.notStrictEqual(txB.id, idA, "Transaction B must have a new ID");
      assert.strictEqual(
        txB.phase,
        "in-flight",
        "Transaction B must be in-flight",
      );

      // Complete compaction B
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }
      assert.strictEqual(
        state.pendingResume,
        null,
        "Transaction B delivered immediately",
      );

      await rm(p, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 18. Pending agent notifications and resume obligations are not starved by compactionPending
  // -----------------------------------------------------------------------
  await t.step(
    "18. Pending agent notifications and resume obligations are not starved by compactionPending",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_notification_unstarved");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-compaction-unstarved";
      const p = `${QUEST_DIR}/${slug}.md`;
      await writeFile(
        p,
        `# Quest: ${slug}\n\n## Goal\nTest unstarved notifications\n`,
        "utf8",
      );

      await commands["quest"].handler(slug, ctx);
      await tools["quest_mark_saved"].execute(
        "call_save",
        { name: slug },
        {},
        () => {},
        ctx,
      );

      const state = getState(ctx as any);

      // Queue a pending notification and set compactionPending = true
      state.compactionPending = true;
      state.pendingNotifications = [
        {
          id: "notif_test_1",
          code: QuestErrorCode.CHECKPOINT_REQUIRED,
          message: "[Quest Journal] Test pending agent notification",
          deliverAs: "followUp",
          attempts: 0,
          createdAt: Date.now(),
        },
      ];

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Trigger turn_end while compactionPending is true
      for (const cb of api.handlers["turn_end"] || []) {
        await cb({ toolResults: [] }, ctx);
      }

      // ASSERTION: Notification must have been delivered despite compactionPending being true
      assert.strictEqual(
        state.pendingNotifications?.[0]?.status,
        "delivering",
        "Pending notification status must transition to delivering when drained",
      );

      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) => m.includes("Test pending agent notification")),
        "Notification message must be delivered to the agent",
      );

      await rm(p, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 19. Pending resume reconstruction preserves original checkpoint hash/count/path without borrowing live state
  // -----------------------------------------------------------------------
  await t.step(
    "19. Regression Test A & H: Pending resume reconstruction preserves original checkpoint hash/count/path without borrowing live state",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_pending_recon_immutability",
      );
      const state = getState(ctx as any);
      const slug = "test-pending-recon-quest";
      state.active = slug;
      state.saveCount = 42;
      state.saveGeneration = {
        count: 42,
        hash: "current_live_hash_999",
        path: `.pi/quest/current/${slug}.md`,
        savedAt: Date.now(),
      };

      state.pendingResume = {
        compactionId: "cmp_historical_123",
        activeQuest: slug,
        reason: "normal-compaction",
        checkpointSaveCount: 7,
        checkpointHash: "historical_checkpoint_hash_111",
        checkpointQuestPath: `.pi/quest/current/${slug}.md`,
        attempts: 1,
        createdAt: 100000,
      };

      // Mutate live saveCount and hash before rebuilding transaction
      state.saveCount = 50;
      state.saveGeneration = {
        count: 50,
        hash: "new_mutated_live_hash",
        path: `.pi/quest/current/${slug}.md`,
        savedAt: Date.now(),
      };

      const tx = createOrGetCompactionTransaction(state);
      assert.strictEqual(
        tx.checkpointSaveCount,
        7,
        "Must preserve historical checkpointSaveCount (7), NOT live count (50)",
      );
      assert.strictEqual(
        tx.checkpointHash,
        "historical_checkpoint_hash_111",
        "Must preserve historical checkpointHash, NOT live hash",
      );
      assert.strictEqual(tx.questPath, `.pi/quest/current/${slug}.md`);
    },
  );

  // -----------------------------------------------------------------------
  // 20. Current save/hash changing later does not rewrite pending resume checkpoint identity
  // -----------------------------------------------------------------------
  await t.step(
    "20. Regression Test B: Current save/hash changing later does not rewrite pending resume checkpoint identity",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_pending_immutability_b");
      const state = getState(ctx as any);
      const slug = "test-pending-b-quest";
      state.active = slug;

      state.pendingResume = {
        compactionId: "cmp_b_456",
        activeQuest: slug,
        reason: "normal-compaction",
        checkpointSaveCount: 5,
        checkpointHash: "hash_b_555",
        checkpointQuestPath: `.pi/quest/current/${slug}.md`,
        attempts: 0,
        createdAt: Date.now(),
      };

      // Mutate state multiple times
      state.saveCount = 20;
      state.lastSavedHash = "hash_mutated_20";
      state.saveGeneration = {
        count: 20,
        hash: "hash_mutated_20",
        path: `.pi/quest/current/${slug}.md`,
        savedAt: Date.now(),
      };

      const snapshot = snapshotState(ctx as any);
      assert.strictEqual(
        snapshot.pendingResume?.checkpointSaveCount,
        5,
        "Snapshot must preserve immutable checkpointSaveCount",
      );
      assert.strictEqual(
        snapshot.pendingResume?.checkpointHash,
        "hash_b_555",
        "Snapshot must preserve immutable checkpointHash",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 21. Pending child A + parent B active does NOT clear A; emits PENDING_RESUME_INCONSISTENT and blocks implementation
  // -----------------------------------------------------------------------
  await t.step(
    "21. Regression Test C & E: Pending child A + parent B active does NOT clear A; emits PENDING_RESUME_INCONSISTENT and blocks implementation",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(
        10000,
        "session_pending_child_inconsistent",
      );
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const parentSlug = "parent-c-quest";
      const childSlug = "child-c-quest";

      await commands["quest"].handler(parentSlug, ctx);
      const parentPath = questPath(getState(ctx as any).questId);
      const state = getState(ctx as any);
      state.pendingSubquestResume = childSlug;

      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Fire compaction
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Pending subquest resume A is NOT silently deleted
      assert.strictEqual(
        state.pendingSubquestResume,
        childSlug,
        "Must NOT silently delete pendingSubquestResume when child is not archived",
      );

      // 2. PENDING_RESUME_INCONSISTENT error is emitted to agent
      const msgs = getAllMessages(api);
      assert.ok(
        msgs.some((m) => m.includes("PENDING_RESUME_INCONSISTENT")),
        "Must emit PENDING_RESUME_INCONSISTENT error",
      );

      // 3. Implementation is blocked
      assert.strictEqual(
        canImplement(state, ctx as any),
        false,
        "Implementation must be blocked on inconsistent pending subquest resume",
      );
      const blockReason = getImplementationBlockReason(state, ctx as any);
      assert.strictEqual(
        blockReason.code,
        QuestErrorCode.PENDING_RESUME_INCONSISTENT,
      );

      await rm(parentPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 22. Pending child A + A demonstrably archived -> explicit obsolete resolution -> persisted resolution -> no resurrection
  // -----------------------------------------------------------------------
  await t.step(
    "22. Regression Test D: Pending child A + A demonstrably archived -> explicit obsolete resolution -> persisted resolution -> no resurrection",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_pending_child_obsolete");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const parentSlug = "parent-d-quest";
      const childSlug = "child-d-quest";

      await commands["quest"].handler(parentSlug, ctx);
      const parentPath = questPath(getState(ctx as any).questId);
      await tools["quest_subquest"].execute(
        "call_sub_d",
        { name: childSlug, switchNow: true },
        {},
        () => {},
        ctx,
      );

      const state = getState(ctx as any);
      state.pendingSubquestResume = childSlug;

      // Archive child subquest
      await tools["quest_archive"].execute(
        "call_arch_d",
        { questName: childSlug, compact: false },
        {},
        () => {},
        ctx,
      );

      assert.strictEqual(state.active, parentSlug);

      // Now child does NOT exist in .pi/quest/current/
      api.agentMessages.length = 0;
      api.userMessages.length = 0;

      // Fire compaction
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Explicit obsolete resolution recorded and persisted
      assert.strictEqual(
        state.pendingSubquestResume,
        null,
        "pendingSubquestResume cleared after obsolete resolution",
      );
      assert.ok(
        state.pendingSubquestResumeResolution,
        "Resolution must be persisted",
      );
      assert.strictEqual(
        state.pendingSubquestResumeResolution.child,
        childSlug,
      );
      assert.strictEqual(
        state.pendingSubquestResumeResolution.resolution,
        "obsolete-after-archive",
      );
      assert.strictEqual(
        state.pendingSubquestResumeResolution.parent,
        parentSlug,
      );

      // 2. Child is NOT resurrected; parent remains active
      assert.strictEqual(
        state.active,
        parentSlug,
        "Child must NOT be resurrected",
      );

      await rm(parentPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 26. pendingSubquestResume not consumed on missing child file without positive archive evidence
  // -----------------------------------------------------------------------
  await t.step(
    "26. Regression Test: pendingSubquestResume not consumed on missing child file without positive archive evidence",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_subquest_archive_evidence");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const parentSlug = "test-parent-evidence-quest";
      const childSlug = "test-child-evidence-quest";

      await commands["quest"].handler(parentSlug, ctx);
      const parentPath = questPath(getState(ctx as any).questId);
      const state = getState(ctx as any);
      state.pendingSubquestResume = childSlug;

      // Fire compaction
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS:
      // 1. Missing file alone is NOT proof of archive; obligation MUST remain pending
      assert.strictEqual(
        state.pendingSubquestResume,
        childSlug,
        "Obligation must remain pending without archive evidence",
      );
      assert.strictEqual(
        state.pendingSubquestResumeResolution,
        null,
        "Must NOT record obsolete-after-archive without evidence",
      );

      // 2. Implementation gate remains blocked with PENDING_RESUME_INCONSISTENT
      assert.strictEqual(
        canImplement(state, ctx as any),
        false,
        "Implementation gate must be blocked",
      );
      const reason = getImplementationBlockReason(state, ctx as any);
      assert.strictEqual(reason.blocked, true);
      assert.strictEqual(
        reason.code,
        QuestErrorCode.PENDING_RESUME_INCONSISTENT,
      );

      // Now provide positive archive evidence: create archive file in .pi/quest/archive
      const ARCHIVE_DIR = ".pi/quest/archive";
      await mkdir(ARCHIVE_DIR, { recursive: true });
      const archiveDest = `${ARCHIVE_DIR}/${state.questId}.zip`;
      await writeFile(archiveDest, `zip content`, "utf8");

      // Fire compaction again
      for (const cb of api.handlers["session_compact"] || []) {
        await cb({}, ctx);
      }

      // ASSERTIONS with positive evidence:
      // 1. Obligation is now reconciled as obsolete-after-archive
      assert.strictEqual(
        state.pendingSubquestResume,
        null,
        "pendingSubquestResume cleared with positive archive evidence",
      );
      assert.ok(
        state.pendingSubquestResumeResolution,
        "Resolution must be persisted",
      );
      assert.strictEqual(
        (state.pendingSubquestResumeResolution as any).child,
        childSlug,
      );
      assert.strictEqual(
        (state.pendingSubquestResumeResolution as any).resolution,
        "obsolete-after-archive",
      );

      await rm(parentPath, { force: true });
      await rm(archiveDest, { force: true });
    },
  );
});
