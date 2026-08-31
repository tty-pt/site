import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_steer_followup: in-flight steering, post-compaction continuation, tool freedom, and root turn-1 confirmation", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const rootQuestSlug = "test-root-steer-quest";
	const rootQuestPath = `${currentDir}/${rootQuestSlug}.md`;
	const childQuestSlug = "test-child-steer-quest";
	const childQuestPath = `${currentDir}/${childQuestSlug}.md`;

	await rm(rootQuestPath, { force: true });
	await rm(childQuestPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
	let compactInvocationCount = 0;
	let lastCompactOptions: any = null;

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
			userMessages.push({ msg: msg?.content || msg, options, customType: msg?.customType, display: msg?.display });
		},
	};

	questJournalExtension(mockPi);

	let currentTokens = 10000;
	let currentContextWindow = 1000000; // 1M window

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
		compact: (opts: any) => {
			compactInvocationCount++;
			lastCompactOptions = opts;
		},
		ui: {
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 1. Root Quest Turn 1 initialization: requires confirmation in start prompt
	await writeFile(rootQuestPath, `# Quest: ${rootQuestSlug}\n\n## Goal\nRoot Goal\n`, "utf8");
	userMessages.length = 0;
	await commands["quest"].handler(rootQuestSlug, mockCtx);

	assert.ok(userMessages.length > 0, "Root quest initialization should send initial directive");
	const rootInitMsg = userMessages[userMessages.length - 1].msg;
	assert.ok(
		rootInitMsg.includes("ASK FOR USER CONFIRMATION") || rootInitMsg.includes("confirmation"),
		`Root quest start message must instruct agent to ask for confirmation before writing code, got: ${rootInitMsg}`,
	);

	// 2. High tool usage under healthy context does NOT trigger artificial checkpoints or turn ends
	userMessages.length = 0;
	currentTokens = 20000; // Healthy context

	// Simulate 60 tool calls / turns in one session
	for (let i = 0; i < 60; i++) {
		for (const cb of handlers["tool_call"] || []) {
			await cb({ toolName: "read", input: { path: "some_file.c" } }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "some_file.c" }, result: "code" }, mockCtx);
		}
	}

	// Under healthy context, no steer or artificial checkpoint should have been sent
	assert.strictEqual(
		userMessages.filter((m) => m.options?.deliverAs === "steer").length,
		0,
		"Healthy context must not trigger in-flight steer regardless of tool count",
	);

	// 3. Pre-compaction warning window at turn_end issues in-flight save instruction
	await commands["quest-economy"].handler("333k 30k", mockCtx);
	currentTokens = 310000; // >= 303k warning threshold
	userMessages.length = 0;

	// Simulate work in the codebase that marks the journal dirty
	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "write", input: { path: "mods/song/song.c" } }, mockCtx);
	}

	// Turn ends in warning window -> requestPreCompactionCheckpoint sends save instruction
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "write", input: { path: "mods/song/song.c" } }] }, mockCtx);
	}

	assert.ok(userMessages.length > 0, "Warning window at turn_end must trigger steer before compaction");
	const followUpEntry = userMessages[userMessages.length - 1];
	assert.strictEqual(followUpEntry.options?.deliverAs, "steer", "In-flight warning must use deliverAs: 'steer'");
	assert.ok(
		followUpEntry.msg.includes("Context compaction is imminent") ||
			followUpEntry.msg.includes("FINAL EXHAUSTIVE DURABLE STATE SAVE") ||
			followUpEntry.msg.includes("Context Compaction Warning"),
		`Steer message must instruct saving before compaction, got: ${followUpEntry.msg}`,
	);

	// 4. In-flight save by the agent: marks journal clean and ready for compaction
	await tools["quest_mark_saved"].execute("call_saved", { name: rootQuestSlug }, null, null, mockCtx);

	// 5. Context reaches compaction threshold (335k >= 333k) -> session_before_compact allows compaction and queues resumption
	currentTokens = 335000; // >= 333k threshold
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}
	userMessages.length = 0;
	let beforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		beforeCompactRes = await cb({}, mockCtx);
	}
	assert.notStrictEqual(beforeCompactRes?.cancel, true, "session_before_compact must allow compaction after verified save");

	// 6. Post-compaction continuation: verified just before compaction / on session_compact
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}

	assert.ok(userMessages.length > 0, "session_compact / session_before_compact must send post-compaction continuation");
	const postCompactEntry = userMessages[0];
	assert.ok(
		postCompactEntry.options?.deliverAs === "followUp" || !postCompactEntry.options,
		"Post-compaction continuation must be delivered to agent",
	);
	assert.ok(
		postCompactEntry.msg.includes("Post-Compaction Autonomous Resumption Directive"),
		"Continuation directive must be included",
	);

	// 7. Test deduplication: repeated calls for the same compaction must not duplicate followUp
	const followUpCount = userMessages.length;
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	assert.strictEqual(
		userMessages.length,
		followUpCount,
		"Duplicate session_compact events for same compaction count must be deduplicated",
	);

	// 8. Child Subquest execution: subquest does NOT prompt for approval
	compactInvocationCount = 0;
	currentTokens = 20000; // Subquest tokens < 60k
	const subToolRes = await tools["quest_subquest"].execute(
		"call_sub_1",
		{
			name: childQuestSlug,
			goal: "Analyze cache invalidation",
			switchNow: true,
		},
		null,
		null,
		mockCtx,
	);
	assert.strictEqual(compactInvocationCount, 0, "Subquest creation under low tokens must NOT trigger launch compaction");
	assert.ok(subToolRes.content[0].text.includes("Created sub-quest"), "Subquest creation succeeded");

	// Cleanup
	await rm(rootQuestPath, { force: true });
	await rm(childQuestPath, { force: true });
});
