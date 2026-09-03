import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import {
  activateExistingQuest,
  asyncContext,
  getQuestLogPath,
  getState,
  isQuestSessionActive,
  readQuestLog,
  SESSION_LIVENESS_FILE,
  setSessionLivenessAsStale,
  writeSessionLiveness,
} from "../index.ts";

function currentDirFor(): string {
  return ".pi/quest/current";
}

function createMockExtensionAPI() {
  const handlers: Record<string, any[]> = {};
  const agentMessages: Array<{ msg: any; options?: any }> = [];
  const mockPi: any = {
    on(event: string, handler: any) {
      if (!handlers[event]) handlers[event] = [];
      handlers[event].push(handler);
    },
    appendEntry() {},
    registerEntryRenderer() {},
    registerTool() {},
    registerCommand() {},
    sendMessage(msg: any, options?: any) {
      agentMessages.push({ msg: msg?.content || msg, options });
    },
    sendUserMessage() {},
    getAllTools: () => [],
    events: { on: () => () => {}, emit: () => {} },
  };
  return { mockPi, handlers, agentMessages };
}

function createMockContext(sessionId: string): any {
  return {
    cwd: process.cwd(),
    mode: "agent",
    hasUI: true,
    sessionManager: {
      id: sessionId,
      sessionId,
      getBranch: () => [],
      appendCustomEntry: () => {},
    },
    getContextUsage: () => ({ tokens: 50000, percent: 6.25 }),
    ui: {
      notify: () => {},
      setStatus: () => {},
      input: async () => "",
      select: async () => null,
    },
  };
}

const MINIMAL_QUEST = [
  `---`,
  `questId: x-sess-quest-1`,
  `---`,
  ``,
  `# Quest: X Sess Quest One`,
  ``,
  `## Goal`,
  `Mount coalescence across sessions`,
  ``,
  `## Current Status`,
  `- [ ] in progress`,
  ``,
  `## Current Understanding`,
  `Second session must not spawn fresh turn 0.`,
  ``,
  `## Key Assumptions`,
  `- [x] none`,
  ``,
  `## Open Questions & Uncertainties`,
  `- [ ] none`,
  ``,
  `## Research Findings`,
  `- none`,
  ``,
  `## Plan Version`,
  `2`,
  ``,
  `## Plan`,
  `1. test`,
  ``,
  `## Plan Confidence`,
  `high`,
  ``,
].join("\n");

async function makeQuest(slug: string): Promise<string> {
  const currentDir = currentDirFor();
  const dir = `${currentDir}/${slug}`;
  await mkdir(dir, { recursive: true });
  await writeFile(`${dir}/quest.md`, MINIMAL_QUEST, "utf8");
  return `${dir}/quest.md`;
}

Deno.test("quest_journal_cross_session_mount: QUEST_REUSED mount coalesces against another live session", async (t) => {
  const currentDir = currentDirFor();
  await rm(currentDir, { recursive: true, force: true });
  await mkdir(currentDir, { recursive: true });
  questJournalExtension(createMockExtensionAPI().mockPi);

  const slug = "x-sess-quest-1";
  await makeQuest(slug);

  await t.step("0. helpers exist and marker file const is sane", () => {
    assert.strictEqual(SESSION_LIVENESS_FILE, ".session.liveness");
    assert.strictEqual(typeof writeSessionLiveness, "function");
    assert.strictEqual(typeof isQuestSessionActive, "function");
    assert.strictEqual(typeof setSessionLivenessAsStale, "function");
  });

  await t.step(
    "1. session A mounts the quest (fresh session, writes liveness)",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctxA = createMockContext("session_A_type_56");
      getState(ctxA).questId = null;

      let mounted: boolean | null = null;
      await asyncContext.run(ctxA, async () => {
        mounted = await activateExistingQuest(
          mockPi,
          ctxA,
          slug,
          "please continue",
        );
      });

      assert.strictEqual(
        mounted,
        true,
        "First live session must mount the quest",
      );

      // A's session must be the one owning the liveness marker
      const ownActive = isQuestSessionActive(slug, "session_A_type_56");
      assert.strictEqual(
        ownActive,
        false,
        "Same-session liveness must NOT be seen as an active foreign session",
      );
    },
  );

  await t.step(
    "2. session B (different sessionId) calling activateExistingQuest is coalesced (returns false, no TURN_START 0, no saveGeneration reset)",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctxB = createMockContext("session_B_type_56");
      getState(ctxB).questId = null;

      const stateB = getState(ctxB);
      stateB.lastSavedHash = "known-hash-from-before";

      // Force a fresh liveness marker for session A so B sees an active foreign session
      writeSessionLiveness(slug, "session_A_type_56");
      assert.strictEqual(
        isQuestSessionActive(slug, "session_B_type_56"),
        true,
        "B must see A's fresh liveness as active",
      );

      let mountedB: boolean | null = null;
      await asyncContext.run(ctxB, async () => {
        mountedB = await activateExistingQuest(
          mockPi,
          ctxB,
          slug,
          "please continue",
        );
      });

      assert.strictEqual(
        mountedB,
        false,
        "Second live session must be refused/coalesced",
      );
      assert.strictEqual(
        stateB.saveGeneration,
        null,
        "saveGeneration must NOT be reset when coalesced",
      );
      assert.strictEqual(
        stateB.questId,
        null,
        "questId must NOT be reassigned when coalesced",
      );

      // The execution log must contain a QUEST_REUSED_COALESCED entry for the coalesced session
      const log = readQuestLog(getQuestLogPath(slug));
      assert.ok(
        log.includes("QUEST_REUSED_COALESCED"),
        "log must contain QUEST_REUSED_COALESCED for the coalesced second mount",
      );
    },
  );

  await t.step(
    "3. same-session re-entry is allowed (idempotent for the owning session)",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctxA = createMockContext("session_A_type_56");
      getState(ctxA).questId = null;
      writeSessionLiveness(slug, "session_A_type_56");

      let mounted: boolean | null = null;
      await asyncContext.run(ctxA, async () => {
        mounted = await activateExistingQuest(
          mockPi,
          ctxA,
          slug,
          "please continue",
        );
      });
      assert.strictEqual(
        mounted,
        true,
        "Same-session re-entry must still mount",
      );
    },
  );

  await t.step(
    "4. stale liveness marker lets a new session mount (crash recovery)",
    async () => {
      const { mockPi } = createMockExtensionAPI();
      const ctxC = createMockContext("session_C_type_56");
      getState(ctxC).questId = null;

      writeSessionLiveness(slug, "session_CRASHED");
      setSessionLivenessAsStale(slug);

      assert.strictEqual(
        isQuestSessionActive(slug, "session_C_type_56"),
        false,
        "Stale marker must not block a new session",
      );

      let mounted: boolean | null = null;
      await asyncContext.run(ctxC, async () => {
        mounted = await activateExistingQuest(
          mockPi,
          ctxC,
          slug,
          "please continue",
        );
      });
      assert.strictEqual(
        mounted,
        true,
        "New session must mount after stale liveness",
      );
    },
  );
});
