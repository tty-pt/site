import assert from "node:assert";
import { mkdir, rm } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_compaction_escalation: periodic checkpoint lifecycle", async (t) => {
  const currentDir = ".pi/quest/current";
  await mkdir(currentDir, { recursive: true });

  const questSlug = "persistent-compaction-test";
  const handlers: Record<string, EventCallback[]> = {};
  const commands: Record<string, any> = {};
  const tools: Record<string, any> = {};
  const userMessages: Array<{ msg: any; options?: any }> = [];
  let compactCallCount = 0;

  const mockCtx: any = {
    cwd: process.cwd(),
    getContextUsage: () => ({ tokens: 50000, contextWindow: 1000000 }),
    sessionManager: { id: "session_escalation_test", getBranch: () => [] },
    compact: (opts?: any) => {
      compactCallCount++;
      if (typeof opts?.onComplete === "function") opts.onComplete();
    },
    ui: { notify: () => {}, setStatus: () => {} },
    hasUI: true,
    mode: "headless",
  };

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
  await commands["quest"].handler(questSlug, mockCtx);

  const emitTurnEnd = async (
    toolResults: any[] = [{
      toolName: "edit",
      input: { path: "mods/song/song.c" },
    }],
  ) => {
    // need tool_result to mark dirty / increment counter
    for (const cb of handlers["tool_result"] || []) {
      // only emit tool_result for substantive tools; for this helper we simulate via turn_end analysis directly
    }
    await new Promise((resolve) => setTimeout(resolve, 60));
    for (const cb of handlers["turn_end"] || []) {
      await cb({ toolResults }, mockCtx);
    }
  };

  await t.step("1. Below 6 turns: no periodic steering", async () => {
    userMessages.length = 0;
    for (let i = 0; i < 5; i++) {
      for (const cb of handlers["tool_result"] || []) {
        await cb(
          { toolName: "edit", input: { path: `mods/song/${i}.c` } },
          mockCtx,
        );
      }
      await emitTurnEnd([{
        toolName: "edit",
        args: { path: `mods/song/${i}.c` },
      }]);
    }
    assert.strictEqual(
      userMessages.filter((m) => m.msg?.includes("Periodic")).length,
      0,
    );
  });

  await t.step("2. 6th turn: periodic steering issued", async () => {
    userMessages.length = 0;
    for (const cb of handlers["tool_result"] || []) {
      await cb({ toolName: "edit", input: { path: "mods/song/5.c" } }, mockCtx);
    }
    await emitTurnEnd([{ toolName: "edit", args: { path: "mods/song/5.c" } }]);
    assert.strictEqual(userMessages.length, 1);
    assert.strictEqual(userMessages[0].options?.deliverAs, "steer");
    assert.ok(userMessages[0].msg.includes("Periodic Durable Checkpoint"));
  });

  await t.step("3. After save, need another 6 turns", async () => {
    await tools["quest_mark_saved"].execute("save_1", {}, null, null, mockCtx);
    await new Promise((r) => setTimeout(r, 60));
    userMessages.length = 0;
    for (let i = 0; i < 5; i++) {
      for (const cb of handlers["tool_result"] || []) {
        await cb(
          { toolName: "edit", input: { path: `mods/song/again${i}.c` } },
          mockCtx,
        );
      }
      await emitTurnEnd([{
        toolName: "edit",
        args: { path: `mods/song/again${i}.c` },
      }]);
    }
    assert.strictEqual(
      userMessages.filter((m) => m.msg?.includes("Periodic")).length,
      0,
    );
    for (const cb of handlers["tool_result"] || []) {
      await cb(
        { toolName: "edit", input: { path: "mods/song/again5.c" } },
        mockCtx,
      );
    }
    await emitTurnEnd([{
      toolName: "edit",
      args: { path: "mods/song/again5.c" },
    }]);
    assert.ok(
      userMessages.some((m) => m.msg?.includes("Periodic Durable Checkpoint")),
    );
  });

  await t.step("4. Dirty blocks compaction", async () => {
    for (const cb of handlers["tool_result"] || []) {
      await cb(
        { toolName: "edit", input: { path: "mods/gig/gig.c" } },
        mockCtx,
      );
    }
    userMessages.length = 0;
    let res: any;
    for (const cb of handlers["session_before_compact"] || []) {
      res = await cb({}, mockCtx);
    }
    assert.strictEqual(res?.cancel, true);
    assert.ok(userMessages[0].msg.includes("Compaction Blocked"));
  });

  await t.step(
    "5. After save, compaction allowed and post-compaction resumes",
    async () => {
      await tools["quest_mark_saved"].execute(
        "save_2",
        {},
        null,
        null,
        mockCtx,
      );
      let res: any;
      for (const cb of handlers["session_before_compact"] || []) {
        res = await cb({}, mockCtx);
      }
      assert.notStrictEqual(res?.cancel, true);
      userMessages.length = 0;
      for (const cb of handlers["session_compact"] || []) await cb({}, mockCtx);
      assert.ok(
        userMessages.some((m) =>
          (typeof m.msg === "string" ? m.msg : "").includes("Post-Compaction")
        ),
      );
    },
  );

  await rm(`${currentDir}/${questSlug}.md`, { force: true });
});
