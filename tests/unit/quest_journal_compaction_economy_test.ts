import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_compaction_dynamics: dynamic economy, subquest launch, mid-subquest, and auto-resume", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const parentQuestPath = "docs/current/parent-compaction-quest.md";
	const subQuestPath = "docs/current/sub-compaction-quest.md";
	await rm(parentQuestPath, { force: true });
	await rm(subQuestPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	let userMessages: any[] = [];
	let notifiedMessages: string[] = [];
	let compactCalled = false;
	let compactOptions: any = null;

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
		sendUserMessage(msg: any) {
			userMessages.push(msg);
		},
	};

	questJournalExtension(mockPi);

	let currentTokens = 50000;
	let currentContextWindow = 1000000; // 1M context window

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens / currentContextWindow) * 100,
		}),
		sessionManager: {
			getBranch: () => [],
		},
		compact: (options: any) => {
			compactCalled = true;
			compactOptions = options;
		},
		ui: {
			notify(msg: string) {
				notifiedMessages.push(msg);
			},
			setWidget() {},
			setStatus() {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 1. Initialize root/parent quest
	await writeFile(parentQuestPath, "# Quest: parent-compaction-quest\n\n## Goal\nRoot goal\n", "utf8");
	await commands["quest"].handler("parent-compaction-quest", mockCtx);

	// 2. Test Sub-Quest start:
	// Sub-quest creation should NOT trigger premature launch compaction (preventing agent interruption)
	currentTokens = 50000;
	compactCalled = false;
	compactOptions = null;

	await tools["quest_subquest"].execute(
		"call_subquest_1",
		{
			goal: "Refactor database engine",
			name: "sub-compaction-quest",
			switchNow: true,
		},
		null,
		null,
		mockCtx,
	);

	assert.strictEqual(compactCalled, false, "Sub-quest creation should NOT trigger premature launch compaction");

	// 4. Test Mid-Subquest compaction when tokens reach dynamic threshold
	// Active quest is now sub-compaction-quest (inside LIFO stack: [parent-compaction-quest, sub-compaction-quest])
	// Set threshold to 333k explicitly for this test with 30k warning margin
	await commands["quest-economy"].handler("333k 30k", mockCtx);
	currentTokens = 310000; // within 30k warning margin of 333k
	userMessages = [];

	// Simulate session_compact to clear save gate (save pending)
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	userMessages = []; // Clear post-compaction resume prompt so we isolate turn_end warnings

	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}

	assert.ok(userMessages.length > 0, "Should send pre-compaction warning when within margin of threshold");
	const warnMsg = typeof userMessages[0] === "string" ? userMessages[0] : (userMessages[0].text || "");
	assert.ok(
		warnMsg.includes("PRE-COMPACTION EXHAUSTIVE CONTEXT PRESERVATION PROTOCOL") || warnMsg.includes("AUTO-COMPACTION WILL OCCUR SOON"),
		`Warning message should alert model to preserve context, got: ${warnMsg}`,
	);

	// Test deduplication: subsequent turn_end without save should not duplicate the deep save message
	const msgCountBefore = userMessages.length;
	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}
	assert.strictEqual(userMessages.length, msgCountBefore, "Subsequent turn_end in same warning window should not spam duplicate warnings");

	// Mark saved
	compactCalled = false;
	compactOptions = null;
	await tools["quest_mark_saved"].execute("call_saved_mid", {}, null, null, mockCtx);
	await new Promise((resolve) => setTimeout(resolve, 60));

	assert.strictEqual(compactCalled, true, "Mid-subquest save should trigger compaction");
	assert.ok(
		compactOptions && compactOptions.customInstructions && compactOptions.customInstructions.includes("sub-compaction-quest"),
		`Mid-subquest compaction instructions should name active sub-quest, got: ${compactOptions?.customInstructions}`,
	);

	// 5. Test Post-Compaction Resumption Prompt on session_compact
	userMessages = [];
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}

	assert.ok(userMessages.length > 0, "session_compact should send immediate post-compaction resumption directive to agent");
	const postCompactMsg = typeof userMessages[0] === "string" ? userMessages[0] : (Array.isArray(userMessages[0]) ? userMessages[0][0].text : userMessages[0].text || "");
	assert.ok(
		postCompactMsg.includes("Post-Compaction Autonomous Resumption Directive"),
		`Post-compaction message should have clear directive, got: ${postCompactMsg}`,
	);
	assert.ok(
		postCompactMsg.includes("docs/current/small-subquest.md") || postCompactMsg.includes("docs/current/sub-compaction-quest.md"),
		`Post-compaction message should reference active quest file, got: ${postCompactMsg}`,
	);

	// 6. Test CRB Provider contributions
	const g = globalThis as any;
	assert.ok(g.__pi_crb_providers && g.__pi_crb_providers.length > 0, "CRB provider hook should be registered");
	const crbRules: string[] = [];
	for (const p of g.__pi_crb_providers) {
		const res = p(mockCtx, ["quest_mark_saved", "edit", "read"]);
		if (Array.isArray(res)) crbRules.push(...res);
	}
	assert.ok(
		crbRules.some((r) => r.toLowerCase().includes("single source of truth")),
		"CRB rules should reinforce quest file as single source of truth",
	);
	assert.ok(
		crbRules.some((r) => r.toLowerCase().includes("zero re-research")),
		"CRB rules should enforce zero re-research",
	);

	// 7. Test Autonomous Post-Compaction Resumption in System Prompt
	let systemPrompt = "Base system prompt.";
	for (const cb of handlers["before_agent_start"] || []) {
		const res = await cb({ systemPrompt }, mockCtx);
		if (res?.systemPrompt) systemPrompt = res.systemPrompt;
	}

	assert.ok(
		systemPrompt.toLowerCase().includes("resume") || systemPrompt.toLowerCase().includes("autonomously"),
		"System prompt should mandate autonomous resumption after compaction",
	);

	// 8. Test session_before_compact when compaction is NOT ready (save pending)
	// Clear save gate by simulating session_compact
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	userMessages = [];

	// Now try to compact via session_before_compact: should cancel AND send deep save request
	for (const cb of handlers["session_before_compact"] || []) {
		const result = await cb({}, mockCtx);
		assert.strictEqual(result?.cancel, true, "session_before_compact should cancel when save is pending");
	}
	assert.ok(userMessages.length > 0, "session_before_compact should alert model to dump context when cancelling");
	const cancelWarn = typeof userMessages[0] === "string" ? userMessages[0] : (userMessages[0].text || "");
	assert.ok(
		cancelWarn.includes("compaction requested while quest file") || cancelWarn.includes("PRE-COMPACTION EXHAUSTIVE CONTEXT PRESERVATION"),
		`Cancel warning should notify about unsaved changes, got: ${cancelWarn}`,
	);

	// Clean up
	await rm(parentQuestPath, { force: true });
	await rm(subQuestPath, { force: true });
});
