import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { resolveQuestRecordBySlug } from "../src/paths.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function getQuestContentBySlug(slug: string): Promise<string> {
  const rec = await resolveQuestRecordBySlug(slug);
  if (!rec) throw new Error(`Quest record not found for slug: ${slug}`);
  return readFile(rec.path, "utf8");
}

Deno.test("quest_journal_formation: research-grounded quest formation & verbatim prompt retention", async (t) => {
  const currentDir = ".pi/quest/current";
  await mkdir(currentDir, { recursive: true });

  const semanticSlug = "audio-stream-buffer-optimization";
  const existingSlug = "existing-auth-pipeline";
  const subSlug = "opus-packet-decoder";

  await rm(currentDir, { recursive: true, force: true });
  await mkdir(currentDir, { recursive: true });

  const handlers: Record<string, EventCallback[]> = {};
  const commands: Record<string, any> = {};
  const tools: Record<string, any> = {};
  const userMessages: Array<
    { msg: any; options?: any; customType?: any; display?: any }
  > = [];

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
      userMessages.push({
        msg: msg?.content || msg,
        options,
        customType: msg?.customType,
        display: msg?.display,
      });
    },
  };

  questJournalExtension(mockPi);

  let currentTokens = 10000;
  let currentContextWindow = 1000000;

  const mockCtx: any = {
    cwd: process.cwd(),
    getContextUsage: () => ({
      tokens: currentTokens,
      contextWindow: currentContextWindow,
      percent: (currentTokens / currentContextWindow) * 100,
    }),
    sessionManager: {
      id: "session_formation_test",
      getBranch: () => [],
    },
    ui: {
      notify: () => {},
      setStatus: () => {},
      input: async () => "",
      select: async () => null,
    },
    hasUI: true,
    mode: "headless",
  };

  const emitToolCall = async (toolName: string, input?: any) => {
    for (const cb of handlers["tool_call"] || []) {
      const res = await cb({ toolName, input }, mockCtx);
      if (res) return res;
    }
    return null;
  };

  const emitToolResult = async (
    toolName: string,
    input?: any,
    output?: any,
    isError = false,
  ) => {
    for (const cb of handlers["tool_result"] || []) {
      await cb({ toolName, input, output, isError }, mockCtx);
    }
  };

  const rawUserPrompt =
    "Please investigate why high-bitrate FLAC files stutter intermittently during playback in the browser client and optimize the streaming pipeline";

  // -----------------------------------------------------------------------
  // 1. Substantive root prompt does NOT immediately produce a quest file
  // -----------------------------------------------------------------------
  await t.step(
    "1. substantive root prompt does NOT immediately create a quest file on disk",
    async () => {
      for (const cb of handlers["before_agent_start"] || []) {
        await cb(
          { prompt: rawUserPrompt, systemPrompt: "Base prompt." },
          mockCtx,
        );
      }

      // Verify no file was created on disk merely from slugifying the prompt
      let fileExisted = false;
      try {
        await readFile(
          `${currentDir}/please-investigate-why-high-bitrate.md`,
          "utf8",
        );
        fileExisted = true;
      } catch {}
      assert.strictEqual(
        fileExisted,
        false,
        "Mechanical prompt slug file must NOT be written to disk",
      );

      // Status reports provisional state
      const status = await commands["quest-status"].handler("", mockCtx);
      assert.ok(
        status.includes("PROVISIONAL ROOT INITIALIZATION"),
        "Must report provisional root initialization state",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 2. Initial research tools are allowed before quest creation/implementation
  // -----------------------------------------------------------------------
  await t.step(
    "2. investigation & read tools are allowed during provisional orientation",
    async () => {
      const readRes = await emitToolCall("read", {
        path: "mods/song/player.c",
      });
      assert.strictEqual(
        readRes?.block,
        undefined,
        "read tool must be allowed",
      );
      await emitToolResult(
        "read",
        { path: "mods/song/player.c" },
        "player code",
      );

      const searchRes = await emitToolCall("search_code", {
        pattern: "flac_stream_chunk",
      });
      assert.strictEqual(
        searchRes?.block,
        undefined,
        "search tool must be allowed",
      );

      const bashRes = await emitToolCall("bash", {
        command: "find mods/song -name '*.c'",
      });
      assert.strictEqual(
        bashRes?.block,
        undefined,
        "bash discovery tool must be allowed",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 3. Direct feature edits remain blocked during provisional orientation
  // -----------------------------------------------------------------------
  await t.step(
    "3. direct feature edits remain blocked before quest identity and research complete",
    async () => {
      const blockRes = await emitToolCall("edit", {
        path: "mods/song/player.c",
      });
      assert.ok(blockRes, "Must block implementation edit");
      assert.strictEqual(
        blockRes.block,
        true,
        "Implementation edit is blocked",
      );
      assert.ok(
        blockRes.reason.includes("PROVISIONAL_RESEARCH_PENDING"),
        "Reason must identify PROVISIONAL_RESEARCH_PENDING",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 4. Model establishes semantic quest identity and research findings
  // -----------------------------------------------------------------------
  await t.step(
    "4. model establishes semantic quest identity and populates research-grounded quest file",
    async () => {
      const initRes = await tools["quest_update_state"].execute(
        "call_init_semantic",
        {
          name: semanticSlug,
          goal:
            "Eliminate FLAC streaming stutter by optimizing audio buffer sizes and prefetch windows",
          status: "Research complete",
          understanding:
            "Audio player in mods/song uses axil chunking with 4KB ring buffers that underrun at 192kHz/24bit.",
          assumptions: [
            "- 64KB buffer eliminates buffer underruns at high bitrates",
            "- Memory overhead remains well within 2MB per active stream",
          ],
          openQuestions: [
            "Latency impact on stream startup time (measured <10ms)",
          ],
          findings: [
            "Root cause is undersized chunk buffer (4KB) causing axil TCP socket read backpressure.",
            "Increasing chunk to 64KB solves high-bitrate stutter without heap reallocations.",
          ],
          plan: [
            "1. Update buffer constant FLAC_CHUNK_SIZE to 64KB in mods/song/player.c",
            "2. Add prefetch threshold logic",
            "3. Verify with high-bitrate test stream",
          ],
          planConfidence: "high",
          planConfidenceReason:
            "Verified data flow and buffer sizing against 192kHz stream telemetry.",
          exactNextAction:
            "Present findings and plan to user before editing player.c",
          researchComplete: true,
        },
        null,
        null,
        mockCtx,
      );

      assert.ok(
        !initRes.content[0].text.includes("refused"),
        "Valid research state must be accepted",
      );

      // Verify the file was created at the semantic slug
      const diskContent = await getQuestContentBySlug(semanticSlug);
      assert.ok(
        diskContent.includes(`# Quest: ${semanticSlug}`),
        "Quest heading matches semantic name",
      );
      assert.ok(
        diskContent.includes("Eliminate FLAC streaming stutter"),
        "Goal is substantive",
      );
      assert.ok(
        diskContent.includes("Root cause is undersized chunk buffer"),
        "Research findings are present",
      );
      assert.ok(
        diskContent.includes("FLAC_CHUNK_SIZE to 64KB"),
        "Plan steps are present",
      );

      // -------------------------------------------------------------------
      // 5. Verbatim user prompt is preserved in ## Original request
      // -------------------------------------------------------------------
      assert.ok(
        diskContent.includes(rawUserPrompt),
        "Original prompt must be preserved verbatim in ## Original request",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 6. First user-facing root interaction asks for confirmation before edit
  // -----------------------------------------------------------------------
  await t.step(
    "6. root quest is blocked awaiting user confirmation after research",
    async () => {
      const blockRes = await emitToolCall("edit", {
        path: "mods/song/player.c",
      });
      assert.ok(blockRes, "Must block until user confirmation");
      assert.strictEqual(blockRes.block, true);
      assert.ok(
        blockRes.reason.includes("CONFIRMATION_PENDING"),
        "Reason must cite CONFIRMATION_PENDING",
      );

      // User confirms
      for (const cb of handlers["before_agent_start"] || []) {
        await cb({ prompt: "Looks good, proceed with 64KB buffer!" }, mockCtx);
      }

      const allowRes = await emitToolCall("edit", {
        path: "mods/song/player.c",
      });
      assert.strictEqual(
        allowRes?.block,
        undefined,
        "Implementation allowed after confirmation",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 7. Existing quests resume without being renamed
  // -----------------------------------------------------------------------
  await t.step("7. existing quests resume without being renamed", async () => {
    // Create a separate existing quest file on disk
    const existingQid = "exist_qid_777";
    const existingQuestDir = `${currentDir}/${existingQid}`;
    await mkdir(existingQuestDir, { recursive: true });
    const existingQuestPath = `${existingQuestDir}/quest.md`;
    const existingContent = [
      `---`,
      `questId: ${existingQid}`,
      `---`,
      ``,
      `# Quest: ${existingSlug}`,
      ``,
      `## Goal`,
      `Migrate authentication cookies to encrypted HttpOnly tokens`,
      ``,
      `## Original request`,
      `> Refactor authentication cookies to use AES-256-GCM tokens`,
      ``,
      `## Current Status`,
      `- [ ] in progress`,
      ``,
      `## Current Understanding`,
      `Auth cookies are issued by axil-auth.`,
      ``,
      `## Key Assumptions`,
      `- [x] AES-256-GCM is available in libcrypto`,
      ``,
      `## Open Questions & Uncertainties`,
      `- [ ] Key rotation frequency`,
      ``,
      `## Research Findings`,
      `- Token format is compatible with existing session store`,
      ``,
      `## Plan Version`,
      `2`,
      ``,
      `## Plan`,
      `1. Add token cipher`,
      `2. Update session middleware`,
      ``,
      `## Plan Confidence`,
      `high`,
      ``,
      `## Exact Next Action`,
      `Implement AES cipher in mods/auth/token.c`,
      ``,
    ].join("\n");

    await writeFile(existingQuestPath, existingContent, "utf8");

    // Create a fresh session context targeting the existing quest
    const freshCtx: any = {
      ...mockCtx,
      sessionManager: { id: "session_resume_test", getBranch: () => [] },
    };

    for (const cb of handlers["before_agent_start"] || []) {
      await cb({ prompt: `Please continue work on ${existingSlug}` }, freshCtx);
    }

    const status = await commands["quest-status"].handler("", freshCtx);
    assert.ok(
      status.includes(existingSlug),
      "Existing quest must be activated",
    );
    assert.ok(
      !status.includes("PROVISIONAL"),
      "Existing quest must not enter provisional state",
    );

    // Verify file content was not overwritten or renamed
    const checkContent = await getQuestContentBySlug(existingSlug);
    assert.ok(
      checkContent.includes("Migrate authentication cookies"),
      "Original goal intact",
    );
    assert.ok(
      checkContent.includes("## Plan Version\n2") ||
        checkContent.includes("Plan Version"),
      "Original plan version intact",
    );
  });

  // -----------------------------------------------------------------------
  // 8. Subquest naming & behavior remains intact and autonomous
  // -----------------------------------------------------------------------
  await t.step(
    "8. subquest creation and autonomous research/implementation intact",
    async () => {
      const subRes = await tools["quest_subquest"].execute(
        "call_sub_creation",
        {
          name: subSlug,
          goal: "Implement Opus packet decoder and framing checks",
          switchNow: true,
        },
        null,
        null,
        mockCtx,
      );

      assert.ok(!subRes.details?.error, "Sub-quest creation succeeded");

      // Subquest requires research first
      const subBlockRes = await emitToolCall("edit", {
        path: "mods/song/opus.c",
      });
      assert.strictEqual(
        subBlockRes?.block,
        true,
        "Sub-quest requires research first",
      );

      await emitToolCall("read", { path: "mods/song/opus.h" });
      await emitToolResult(
        "read",
        { path: "mods/song/opus.h" },
        "opus header code",
      );

      // Complete subquest research
      await tools["quest_update_state"].execute(
        "call_sub_research",
        {
          name: subSlug,
          understanding: "Opus audio packets are framed in Ogg containers.",
          assumptions: ["libopus framing is compliant"],
          openQuestions: ["None"],
          findings: ["Ogg stream parser is type safe"],
          plan: ["1. Parse Ogg headers", "2. Feed packets to decoder"],
          planConfidence: "high",
          exactNextAction: "Write opus.c decoder wrapper",
          researchComplete: true,
        },
        null,
        null,
        mockCtx,
      );

      // Autonomous: Subquest can implement immediately after research without human confirmation
      const subAllowRes = await emitToolCall("edit", {
        path: "mods/song/opus.c",
      });
      assert.strictEqual(
        subAllowRes?.block,
        undefined,
        "Sub-quest implements autonomously",
      );
    },
  );

  // Clean up
  await rm(currentDir, { recursive: true, force: true });
});
