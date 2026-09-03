import assert from "node:assert";
import { resolve } from "node:path";
import { mkdir, rm, writeFile } from "node:fs/promises";
import {
  createDefaultState,
  type ExtensionAPI,
  type ExtensionContext,
  findProjectRoot,
  getState,
  QuestErrorCode,
  readQuestLog,
  resolveSubagentCwd,
  resolveSubagentExecutor,
  setCustomSubagentRunner,
} from "../index.ts";

function createEventBus() {
  type Handler = (data: any) => void;
  const handlers: Record<string, Handler[]> = {};
  const emitted: Array<{ event: string; data: any }> = [];
  return {
    emitted,
    events: {
      on: (event: string, handler: Handler) => {
        if (!handlers[event]) handlers[event] = [];
        handlers[event].push(handler);
        return () => {
          const i = handlers[event].indexOf(handler);
          if (i >= 0) handlers[event].splice(i, 1);
        };
      },
      emit: (event: string, data: any) => {
        emitted.push({ event, data });
        for (const h of [...(handlers[event] || [])]) {
          try {
            h(data);
          } catch {}
        }
      },
    },
  };
}

function createMockExtensionAPI(tools: string[] = ["subagent"]) {
  const bus = createEventBus();
  const mockPi: ExtensionAPI = {
    on: () => {
      // session-level handlers not needed for executor test
    },
    registerTool: () => {},
    registerCommand: () => {},
    sendUserMessage: () => {},
    sendMessage: () => {},
    appendEntry: () => {},
    registerEntryRenderer: () => {},
    getAllTools: () => tools.map((name) => ({ name })),
    events: bus.events,
  };
  return { mockPi, bus };
}

function createMockContext(
  cwd: string,
  sessionId = `session_${Date.now().toString(36)}`,
): ExtensionContext {
  const branch: any[] = [];
  return {
    cwd,
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
    getContextUsage: () => ({ tokens: 1000, percent: 1 }),
    ui: {
      notify: () => {},
      setStatus: () => {},
      input: async () => "",
      select: async () => null,
    },
  };
}

function extensionDirInside(projectRoot: string): string {
  return resolve(projectRoot, ".pi/extensions/pi-quest");
}

Deno.test("Subagent Working Dir Suite: subagent cwd is anchored to project root (#58)", async (t) => {
  const currentDir = ".pi/quest/current";
  await rm(currentDir, { recursive: true, force: true });
  await mkdir(currentDir, { recursive: true });
  setCustomSubagentRunner(null);
  const projectRoot = findProjectRoot(process.cwd());

  await t.step(
    "1. resolveSubagentCwd returns project root for a stray extension-dir cwd",
    () => {
      const stray = extensionDirInside(projectRoot);
      const cwd = resolveSubagentCwd(createMockContext(stray) as any);
      assert.strictEqual(
        cwd,
        projectRoot,
        `expected ${projectRoot}, got ${cwd}`,
      );
      assert.ok(
        !cwd.includes("/.pi/extensions/"),
        "must not resolve into the extension dir",
      );
    },
  );

  await t.step(
    "2. resolveSubagentCwd returns project root for a normal repo cwd",
    () => {
      const ctx = createMockContext(projectRoot);
      const cwd = resolveSubagentCwd(ctx as any);
      assert.strictEqual(cwd, projectRoot);
    },
  );

  await t.step(
    "3. subagent request emits cwd anchored to project root even from stray cwd",
    async () => {
      const { mockPi, bus } = createMockExtensionAPI(["subagent"]);
      const stray = extensionDirInside(projectRoot);
      const executor = resolveSubagentExecutor(
        mockPi as any,
        createMockContext(stray) as any,
      );
      assert.ok(
        executor,
        "subagent executor must be resolved when subagent tool registered",
      );

      let emittedCwd = "";
      let emittedRequestId = "";
      bus.events.on("prompt-template:subagent:request", (data: any) => {
        if (data && typeof data.cwd === "string") {
          emittedCwd = data.cwd;
          if (data.requestId) emittedRequestId = data.requestId;
        }
      });

      const promise = executor("DIRECTION REVIEW task", {
        agent: "reviewer",
        isCriticalReview: true,
        model: "x/y",
        async: true,
      });
      // Resolve the pending structured bridge promise after emit, using the requestId the executor generated
      bus.events.emit("prompt-template:subagent:response", {
        requestId: emittedRequestId,
        status: "completed",
        result: { kind: "text", text: "VERDICT: PASS" },
      });
      await promise;

      assert.strictEqual(
        emittedCwd,
        projectRoot,
        `emitted cwd must be project root, got ${emittedCwd}`,
      );
      assert.ok(
        !emittedCwd.includes("/.pi/extensions/"),
        "must not emit extension dir as cwd",
      );
    },
  );

  await t.step(
    "4. SUBAGENT_CWD_REANCHORED is logged when cwd was misdirected, not when normal",
    async () => {
      const strayQid = "reanchor-stray";
      await mkdir(`${currentDir}/${strayQid}`, { recursive: true });
      await writeFile(
        `${currentDir}/${strayQid}/quest.md`,
        `# Quest: reanchor-stray\n\n## Goal\nReanchor test\n\n## Original request\n> Fix the consumer side code\n`,
        "utf8",
      );
      const normalQid = "reanchor-normal";
      await mkdir(`${currentDir}/${normalQid}`, { recursive: true });
      await writeFile(
        `${currentDir}/${normalQid}/quest.md`,
        `# Quest: reanchor-normal\n\n## Goal\nReanchor test\n\n## Original request\n> Fix the consumer side code\n`,
        "utf8",
      );

      const stray = extensionDirInside(projectRoot);
      const ctxStray = createMockContext(stray, "session_reanchor_1");
      const s = getState(ctxStray);
      s.active = strayQid;
      s.questId = strayQid;
      s.stack = [strayQid];

      resolveSubagentCwd(ctxStray); // triggers re-anchor => should log SUBAGENT_CWD_REANCHORED
      const strayLog = await readQuestLog(ctxStray);
      assert.ok(
        strayLog.includes("SUBAGENT_CWD_REANCHORED"),
        "re-anchor diagnostic must be logged for stray cwd",
      );

      // Normal repo cwd (its own qid) => no re-anchor event
      const ctxNormal = createMockContext(projectRoot, "session_reanchor_2");
      const sn = getState(ctxNormal);
      sn.active = normalQid;
      sn.questId = normalQid;
      sn.stack = [normalQid];
      resolveSubagentCwd(ctxNormal);
      const normalLog = await readQuestLog(ctxNormal);
      assert.ok(
        !normalLog.includes("SUBAGENT_CWD_REANCHORED"),
        "no re-anchor diagnostic for normal cwd",
      );
    },
  );

  setCustomSubagentRunner(null);
});
