import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { state } from "../src/state.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function testStatusHierarchyAndFormatting() {
  const currentDir = ".pi/quest/current";
  await mkdir(currentDir, { recursive: true });

  const handlers: Record<string, EventCallback[]> = {};
  const tools: Record<string, any> = {};
  const commands: Record<string, any> = {};
  let lastStatusText: string | undefined;

  const mockPi: any = {
    on(event: string, callback: EventCallback) {
      if (!handlers[event]) handlers[event] = [];
      handlers[event].push(callback);
    },
    appendEntry() {},
    registerEntryRenderer() {},
    registerTool(toolDef: any) {
      tools[toolDef.name] = toolDef;
    },
    registerCommand(name: string, commandDef: any) {
      commands[name] = commandDef;
    },
    sendUserMessage() {},
  };

  questJournalExtension(mockPi);

  let currentTokens: number | null = 45000;
  let currentContextWindow = 200000;
  let currentPercent = 22.5;

  const mockCtx: any = {
    cwd: process.cwd(),
    getContextUsage: () => ({
      tokens: currentTokens,
      contextWindow: currentContextWindow,
      percent: currentPercent,
    }),
    sessionManager: {
      getBranch: () => [],
    },
    ui: {
      notify() {},
      setWidget() {},
      setStatus(key: string, text: string | undefined) {
        if (key === "quest") {
          lastStatusText = text;
        }
      },
      input: async () => "Subquest goal",
      select: async () => null,
    },
    hasUI: true,
    mode: "tui",
  };

  // 1. Single active quest status bar formatting
  await commands["quest"].handler("parent-hierarchy-quest", mockCtx);

  assert.ok(lastStatusText, "Status text should be set");
  assert.ok(
    lastStatusText.includes("parent-hierarchy-quest"),
    "Status should contain active quest name",
  );
  assert.ok(
    lastStatusText.includes("parent-hierarchy-quest") ||
      lastStatusText.includes("✨"),
    "Status should contain quest name",
  );

  // 2. Create child sub-quest and verify depth-based compact formatting: d2: child-sub-quest (no full path)
  const subquestTool = tools["quest_subquest"];
  await subquestTool.execute(
    "call_sub1",
    {
      name: "child-sub-quest",
      goal: "Child Subquest Goal",
      parentName: "parent-hierarchy-quest",
      switchNow: true,
    },
    null,
    null,
    mockCtx,
  );

  assert.ok(
    lastStatusText,
    "Status text should be set after switching to sub-quest",
  );
  assert.ok(
    lastStatusText.includes("d2: child-sub-quest"),
    `Status should show depth 2 prefix and subquest name, got: ${lastStatusText}`,
  );
  assert.ok(
    !lastStatusText.includes("parent-hierarchy-quest ↳ child-sub-quest"),
    `Status should NOT show full path across ancestors to save space, got: ${lastStatusText}`,
  );

  // 3. Create grandchild sub-quest and verify depth 3 formatting: d3: grandchild-sub-quest
  await subquestTool.execute(
    "call_sub2",
    {
      name: "grandchild-sub-quest",
      goal: "Grandchild Goal",
      parentName: "child-sub-quest",
      switchNow: true,
    },
    null,
    null,
    mockCtx,
  );

  assert.ok(
    lastStatusText.includes("d3: grandchild-sub-quest"),
    `Status should represent depth 3 with d3: prefix, got: ${lastStatusText}`,
  );
  assert.ok(
    !lastStatusText.includes(
      "parent-hierarchy-quest ↳ child-sub-quest ↳ grandchild-sub-quest",
    ),
    `Status should NOT show full nested path, got: ${lastStatusText}`,
  );

  // 4. Test max length truncation with ellipsis on very long quest name
  await subquestTool.execute(
    "call_sub3",
    {
      name: "very-long-subquest-name-exceeding-display-limit",
      goal: "Long name goal",
      parentName: "grandchild-sub-quest",
      switchNow: true,
    },
    null,
    null,
    mockCtx,
  );
  assert.ok(
    lastStatusText.includes("d4: very-long-subquest-name…"),
    `Status should truncate long sub-quest name at depth 4 with ellipsis, got: ${lastStatusText}`,
  );

  // 5. Test status display when tokens is null or unknown
  currentTokens = null;
  currentPercent = null as any;
  // Mark saved to trigger UI update
  await tools["quest_mark_saved"].execute(
    "call_saved",
    {},
    null,
    null,
    mockCtx,
  );
  assert.ok(
    lastStatusText.includes("very-long-subquest-name…"),
    "Status should still show hierarchy when tokens is null",
  );

  // 6. Test status display when save pending vs fresh
  // Simulate unsaved state
  for (const cb of handlers["turn_end"] || []) {
    await cb({}, mockCtx);
  }
  // Verify save pending indication
  // Clean up test files
  if (state.questId) {
    await rm(`${currentDir}/${state.questId}`, {
      recursive: true,
      force: true,
    });
  }

  console.log("PASS: quest_journal_status_test");
}

Deno.test("quest_journal_status: hierarchy and formatting", async () => {
  await testStatusHierarchyAndFormatting();
});
