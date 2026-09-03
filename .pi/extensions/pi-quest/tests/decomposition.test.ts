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

Deno.test("quest_journal_decomposition: structural workstream decomposition & sub-quest lifecycle", async (t) => {
  const currentDir = ".pi/quest/current";
  const archiveDir = ".pi/quest/archive";
  await mkdir(currentDir, { recursive: true });
  await mkdir(archiveDir, { recursive: true });

  const rootSlug = "repair-authentication-pipeline";
  const child1Slug = "auth-callback-flow";
  const child2Slug = "session-persistence";
  const child3Slug = "frontend-auth-state";
  const simpleSlug = "update-header-cache-ttl";

  const cleanAll = async () => {
    await rm(currentDir, { recursive: true, force: true });
    await rm(archiveDir, { recursive: true, force: true });
  };

  await cleanAll();

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
      id: "session_decomposition_test",
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

  // -----------------------------------------------------------------------
  // 1. Task with two independent subsystems creates useful sub-quests (< 4 phases)
  // -----------------------------------------------------------------------
  await t.step(
    "1. task with two independent subsystems creates sub-quests even with <4 phases",
    async () => {
      // Root prompt arrives
      for (const cb of handlers["before_agent_start"] || []) {
        await cb({
          prompt:
            "Refactor auth: overhaul callback verification and session token persistence in DB",
          systemPrompt: "Base.",
        }, mockCtx);
      }

      await emitToolCall("read", { path: "mods/auth/callback.c" });
      await emitToolResult(
        "read",
        { path: "mods/auth/callback.c" },
        "auth callback code",
      );

      // Initialize root quest with coordination plan referencing the 2 sub-quests
      await tools["quest_update_state"].execute(
        "call_root_init",
        {
          name: rootSlug,
          goal:
            "Overhaul callback verification and session persistence subsystems",
          status: "Research complete",
          understanding:
            "Authentication pipeline has two decoupled subsystems: OAuth callback verification (mods/auth/callback.c) and session token persistence (mods/auth/session.c).",
          assumptions: [
            "- Callback verification can be tested with mock OAuth identity provider",
            "- Session table schema supports token rotation without migration",
          ],
          openQuestions: ["None"],
          findings: [
            "Callback verification logic is isolated to mods/auth/callback.c.",
            "Session persistence is handled independently in mods/auth/session.c.",
          ],
          plan: [
            "1. Investigate and overhaul [[auth-callback-flow]]",
            "2. Investigate and overhaul [[session-persistence]]",
            "3. Integrate changes and run end-to-end auth suite",
          ],
          planConfidence: "high",
          exactNextAction:
            "Create sub-quests for callback flow and session persistence",
          researchComplete: true,
        },
        null,
        null,
        mockCtx,
      );

      // Pre-create the two sub-quests without switching away from root quest
      const sub1Res = await tools["quest_subquest"].execute(
        "call_sub1",
        {
          name: child1Slug,
          goal: "Overhaul OAuth callback verification and PKCE validation",
          switchNow: false,
        },
        null,
        null,
        mockCtx,
      );
      assert.strictEqual(
        sub1Res.details?.switched,
        false,
        "Sub-quest 1 created with switchNow: false",
      );

      const sub2Res = await tools["quest_subquest"].execute(
        "call_sub2",
        {
          name: child2Slug,
          goal: "Implement encrypted token storage and rotation in session DB",
          switchNow: false,
        },
        null,
        null,
        mockCtx,
      );
      assert.strictEqual(
        sub2Res.details?.switched,
        false,
        "Sub-quest 2 created with switchNow: false",
      );

      // Verify root quest links both sub-quests under ## Sub-Quests
      const rootContent = await getQuestContentBySlug(rootSlug);
      assert.ok(
        rootContent.includes(`[[${child1Slug}]]`),
        "Root links child 1",
      );
      assert.ok(
        rootContent.includes(`[[${child2Slug}]]`),
        "Root links child 2",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 2. Simple multi-step task remains a single quest without artificial fragmentation
  // -----------------------------------------------------------------------
  await t.step(
    "2. simple multi-step task remains a single quest without artificial sub-quests",
    async () => {
      const simpleCtx = {
        ...mockCtx,
        sessionManager: { id: "session_simple_test", getBranch: () => [] },
      };

      // Trivial 2-step task: update cache header constant and add helper
      for (const cb of handlers["before_agent_start"] || []) {
        await cb({
          prompt:
            "Change default static asset Cache-Control max-age to 86400 seconds in mods/core/http.c",
          systemPrompt: "Base.",
        }, simpleCtx);
      }

      for (const cb of handlers["tool_call"] || []) {
        await cb(
          { toolName: "read", input: { path: "mods/core/http.c" } },
          simpleCtx,
        );
      }

      await tools["quest_update_state"].execute(
        "call_simple_init",
        {
          name: simpleSlug,
          goal:
            "Update Cache-Control max-age constant and verify response headers",
          status: "Research complete",
          understanding:
            "Single constant HTTP_STATIC_MAX_AGE in mods/core/http.c controls cache headers.",
          assumptions: ["All static routes respect HTTP_STATIC_MAX_AGE"],
          openQuestions: ["None"],
          findings: ["Found HTTP_STATIC_MAX_AGE = 3600 in mods/core/http.c:42"],
          plan: [
            "1. Update HTTP_STATIC_MAX_AGE to 86400 in mods/core/http.c",
            "2. Run tests/unit/http_header_test.c",
          ],
          planConfidence: "high",
          exactNextAction: "Ask user confirmation to update constant",
          researchComplete: true,
        },
        null,
        null,
        simpleCtx,
      );

      const simpleContent = await getQuestContentBySlug(simpleSlug);
      assert.ok(
        !simpleContent.includes("## Sub-Quests\n- [ ]"),
        "Simple task has no artificial sub-quests",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 3. Complex task with three independent workstreams creates 3 sub-quests
  // -----------------------------------------------------------------------
  await t.step(
    "3. complex task creates 3 meaningful sub-quests for separable concerns",
    async () => {
      const sub3Res = await tools["quest_subquest"].execute(
        "call_sub3",
        {
          name: child3Slug,
          goal: "Update client-side auth state hooks and SSR token hydration",
          switchNow: false,
        },
        null,
        null,
        mockCtx,
      );

      assert.strictEqual(sub3Res.details?.switched, false);

      const rootContent = await getQuestContentBySlug(rootSlug);
      assert.ok(
        rootContent.includes(`[[${child1Slug}]]`),
        "Root contains child 1",
      );
      assert.ok(
        rootContent.includes(`[[${child2Slug}]]`),
        "Root contains child 2",
      );
      assert.ok(
        rootContent.includes(`[[${child3Slug}]]`),
        "Root contains child 3",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 4. Parent plan references sub-quests rather than duplicating all child reasoning
  // -----------------------------------------------------------------------
  await t.step(
    "4. parent plan references sub-quests rather than duplicating child reasoning",
    async () => {
      const rootContent = await getQuestContentBySlug(rootSlug);
      assert.ok(
        rootContent.includes(
          "1. Investigate and overhaul [[auth-callback-flow]]",
        ),
        "Plan references [[auth-callback-flow]]",
      );
      assert.ok(
        rootContent.includes(
          "2. Investigate and overhaul [[session-persistence]]",
        ),
        "Plan references [[session-persistence]]",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 5. Root confirmation behavior remains intact
  // -----------------------------------------------------------------------
  await t.step(
    "5. root quest requires user confirmation before implementation",
    async () => {
      const blockRes = await emitToolCall("edit", { path: "mods/auth/auth.c" });
      assert.strictEqual(
        blockRes?.block,
        true,
        "Root quest requires user confirmation before implementation",
      );
      assert.ok(
        blockRes.reason.includes("CONFIRMATION_PENDING"),
        "Reason is CONFIRMATION_PENDING",
      );

      // User confirms root plan
      for (const cb of handlers["before_agent_start"] || []) {
        await cb({
          prompt:
            "The plan and sub-quests look great, proceed with implementation!",
        }, mockCtx);
      }

      const allowRes = await emitToolCall("edit", { path: "mods/auth/auth.c" });
      assert.strictEqual(
        allowRes?.block,
        undefined,
        "Root implementation unblocked after user confirmation",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 6. Child quests retain their own independent research/reassessment state
  // -----------------------------------------------------------------------
  await t.step(
    "6. child quests retain their own independent research/reassessment state",
    async () => {
      // Switch to child 1
      await tools["quest_subquest"].execute(
        "call_switch_child1",
        {
          name: child1Slug,
          goal: "Overhaul OAuth callback verification and PKCE validation",
          switchNow: true,
        },
        null,
        null,
        mockCtx,
      );

      const status = await commands["quest-status"].handler("", mockCtx);
      assert.ok(status.includes(child1Slug), "Child 1 is now active");
      assert.ok(
        status.includes("d2"),
        "Hierarchy depth is 2 (parent -> child 1)",
      );

      // Child 1 starts in researchRequired = true
      const blockRes = await emitToolCall("edit", {
        path: "mods/auth/callback.c",
      });
      assert.strictEqual(
        blockRes?.block,
        true,
        "Child 1 requires research before implementation",
      );

      // Complete Child 1 research
      await tools["quest_update_state"].execute(
        "call_child1_research",
        {
          name: child1Slug,
          goal: "Overhaul OAuth callback verification and PKCE validation",
          status: "Research complete",
          understanding:
            "PKCE code_verifier is passed via axil query string and hashed with SHA-256.",
          assumptions: ["SHA-256 EVP digest is thread-safe in libcrypto"],
          openQuestions: ["None"],
          findings: [
            "PKCE validation can reuse existing axil sha256 buffer helper",
          ],
          plan: [
            "1. Add PKCE verification helper in mods/auth/callback.c",
            "2. Run callback unit tests",
          ],
          planConfidence: "high",
          exactNextAction:
            "Implement PKCE verification helper in mods/auth/callback.c",
          researchComplete: true,
        },
        null,
        null,
        mockCtx,
      );

      const child1Content = await getQuestContentBySlug(child1Slug);
      assert.ok(
        child1Content.includes(
          "PKCE validation can reuse existing axil sha256 buffer helper",
        ),
        "Child 1 has independent findings",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 7. Child completion returns a concise result to the parent
  // -----------------------------------------------------------------------
  await t.step(
    "7. child completion (archive) returns concise result to parent",
    async () => {
      // Update child 1 with decisions made and files touched before archiving
      await tools["quest_update_state"].execute(
        "call_child1_finish",
        {
          name: child1Slug,
          status: "Complete",
          decisions: [
            "Enforced strict constant-time comparison for PKCE code verifier hashes",
          ],
          filesTouched: [
            "mods/auth/callback.c",
            "tests/unit/auth_callback_test.c",
          ],
          exactNextAction: "Archive sub-quest and return to parent",
        },
        null,
        null,
        mockCtx,
      );

      userMessages.length = 0;

      // Archive child 1
      const archiveRes = await tools["quest_archive"].execute(
        "call_archive_child1",
        { name: child1Slug },
        null,
        null,
        mockCtx,
      );

      assert.ok(!archiveRes.details?.error, "Child 1 archived successfully");
      assert.strictEqual(
        archiveRes.details?.nextActive,
        rootSlug,
        "Active quest restored to parent",
      );

      // Check directive sent to parent
      const directive = userMessages.find((m) =>
        (typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes(
          "Sub-Quest 'auth-callback-flow' Completed",
        )
      );
      assert.ok(directive, "Parent evaluation directive dispatched");
      const directiveText = typeof directive.msg === "string"
        ? directive.msg
        : directive.msg?.text || "";
      assert.ok(
        directiveText.includes("constant-time comparison"),
        "Includes child 1 decisions",
      );
      assert.ok(
        directiveText.includes("mods/auth/callback.c"),
        "Includes child 1 files touched",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 8. Parent execution resumes with child findings recorded
  // -----------------------------------------------------------------------
  await t.step(
    "8. parent execution resumes and child 1 is marked completed",
    async () => {
      const rootContent = await getQuestContentBySlug(rootSlug);
      // Child 1 link in parent marked completed - [x] [[auth-callback-flow]]
      assert.ok(
        rootContent.includes(`- [x] [[${child1Slug}]]`),
        "Child 1 marked completed in parent ## Sub-Quests",
      );

      // Status reports parent as active
      const status = await commands["quest-status"].handler("", mockCtx);
      assert.ok(
        status.includes(rootSlug.slice(0, 20)) ||
          status.includes("repair-authentication"),
        "Parent quest is active again",
      );

      // Implementation in parent is unblocked
      const allowRes = await emitToolCall("edit", { path: "mods/auth/auth.c" });
      assert.strictEqual(
        allowRes?.block,
        undefined,
        "Parent resumes execution unblocked",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 9. Direct implementation remains blocked until each child satisfies its own research gate
  // -----------------------------------------------------------------------
  await t.step(
    "9. child 2 is blocked until its own research gate is satisfied",
    async () => {
      // Switch to child 2
      await tools["quest_subquest"].execute(
        "call_switch_child2",
        {
          name: child2Slug,
          goal: "Implement encrypted token storage and rotation in session DB",
          switchNow: true,
        },
        null,
        null,
        mockCtx,
      );

      // Child 2 should be blocked before research
      const blockRes = await emitToolCall("edit", {
        path: "mods/auth/session.c",
      });
      assert.strictEqual(
        blockRes?.block,
        true,
        "Child 2 cannot implement before its own research is complete",
      );
      assert.ok(
        blockRes.reason.includes("RESEARCH_PENDING"),
        "Reason must be RESEARCH_PENDING",
      );
    },
  );

  // -----------------------------------------------------------------------
  // 10. Sub-quest creation remains autonomous without redundant confirmation
  // -----------------------------------------------------------------------
  await t.step(
    "10. sub-quest executes autonomously once research is complete without user confirmation",
    async () => {
      await emitToolCall("read", { path: "mods/auth/session.h" });
      await emitToolResult(
        "read",
        { path: "mods/auth/session.h" },
        "session header code",
      );
      // Complete Child 2 research
      await tools["quest_update_state"].execute(
        "call_child2_research",
        {
          name: child2Slug,
          goal: "Implement encrypted token storage and rotation in session DB",
          status: "Research complete",
          understanding:
            "Session table uses AES-256-GCM encryption with HMAC authentication.",
          assumptions: [
            "Session encryption key is present in environment config",
          ],
          openQuestions: ["None"],
          findings: [
            "Session store interface is defined in mods/auth/session.h",
          ],
          plan: [
            "1. Add token encryption routines to mods/auth/session.c",
            "2. Run tests/unit/auth_session_test.c",
          ],
          planConfidence: "high",
          exactNextAction:
            "Implement token encryption routines in mods/auth/session.c",
          researchComplete: true,
        },
        null,
        null,
        mockCtx,
      );

      // Child 2 can edit code immediately without waiting for human confirmation
      const allowRes = await emitToolCall("edit", {
        path: "mods/auth/session.c",
      });
      assert.strictEqual(
        allowRes?.block,
        undefined,
        "Child sub-quest implements autonomously without user confirmation",
      );
    },
  );

  // Clean up
  await cleanAll();
});
