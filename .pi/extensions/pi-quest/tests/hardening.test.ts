import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { resolveQuestRecordBySlug } from "../src/paths.ts";

Deno.test("quest_journal_hardening: code fence parsing, timer safety, token calculations, and path consistency", async () => {
  const tools: Record<string, any> = {};
  const commands: Record<string, any> = {};

  const mockPi: any = {
    on() {},
    appendEntry() {},
    registerEntryRenderer() {},
    registerTool(tool: any) {
      tools[tool.name] = tool;
    },
    registerCommand(name: string, cmd: any) {
      commands[name] = cmd;
    },
    sendUserMessage() {},
  };

  questJournalExtension(mockPi);

  await mkdir(".pi/quest/current", { recursive: true });
  await mkdir(".pi/quest/future", { recursive: true });
  await mkdir(".pi/quest/archive", { recursive: true });

  const testQuestSlug = "test-hardening-quest";
  let compactedWithInstructions: string | null = null;
  const mockCtx: any = {
    cwd: "/home/quirinpa/site",
    hasUI: true,
    mode: "tui",
    sessionManager: {
      id: "session_hardening_main",
      getBranch: () => [],
    },
    ui: {
      notify: () => {},
      setStatus: () => {},
    },
    getContextUsage: () => ({
      tokens: 50000,
      contextWindow: 200000,
      percent: 25,
    }),
    compact: (options: any) => {
      compactedWithInstructions = options.customInstructions;
      if (options.onComplete) options.onComplete();
    },
  };

  await commands["quest"].handler(testQuestSlug, mockCtx);
  const initialRecord = await resolveQuestRecordBySlug(testQuestSlug);
  assert.ok(initialRecord, "Quest record must exist");
  const testQuestPath = initialRecord.path;

  // 1. Markdown code fences containing `# comments` and `## fake headings`
  const markdownWithFences = [
    `# Quest: ${testQuestSlug}`,
    "",
    "## Goal",
    "Test that code fences don't break section parsing.",
    "",
    "## In-Depth Analysis & Findings",
    "Here is a code snippet with shell comments and simulated markdown:",
    "```bash",
    "# This is a bash comment that looks like a markdown heading",
    "## This looks like an H2 section inside code",
    "### This looks like an H3 section inside code",
    "echo 'Hello world'",
    "```",
    "",
    "And here is another snippet using tildes:",
    "~~~markdown",
    "## Fake Section Inside Tildes",
    "Content inside tildes.",
    "~~~",
    "",
    "## Current Status",
    "- [ ] in progress",
    "",
    "## Next recommended step",
    "Verify parser robustness",
  ].join("\n");

  await writeFile(testQuestPath, markdownWithFences, "utf8");

  // Update state using structured tool
  const updateRes = await tools["quest_update_state"].execute(
    "call_update",
    {
      status: "- [x] Hardening Complete",
      nextStep: "Verify all unit tests pass",
    },
    null,
    null,
    mockCtx,
  );

  assert.ok(
    updateRes &&
      updateRes.content[0].text.includes("Successfully updated quest state"),
    "Update state must succeed",
  );

  const updatedContent = await readFile(testQuestPath, "utf8");

  // Verify that code fence contents survived verbatim and weren't sliced as sections
  assert.ok(
    updatedContent.includes(
      "# This is a bash comment that looks like a markdown heading",
    ),
    "Bash comments inside code fences must survive",
  );
  assert.ok(
    updatedContent.includes("## This looks like an H2 section inside code"),
    "H2 inside code fences must not be treated as a section header",
  );
  assert.ok(
    updatedContent.includes("## Fake Section Inside Tildes"),
    "H2 inside tilde fences must not be treated as a section header",
  );
  assert.ok(
    updatedContent.includes("- [x] Hardening Complete"),
    "Current status must be properly updated",
  );
  assert.ok(
    updatedContent.includes("Verify all unit tests pass"),
    "Next step must be updated",
  );

  // 2. Timer Safety & AsyncContext preservation
  const sessionCtxA: any = {
    cwd: "/home/quirinpa/site",
    hasUI: true,
    sessionManager: { id: "session_timer_a", getBranch: () => [] },
    ui: { notify: () => {}, setStatus: () => {} },
    getContextUsage: () => ({
      tokens: 100000,
      contextWindow: 120000,
      percent: 83,
    }),
    compact: (options: any) => {
      if (options.onComplete) options.onComplete();
    },
  };

  const sessionCtxB: any = {
    cwd: "/home/quirinpa/site",
    hasUI: true,
    sessionManager: { id: "session_timer_b", getBranch: () => [] },
    ui: { notify: () => {}, setStatus: () => {} },
    getContextUsage: () => ({
      tokens: 20000,
      contextWindow: 120000,
      percent: 16,
    }),
  };

  await commands["quest"].handler("timer-quest-a", sessionCtxA);
  await commands["quest"].handler("timer-quest-b", sessionCtxB);

  // Trigger archive in session A with compact: true
  const archiveRes = await tools["quest_archive"].execute(
    "call_archive",
    { questName: "timer-quest-a", compact: true },
    null,
    null,
    sessionCtxA,
  );

  assert.ok(
    archiveRes.details.archived === "timer-quest-a",
    "Archive must target timer-quest-a",
  );

  // Wait for timer ticks to resolve
  await new Promise((resolve) => setTimeout(resolve, 80));

  // Verify session B was unaffected
  const statusB = await commands["quest-status"].handler("", sessionCtxB);
  assert.ok(
    statusB.includes("timer-quest-b"),
    "Session B must still have timer-quest-b active after timer callbacks in session A",
  );

  // Cleanup
  await rm(".pi/quest/current", { recursive: true, force: true });
});
