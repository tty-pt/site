import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import plugin, {
  canImplement,
  classifyBashCommand,
  classifyInvestigationKind,
  classifyToolCall,
  getState,
  hasSufficientInvestigation,
  QuestErrorCode,
  recordObservedInvestigation,
  startResearchEpoch,
  type StoredState,
  triggerReassessment,
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
  };

  return {
    mockPi,
    handlers,
    registeredTools,
    registeredCommands,
    agentMessages,
    userMessages,
    appendedEntries,
  };
}

function createMockContext(
  tokens = 50000,
  sessionId = `session_${Math.random().toString(36).slice(2)}`,
) {
  const branch: any[] = [];
  return {
    mode: "agent",
    hasUI: true,
    sessionManager: {
      id: sessionId,
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
      select: async () => "",
    },
  };
}

Deno.test("quest_journal_observed_investigation_evidence: strict epoch-bound investigation requirement for research & reassessment gates", async (t) => {
  const QUEST_DIR = ".pi/quest/current";
  await mkdir(QUEST_DIR, { recursive: true });

  const getAllMessages = (api: ReturnType<typeof createMockExtensionAPI>) => {
    return [
      ...api.agentMessages.map((m) => String(m.msg)),
      ...api.userMessages.map((m) => String(m.msg)),
    ];
  };

  // -----------------------------------------------------------------------
  // 1. tool_call for a read does not create research evidence by itself
  // -----------------------------------------------------------------------
  await t.step(
    "1. tool_call for a read does not create research evidence by itself",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_1");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-tool-call-no-evidence";
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      // Emit tool_call event for read
      for (const cb of api.handlers["tool_call"] || []) {
        await cb(
          { toolName: "read", input: { path: "mods/song/player.c" } },
          ctx,
        );
      }

      assert.strictEqual(
        state.currentReceipt?.evidenceCount || 0,
        0,
        "tool_call must NOT increment evidence count",
      );
      assert.strictEqual(
        state.currentReceipt?.toolCalls || 0,
        0,
        "tool_call must NOT increment toolCalls",
      );

      // Attempting researchComplete must be refused because tool_call alone is not completed evidence
      const res = await tools["quest_update_state"].execute(
        "call_update",
        {
          name: slug,
          status: "Research complete",
          understanding: "Verified player.c",
          assumptions: ["Assumption 1"],
          openQuestions: ["None"],
          findings: ["Findings 1"],
          plan: ["1. Step 1"],
          planConfidence: "high",
          nextAction: "Step 1",
          researchComplete: true,
        },
        {},
        () => {},
        ctx,
      );

      assert.ok(
        res.content[0].text.includes("researchComplete refused"),
        "researchComplete must be refused",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 2. Failed read does not create research evidence
  // -----------------------------------------------------------------------
  await t.step("2. failed read does not create research evidence", async () => {
    const api = createMockExtensionAPI();
    plugin(api.mockPi as any);

    const ctx = createMockContext(10000, "session_test_2");
    const commands: Record<string, any> = {};
    for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

    const slug = "test-failed-read";
    await commands["quest"].handler(slug, ctx);
    const state = getState(ctx as any);

    // Emit tool_result with isError = true
    for (const cb of api.handlers["tool_result"] || []) {
      await cb({
        toolName: "read",
        input: { path: "nonexistent.c" },
        isError: true,
        error: "File not found",
      }, ctx);
    }

    assert.strictEqual(
      state.currentReceipt?.evidenceCount || 0,
      0,
      "Failed tool_result must NOT increment evidenceCount",
    );
    assert.strictEqual(
      state.currentReceipt?.toolCalls || 0,
      0,
      "Failed tool_result must NOT increment toolCalls",
    );
  });

  // -----------------------------------------------------------------------
  // 3. Successful read creates exactly one evidence record
  // -----------------------------------------------------------------------
  await t.step(
    "3. successful read creates exactly one evidence record",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_3");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

      const slug = "test-successful-read";
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      // Emit single successful tool_result
      for (const cb of api.handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/player.c" },
          output: "int main() {}",
          isError: false,
        }, ctx);
      }

      assert.strictEqual(
        state.currentReceipt?.evidenceCount,
        1,
        "Successful read must increment evidenceCount to 1",
      );
      assert.strictEqual(
        state.currentReceipt?.toolCalls,
        1,
        "Successful read must increment toolCalls to 1",
      );
      assert.deepStrictEqual(state.currentReceipt?.readTargets, [
        "mods/song/player.c",
      ], "readTargets must contain target path");
    },
  );

  // -----------------------------------------------------------------------
  // 4. Tool appearing in both tool_call and tool_result is counted once
  // -----------------------------------------------------------------------
  await t.step(
    "4. a tool appearing in both tool_call and tool_result is counted once",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_4");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

      const slug = "test-call-and-result";
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      // 1. First tool_call event fires
      for (const cb of api.handlers["tool_call"] || []) {
        await cb(
          { toolName: "read", input: { path: "mods/song/player.c" } },
          ctx,
        );
      }
      // 2. Then tool_result event fires
      for (const cb of api.handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/player.c" },
          output: "int player();",
          isError: false,
        }, ctx);
      }

      assert.strictEqual(
        state.currentReceipt?.evidenceCount,
        1,
        "Tool call + tool result lifecycle must record exactly 1 evidence",
      );
      assert.strictEqual(
        state.currentReceipt?.toolCalls,
        1,
        "Tool call + tool result lifecycle must record exactly 1 toolCall",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 5. git status does not satisfy substantive research
  // -----------------------------------------------------------------------
  await t.step(
    "5. git status does not satisfy substantive research",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_5");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-git-status-research";
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      // Emit successful git status
      for (const cb of api.handlers["tool_result"] || []) {
        await cb({
          toolName: "bash",
          input: { command: "git status" },
          output: "On branch main\nnothing to commit",
          isError: false,
        }, ctx);
      }

      assert.strictEqual(
        state.currentReceipt?.evidenceCount || 0,
        0,
        "git status must NOT count as investigation evidence",
      );

      // Attempting researchComplete must be refused
      const res = await tools["quest_update_state"].execute(
        "call_update",
        {
          name: slug,
          status: "Research complete",
          understanding: "Understanding",
          assumptions: ["Assumption"],
          openQuestions: ["None"],
          findings: ["Findings"],
          plan: ["1. Plan"],
          planConfidence: "high",
          nextAction: "Action",
          researchComplete: true,
        },
        {},
        () => {},
        ctx,
      );

      assert.ok(
        res.content[0].text.includes("researchComplete refused"),
        "researchComplete must be refused with only git status",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 6. pwd does not satisfy substantive research
  // -----------------------------------------------------------------------
  await t.step("6. pwd does not satisfy substantive research", async () => {
    const api = createMockExtensionAPI();
    plugin(api.mockPi as any);

    const ctx = createMockContext(10000, "session_test_6");
    const commands: Record<string, any> = {};
    for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
    const tools: Record<string, any> = {};
    for (const tool of api.registeredTools) tools[tool.name] = tool;

    const slug = "test-pwd-research";
    await commands["quest"].handler(slug, ctx);
    const state = getState(ctx as any);

    // Emit successful pwd and whoami
    for (const cb of api.handlers["tool_result"] || []) {
      await cb({
        toolName: "bash",
        input: { command: "pwd" },
        output: "/home/user/site",
        isError: false,
      }, ctx);
      await cb({
        toolName: "bash",
        input: { command: "whoami" },
        output: "user",
        isError: false,
      }, ctx);
    }

    assert.strictEqual(
      state.currentReceipt?.evidenceCount || 0,
      0,
      "pwd and whoami must NOT count as investigation evidence",
    );

    const check = hasSufficientInvestigation(state, "research");
    assert.strictEqual(
      check.sufficient,
      false,
      "Investigation must not be sufficient with only pwd",
    );
  });

  // -----------------------------------------------------------------------
  // 7. npm test / make test does not satisfy research by itself
  // -----------------------------------------------------------------------
  await t.step(
    "7. npm test / make test does not satisfy research by itself",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_7");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

      const slug = "test-build-test-research";
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      for (const cb of api.handlers["tool_result"] || []) {
        await cb({
          toolName: "bash",
          input: { command: "npm test" },
          output: "PASS: all tests",
          isError: false,
        }, ctx);
        await cb({
          toolName: "bash",
          input: { command: "make test" },
          output: "All tests passed",
          isError: false,
        }, ctx);
      }

      assert.strictEqual(
        state.currentReceipt?.evidenceCount || 0,
        0,
        "npm test and make test must NOT count as investigation evidence",
      );
      const check = hasSufficientInvestigation(state, "research");
      assert.strictEqual(
        check.sufficient,
        false,
        "Test execution alone cannot satisfy research gate",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 8. A genuine source read/search does satisfy the minimum evidence requirement
  // -----------------------------------------------------------------------
  await t.step(
    "8. a genuine source read/search does satisfy the minimum evidence requirement",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_8");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
      const tools: Record<string, any> = {};
      for (const tool of api.registeredTools) tools[tool.name] = tool;

      const slug = "test-genuine-research";
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      // Execute genuine search and read
      for (const cb of api.handlers["tool_result"] || []) {
        await cb({
          toolName: "bash",
          input: { command: "rg 'player_init' mods/song" },
          output: "mods/song/player.c: void player_init();",
          isError: false,
        }, ctx);
      }

      assert.strictEqual(
        state.currentReceipt?.evidenceCount,
        1,
        "rg must count as investigation evidence",
      );
      const check = hasSufficientInvestigation(state, "research");
      assert.strictEqual(
        check.sufficient,
        true,
        "Genuine search must satisfy research evidence check",
      );

      // quest_update_state researchComplete: true must succeed
      const res = await tools["quest_update_state"].execute(
        "call_update",
        {
          name: slug,
          status: "Research complete",
          understanding: "Verified player_init in player.c",
          assumptions: ["Player init is called once"],
          openQuestions: ["None"],
          findings: ["player_init initializes audio stream buffer"],
          plan: ["1. Modify player_init", "2. Test playback"],
          planConfidence: "high",
          nextAction: "Modify player_init",
          researchComplete: true,
        },
        {},
        () => {},
        ctx,
      );

      assert.ok(
        !res.content[0].text.includes("refused"),
        "researchComplete must succeed with genuine evidence",
      );
      assert.strictEqual(
        state.researchComplete,
        true,
        "researchComplete state must be true",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 9. bash and user_bash behave identically
  // -----------------------------------------------------------------------
  await t.step("9. bash and user_bash behave identically", async () => {
    // Permissions
    assert.strictEqual(
      classifyToolCall("bash", { command: "rg 'flac' mods" }),
      "read",
    );
    assert.strictEqual(
      classifyToolCall("user_bash", { command: "rg 'flac' mods" }),
      "read",
    );
    assert.strictEqual(
      classifyToolCall("bash", { command: "make test" }),
      "implementation",
    );
    assert.strictEqual(
      classifyToolCall("user_bash", { command: "make test" }),
      "implementation",
    );
    assert.strictEqual(
      classifyToolCall("bash", { command: "sed -i 's/a/b/' file.c" }),
      "implementation",
    );
    assert.strictEqual(
      classifyToolCall("user_bash", { command: "sed -i 's/a/b/' file.c" }),
      "implementation",
    );

    // Investigation value
    assert.strictEqual(
      classifyInvestigationKind("bash", { command: "rg 'flac' mods" }).kind,
      "code-search",
    );
    assert.strictEqual(
      classifyInvestigationKind("user_bash", { command: "rg 'flac' mods" })
        .kind,
      "code-search",
    );
    assert.strictEqual(
      classifyInvestigationKind("bash", { command: "git status" }).kind,
      "none",
    );
    assert.strictEqual(
      classifyInvestigationKind("user_bash", { command: "git status" }).kind,
      "none",
    );

    // Evidence recording via tool_result
    const api = createMockExtensionAPI();
    plugin(api.mockPi as any);
    const ctx = createMockContext(10000, "session_test_9");
    const commands: Record<string, any> = {};
    for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

    const slug = "test-user-bash-parity";
    await commands["quest"].handler(slug, ctx);
    const state = getState(ctx as any);

    for (const cb of api.handlers["tool_result"] || []) {
      await cb({
        toolName: "user_bash",
        input: { command: "cat mods/song/player.c" },
        output: "code",
        isError: false,
      }, ctx);
    }
    assert.strictEqual(
      state.currentReceipt?.evidenceCount,
      1,
      "user_bash cat must increment evidenceCount",
    );
    assert.deepStrictEqual(state.currentReceipt?.readTargets, [
      "mods/song/player.c",
    ], "user_bash target recorded");
  });

  // -----------------------------------------------------------------------
  // 10. Old evidence cannot satisfy a new research epoch
  // -----------------------------------------------------------------------
  await t.step(
    "10. old evidence cannot satisfy a new research epoch",
    async () => {
      const targetState: StoredState = {
        active: "test-quest-epoch",
        saveCount: 1,
        compactCount: 0,
        prompts: ["Test prompt"],
        stack: ["test-quest-epoch"],
        researchRound: 1,
        investigationEpoch: 1,
        currentReceipt: {
          epoch: 1,
          epochType: "research",
          startedAt: Date.now() - 5000,
          toolCalls: 3,
          readTargets: ["mods/song/player.c"],
          searchTargets: ["search_code:player"],
          commands: ["rg player mods/song"],
          evidenceCount: 3,
        },
      };

      assert.strictEqual(
        hasSufficientInvestigation(targetState, "research").sufficient,
        true,
      );

      // Advance to Research Round 2 (which starts a new epoch)
      targetState.researchRound = 2;
      startResearchEpoch(targetState, "research");

      assert.strictEqual(targetState.investigationEpoch, 2);
      assert.strictEqual(targetState.currentReceipt?.evidenceCount, 0);
      assert.strictEqual(
        hasSufficientInvestigation(targetState, "research").sufficient,
        false,
        "Epoch 1 evidence cannot satisfy Epoch 2",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 11. Old evidence cannot satisfy a new reassessment epoch
  // -----------------------------------------------------------------------
  await t.step(
    "11. old evidence cannot satisfy a new reassessment epoch",
    async () => {
      const targetState: StoredState = {
        active: "test-quest-reassess-epoch",
        saveCount: 1,
        compactCount: 0,
        prompts: ["Test prompt"],
        stack: ["test-quest-reassess-epoch"],
        researchRound: 1,
        researchComplete: true,
        investigationEpoch: 1,
        currentReceipt: {
          epoch: 1,
          epochType: "research",
          startedAt: Date.now() - 5000,
          toolCalls: 2,
          readTargets: ["mods/song/player.c"],
          searchTargets: [],
          commands: ["rg player mods/song"],
          evidenceCount: 2,
        },
      };

      // Trigger reassessment due to test failure
      triggerReassessment(targetState, "Test failure in ring_buffer_test.c");

      assert.strictEqual(targetState.reassessmentRequired, true);
      assert.strictEqual(
        targetState.investigationEpoch,
        2,
        "triggerReassessment must advance investigation epoch",
      );
      assert.strictEqual(targetState.currentReceipt?.epoch, 2);
      assert.strictEqual(targetState.currentReceipt?.epochType, "reassessment");
      assert.strictEqual(
        targetState.currentReceipt?.evidenceCount,
        0,
        "Fresh reassessment epoch must start with 0 evidence",
      );

      const check = hasSufficientInvestigation(targetState, "reassessment");
      assert.strictEqual(
        check.sufficient,
        false,
        "Prior research evidence cannot satisfy new reassessment epoch",
      );

      // Record fresh investigation in epoch 2
      recordObservedInvestigation(
        targetState,
        "read",
        { path: "tests/unit/ring_buffer_test.c" },
        "test code",
        false,
      );
      assert.strictEqual(targetState.currentReceipt?.evidenceCount, 1);
      assert.strictEqual(
        hasSufficientInvestigation(targetState, "reassessment").sufficient,
        true,
        "Fresh investigation in epoch 2 satisfies reassessment",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 12. Reconstructed historical quest state does not masquerade as newly observed investigation
  // -----------------------------------------------------------------------
  await t.step(
    "12. reconstructed historical quest state does not masquerade as newly observed investigation",
    async () => {
      const api = createMockExtensionAPI();
      plugin(api.mockPi as any);

      const ctx = createMockContext(10000, "session_test_12");
      const commands: Record<string, any> = {};
      for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;

      const slug = "test-historical-honesty";
      const qid = "test-historical-qid";
      await mkdir(`.pi/quest/current/${qid}`, { recursive: true });
      const p = `.pi/quest/current/${qid}/quest.md`;
      await writeFile(
        p,
        `# Quest: ${slug}

## Goal
Implement historical test

## Current Status
- [x] Research complete

## Current Understanding
- Established historical understanding

## Key Assumptions
- [x] Assumption 1

## Open Questions & Uncertainties
- None

## Research Findings
- Historical finding 1

## Plan
1. Step 1

## Plan Confidence
high

## Plan Revisions
- Initial plan formulated.

## Execution Snapshot
### Completed
- Initial setup

### Files Modified
- none

### Test / Build Status
- Tests clean

### Remaining Work
- [ ] Task 1

### Exact Next Action
Implement task 1
`,
        "utf8",
      );

      // Switch to existing quest
      await commands["quest"].handler(slug, ctx);
      const state = getState(ctx as any);

      assert.strictEqual(
        state.researchComplete,
        true,
        "Historical quest researchComplete is true",
      );
      assert.strictEqual(
        state.currentReceipt,
        null,
        "currentReceipt must be null when loading historical quest",
      );
      assert.ok(
        state.lastCompletedReceipt?.isHistorical,
        "lastCompletedReceipt must be marked isHistorical",
      );
      assert.strictEqual(
        state.lastCompletedReceipt?.evidenceCount,
        0,
        "Historical receipt must NOT invent fake evidence count",
      );
      assert.deepStrictEqual(
        state.lastCompletedReceipt?.readTargets,
        [],
        "Historical receipt must NOT invent fake readTargets",
      );
    },
  );
});
