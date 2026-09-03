import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import plugin, {
  classifyBashCommand,
  classifyToolCall,
  getState,
  type StoredState,
  type ToolPermission,
} from "../index.ts";

function createMockExtensionAPI() {
  const handlers: Record<string, any[]> = {};
  const registeredTools: any[] = [];
  const registeredCommands: any[] = [];
  const userMessages: Array<
    { msg: any; options?: any; customType?: any; display?: any }
  > = [];

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
      userMessages.push({
        msg: msg?.content || msg,
        options,
        customType: msg?.customType,
        display: msg?.display,
      });
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

Deno.test("quest_journal_enforced_tool_permissions: default-deny enforcement of mutating tools across all execution vectors", async (t) => {
  const QUEST_DIR = ".pi/quest/current";
  await mkdir(QUEST_DIR, { recursive: true });

  // -----------------------------------------------------------------------
  // 1. Direct tool & bash classification unit tests
  // -----------------------------------------------------------------------
  await t.step(
    "1. classifyToolCall and classifyBashCommand classification accuracy",
    () => {
      // Read tools
      assert.strictEqual(
        classifyToolCall("read", { path: "mods/song/song.c" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("search_code", { pattern: "test" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("doc_to_md", { path: "doc.pdf" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("fetch", { url: "http://localhost" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("memory_read", { target: "long_term" }),
        "read",
      );
      assert.strictEqual(classifyToolCall("search_graph", {}), "read");

      // Research tools
      assert.strictEqual(
        classifyToolCall("web_search", { query: "test" }),
        "research",
      );
      assert.strictEqual(
        classifyToolCall("source_check", { claim: "test" }),
        "research",
      );
      assert.strictEqual(
        classifyToolCall("bg_delegate", { name: "inspect" }),
        "research",
      );
      assert.strictEqual(
        classifyToolCall("fusion_investigate", { objective: "test" }),
        "research",
      );

      // Journal tools
      assert.strictEqual(classifyToolCall("quest_update_state", {}), "journal");
      assert.strictEqual(classifyToolCall("quest_mark_saved", {}), "journal");
      assert.strictEqual(classifyToolCall("quest_subquest", {}), "journal");
      assert.strictEqual(
        classifyToolCall("edit", { path: ".pi/quest/current/qid123/quest.md" }),
        "journal",
      );
      assert.strictEqual(
        classifyToolCall("write", { path: ".pi/quest/future/draft-quest.md" }),
        "journal",
      );
      assert.strictEqual(
        classifyToolCall("edit", { path: "MEMORY.md" }),
        "journal",
      );
      assert.strictEqual(
        classifyToolCall("memory_write", { target: "long_term" }),
        "journal",
      );

      // Interaction tools
      assert.strictEqual(
        classifyToolCall("ask_questions", { questions: [] }),
        "interaction",
      );

      // Implementation tools (code modification)
      assert.strictEqual(
        classifyToolCall("edit", { path: "mods/song/song.c" }),
        "implementation",
      );
      assert.strictEqual(
        classifyToolCall("write", { path: "mods/gig/gig.c" }),
        "implementation",
      );
      assert.strictEqual(
        classifyToolCall("bg_run", { name: "build", command: "make" }),
        "implementation",
      );
      assert.strictEqual(
        classifyToolCall("subagent", { agent: "coder", task: "fix" }),
        "implementation",
      );
      assert.strictEqual(
        classifyToolCall("subagent", { action: "list" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("delete_project", {}),
        "implementation",
      );

      // Bash & user_bash tool parity
      assert.strictEqual(
        classifyToolCall("bash", { command: "rg 'player' mods/song" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("user_bash", { command: "rg 'player' mods/song" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("bash", { command: "git status" }),
        "read",
      );
      assert.strictEqual(
        classifyToolCall("user_bash", { command: "git status" }),
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
        classifyToolCall("bash", { command: "rm -f foo.c" }),
        "implementation",
      );
      assert.strictEqual(
        classifyToolCall("user_bash", { command: "rm -f foo.c" }),
        "implementation",
      );

      // Unknown tools
      assert.strictEqual(
        classifyToolCall("unrecognized_third_party_tool", {}),
        "unknown",
      );

      // Bash classification
      assert.strictEqual(classifyBashCommand("pwd"), "read");
      assert.strictEqual(classifyBashCommand("ls -la"), "read");
      assert.strictEqual(classifyBashCommand("find mods/ -name '*.c'"), "read");
      assert.strictEqual(
        classifyBashCommand("rg 'player_init' mods/song"),
        "read",
      );
      assert.strictEqual(classifyBashCommand("cat mods/song/song.c"), "read");
      assert.strictEqual(
        classifyBashCommand("head -n 20 mods/song/song.c"),
        "read",
      );
      assert.strictEqual(
        classifyBashCommand("tail -n 20 mods/song/song.c"),
        "read",
      );
      assert.strictEqual(classifyBashCommand("git status"), "read");
      assert.strictEqual(
        classifyBashCommand("git diff mods/song/song.c"),
        "read",
      );
      assert.strictEqual(classifyBashCommand("git log -n 5"), "read");
      assert.strictEqual(classifyBashCommand("git show HEAD"), "read");
      assert.strictEqual(classifyBashCommand("git branch --list"), "read");
      assert.strictEqual(
        classifyBashCommand("sed -n '1,10p' mods/song/song.c"),
        "read",
      );
      assert.strictEqual(
        classifyBashCommand("cat file.txt | grep foo | wc -l"),
        "read",
      );
      assert.strictEqual(classifyBashCommand("find . >/dev/null 2>&1"), "read");

      // Mutating bash commands
      assert.strictEqual(
        classifyBashCommand("sed -i 's/foo/bar/g' mods/song/song.c"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("perl -i -pe 's/foo/bar/g' file.c"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("echo 'new content' > mods/song/song.c"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("cat snippet.txt >> mods/song/song.c"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand(
          "python3 -c \"open('mods/song/song.c', 'w').write('code')\"",
        ),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand(
          "node -e \"require('fs').writeFileSync('file.txt', 'hi')\"",
        ),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("cp old.c new.c"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("mv file.c renamed.c"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("rm -rf mods/song/temp.o"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("mkdir -p build/obj"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("touch new_file.h"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("git apply patch.diff"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("git checkout feature-branch"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("git reset --hard"),
        "implementation",
      );
      assert.strictEqual(
        classifyBashCommand("git clean -fd"),
        "implementation",
      );
      assert.strictEqual(classifyBashCommand("make"), "implementation");
      assert.strictEqual(classifyBashCommand("cargo build"), "implementation");
    },
  );

  // -----------------------------------------------------------------------
  // 2. Gate blocks all mutating tools during RESEARCH_PENDING
  // -----------------------------------------------------------------------
  await t.step(
    "2. Gate blocks mutating tools while in RESEARCH_PENDING",
    async () => {
      const { mockPi, handlers, registeredCommands } = createMockExtensionAPI();
      plugin(mockPi as any);

      const mockCtx = createMockContext(10000, "session_perm_gated_1");
      const commands: Record<string, any> = {};
      for (const cmd of registeredCommands) commands[cmd.name] = cmd;

      const questSlug = "test-permissions-root-quest";
      await commands["quest"].handler(questSlug, mockCtx);
      const questPath = `.pi/quest/current/${
        getState(mockCtx as any).questId
      }/quest.md`;

      const emitToolCall = async (toolName: string, input?: any) => {
        for (const cb of handlers["tool_call"] || []) {
          const res = await cb({ toolName, input }, mockCtx);
          if (res) return res;
        }
        return null;
      };

      // 1. edit to source -> blocked
      const editRes = await emitToolCall("edit", { path: "mods/song/song.c" });
      assert.ok(editRes, "edit must be gated");
      assert.strictEqual(editRes.block, true);
      assert.ok(editRes.reason.includes("RESEARCH_PENDING"));
      assert.ok(editRes.reason.includes("Permission: implementation"));

      // 2. write to source -> blocked
      const writeRes = await emitToolCall("write", { path: "mods/gig/gig.c" });
      assert.ok(writeRes, "write must be gated");
      assert.strictEqual(writeRes.block, true);
      assert.ok(writeRes.reason.includes("Permission: implementation"));

      // 3. bash containing sed -i -> blocked
      const sedRes = await emitToolCall("bash", {
        command: "sed -i 's/a/b/g' mods/song/song.c",
      });
      assert.ok(sedRes, "sed -i must be blocked");
      assert.strictEqual(sedRes.block, true);
      assert.ok(sedRes.reason.includes("Tool: bash"));
      assert.ok(sedRes.reason.includes("Permission: implementation"));

      // 4. bash containing redirection (> file) -> blocked
      const redirRes = await emitToolCall("bash", {
        command: "echo 'code' > mods/song/song.c",
      });
      assert.ok(redirRes, "output redirection must be blocked");
      assert.strictEqual(redirRes.block, true);
      assert.ok(redirRes.reason.includes("Permission: implementation"));

      // 5. bash containing Python file-writing -> blocked
      const pyRes = await emitToolCall("bash", {
        command: "python3 -c \"open('mods/song/song.c', 'w').write('foo')\"",
      });
      assert.ok(pyRes, "python file write must be blocked");
      assert.strictEqual(pyRes.block, true);

      // 6. subagent implementation -> blocked
      const subagentRes = await emitToolCall("subagent", {
        agent: "coder",
        task: "edit song.c",
      });
      assert.ok(subagentRes, "subagent implementation must be blocked");
      assert.strictEqual(subagentRes.block, true);

      // 7. unknown tool -> blocked
      const unknownRes = await emitToolCall("custom_unknown_tool", { arg: 1 });
      assert.ok(unknownRes, "unknown tool must be blocked default-deny");
      assert.strictEqual(unknownRes.block, true);
      assert.ok(unknownRes.reason.includes("Permission: unknown"));

      // 8. read-only bash -> allowed
      const readBashRes = await emitToolCall("bash", {
        command: "rg 'flac' mods/song",
      });
      assert.strictEqual(
        readBashRes?.block,
        undefined,
        "read-only rg bash command must be allowed",
      );

      const gitStatusRes = await emitToolCall("bash", {
        command: "git status",
      });
      assert.strictEqual(
        gitStatusRes?.block,
        undefined,
        "git status must be allowed",
      );

      const catRes = await emitToolCall("bash", {
        command: "cat mods/song/fields.h",
      });
      assert.strictEqual(catRes?.block, undefined, "cat must be allowed");

      // 9. read tool -> allowed
      const readRes = await emitToolCall("read", { path: "mods/song/song.c" });
      assert.strictEqual(
        readRes?.block,
        undefined,
        "read tool must be allowed",
      );

      // 10. subagent list/inspect -> allowed
      const subagentListRes = await emitToolCall("subagent", {
        action: "list",
      });
      assert.strictEqual(
        subagentListRes?.block,
        undefined,
        "subagent list action must be allowed",
      );

      // 11. quest-file edit -> allowed
      const questEditRes = await emitToolCall("edit", {
        path: `.pi/quest/current/${questSlug}.md`,
      });
      assert.strictEqual(
        questEditRes?.block,
        undefined,
        "edit to active quest file must be allowed",
      );

      // 12. quest-journal tools -> allowed
      const markSavedRes = await emitToolCall("quest_mark_saved", {});
      assert.strictEqual(
        markSavedRes?.block,
        undefined,
        "quest_mark_saved must be allowed",
      );

      // Cleanup
      await rm(questPath, { force: true });
    },
  );

  // -----------------------------------------------------------------------
  // 3. Opening gate restores full implementation tool access
  // -----------------------------------------------------------------------
  await t.step(
    "3. Opening gate restores full implementation tool access",
    async () => {
      const { mockPi, handlers, registeredTools, registeredCommands } =
        createMockExtensionAPI();
      plugin(mockPi as any);

      const mockCtx = createMockContext(10000, "session_perm_open_2");
      const tools: Record<string, any> = {};
      for (const tool of registeredTools) tools[tool.name] = tool;
      const commands: Record<string, any> = {};
      for (const cmd of registeredCommands) commands[cmd.name] = cmd;

      const questSlug = "test-permissions-unlocked-quest";
      await commands["quest"].handler(questSlug, mockCtx);
      const questPath = `.pi/quest/current/${
        getState(mockCtx as any).questId
      }/quest.md`;

      await writeFile(
        questPath,
        `# Quest: ${questSlug}\n\n## Goal\nTest permissions unlocked\n\n## Current Understanding\nVerified complete architecture.\n\n## Key Assumptions\n- None\n\n## Research Findings\n- All dependencies available\n\n## Open Questions & Uncertainties\n- None\n\n## Plan\n1. Modify song.c\n\n## Plan Confidence\nHigh\n\n## Exact Next Action\nEdit song.c\n`,
        "utf8",
      );

      // Perform genuine investigation tool_result
      for (const cb of handlers["tool_result"] || []) {
        await cb({
          toolName: "read",
          input: { path: "mods/song/song.c" },
          output: "code",
          isError: false,
        }, mockCtx);
      }

      // Complete research
      await tools["quest_update_state"].execute(
        "call_research_done",
        {
          name: questSlug,
          researchComplete: true,
          planConfidence: "high",
          exactNextAction: "Edit song.c",
        },
        null,
        null,
        mockCtx,
      );

      // Confirm root quest plan via ask_questions
      for (const cb of handlers["tool_result"] || []) {
        await cb(
          {
            toolName: "ask_questions",
            input: {
              questions: [{
                question: "Proceed with implementation?",
                options: [{ label: "Yes, proceed with implementation" }],
              }],
            },
            details: {
              status: "answered",
              answers: [
                {
                  questionIndex: 0,
                  question: "Proceed with implementation?",
                  answer: "Yes, proceed with implementation",
                },
              ],
            },
            isError: false,
          },
          mockCtx,
        );
      }

      const emitToolCall = async (toolName: string, input?: any) => {
        for (const cb of handlers["tool_call"] || []) {
          const res = await cb({ toolName, input }, mockCtx);
          if (res) return res;
        }
        return null;
      };

      // Now implementation is unlocked
      const editRes = await emitToolCall("edit", { path: "mods/song/song.c" });
      assert.strictEqual(
        editRes?.block,
        undefined,
        "edit to code must be allowed once gate is open",
      );

      const writeRes = await emitToolCall("write", { path: "mods/gig/gig.c" });
      assert.strictEqual(
        writeRes?.block,
        undefined,
        "write to code must be allowed once gate is open",
      );

      const bashSedRes = await emitToolCall("bash", {
        command: "sed -i 's/a/b/g' mods/song/song.c",
      });
      assert.strictEqual(
        bashSedRes?.block,
        undefined,
        "mutating bash must be allowed once gate is open",
      );

      const bashMakeRes = await emitToolCall("bash", {
        command: "make -C mods/song",
      });
      assert.strictEqual(
        bashMakeRes?.block,
        undefined,
        "make must be allowed once gate is open",
      );

      const subagentRes = await emitToolCall("subagent", {
        agent: "coder",
        task: "implement",
      });
      assert.strictEqual(
        subagentRes?.block,
        undefined,
        "subagent must be allowed once gate is open",
      );

      // Cleanup
      await rm(questPath, { force: true });
    },
  );
});
