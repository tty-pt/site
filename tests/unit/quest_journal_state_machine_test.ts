import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_state_machine: deterministic lifecycle, no implicit activation, verified saves only, and loop prevention", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const mainQuestSlug = "main-sm-quest";
	const mainQuestPath = `docs/current/${mainQuestSlug}.md`;
	const otherQuestSlug = "other-sm-quest";
	const otherQuestPath = `docs/current/${otherQuestSlug}.md`;

	await rm(mainQuestPath, { force: true });
	await rm(otherQuestPath, { force: true });

	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
	const userMessages: any[] = [];
	let isIdle = true;
	let compactInvocationCount = 0;

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
	let currentContextWindow = 1000000;

	const mockCtx: any = {
		cwd: "/home/quirinpa/site",
		hasUI: true,
		mode: "tui",
		isIdle: () => isIdle,
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens / currentContextWindow) * 100,
		}),
		sessionManager: {
			getBranch: () => [],
		},
		compact: () => {
			compactInvocationCount++;
		},
		ui: {
			notify: () => {},
			setStatus: () => {},
		},
	};

	// 1. Initial state: IDLE (no active quest)
	const initialStatus = await commands["quest-status"].handler("", mockCtx);
	assert.ok(initialStatus.includes("No active quest"), "Initial state should be no active quest");

	// 2. Explicit activation: ACTIVATE_QUEST
	await writeFile(mainQuestPath, `# Quest: ${mainQuestSlug}\n\n## Goal\nMain goal\n`, "utf8");
	await commands["quest"].handler(mainQuestSlug, mockCtx);

	let status = await commands["quest-status"].handler("", mockCtx);
	assert.ok(status.includes(mainQuestSlug), "Active quest must be main-sm-quest");

	// 3. Test: Writing arbitrary files under docs/current/ must NOT change active quest!
	// Simulate tool_result writing to docs/current/other-sm-quest.md
	await writeFile(otherQuestPath, `# Quest: ${otherQuestSlug}\n\n## Goal\nOther goal\n`, "utf8");

	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "write",
				input: { path: otherQuestPath, content: "arbitrary content" },
				isError: false,
			},
			mockCtx,
		);
	}

	// Active quest must remain mainQuestSlug (deterministic, no implicit filesystem side-effect mutation)
	status = await commands["quest-status"].handler("", mockCtx);
	assert.ok(
		status.includes(mainQuestSlug),
		`Active quest must remain '${mainQuestSlug}' after writing '${otherQuestPath}', got: ${status}`,
	);
	assert.strictEqual(
		status.includes(otherQuestSlug),
		false,
		`Active quest must NOT be mutated to '${otherQuestSlug}' implicitly`,
	);

	// 4. Test: tool_result on active quest verifies and marks clean
	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "write",
				input: { path: mainQuestPath, content: `# Quest: ${mainQuestSlug}\n\n## Goal\nUpdated goal\n` },
				isError: false,
			},
			mockCtx,
		);
	}

	// 5. Test: Loop prevention on turn_end
	// When state is clean, turn_end must NOT emit redundant save request messages
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ message: { role: "assistant" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(
		userMessages.length,
		0,
		"Clean state on turn_end must not emit save request or trigger runaway prompt loops",
	);

	// Clean up
	await rm(mainQuestPath, { force: true });
	await rm(otherQuestPath, { force: true });
});

Deno.test("quest_journal_state_machine: proactive pre-compaction warning, verified save gate, and compaction lifecycle (Cases A-H)", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const questSlug = "state-machine-cases-quest";
	const questFilePath = `docs/current/${questSlug}.md`;
	await rm(questFilePath, { force: true });

	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
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
	const currentContextWindow = 1000000;

	const mockCtx: any = {
		cwd: process.cwd(),
		hasUI: true,
		mode: "tui",
		isIdle: () => true,
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens / currentContextWindow) * 100,
		}),
		sessionManager: {
			id: "session_cases_a_h",
			getBranch: () => [],
		},
		compact: (opts: any) => {
			compactInvocationCount++;
			lastCompactOptions = opts;
		},
		ui: {
			notify: () => {},
			setStatus: () => {},
		},
	};

	// Initialize quest and configure economy threshold: 333k, warning margin: 30k (warns at 303k)
	await writeFile(questFilePath, `# Quest: ${questSlug}\n\n## Goal\nTest Cases A-H\n`, "utf8");
	await commands["quest"].handler(questSlug, mockCtx);
	await commands["quest-economy"].handler("333k 30k", mockCtx);

	// -----------------------------------------------------------------------
	// Case A: Normal substantive turns under healthy tokens do NOT produce synthetic messages
	// -----------------------------------------------------------------------
	for (let i = 0; i < 3; i++) {
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
		}
	}
	// Verify NO incremental checkpoint was requested
	const incMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Quest Incremental Checkpoint"),
	);
	assert.strictEqual(incMsgs.length, 0, "No incremental checkpoint during normal execution");

	// -----------------------------------------------------------------------
	// Case C: Now tokens reach 303k (warning threshold) while quest is dirty -> sends final save instruction
	// -----------------------------------------------------------------------
	currentTokens = 303000;
	userMessages.length = 0;

	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
	}

	// Case C assertion: Explicit save instruction issued at turn boundary in warning window
	const finalSaveMsgsC = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Context compaction is imminent") ||
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("FINAL EXHAUSTIVE DURABLE STATE SAVE"),
	);
	assert.strictEqual(finalSaveMsgsC.length, 1, "Case C: Final save instruction must be sent when entering warning window");

	// -----------------------------------------------------------------------
	// Case B: Subsequent turns in the warning window below 333k do NOT send duplicate messages
	// -----------------------------------------------------------------------
	currentTokens = 315000;
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 0, "Case B: Repeated turn in warning window must not send duplicate messages");

	// -----------------------------------------------------------------------
	// Case D: Compaction threshold reached (>= 333k) while dirty -> session_before_compact cancels as a safety gate
	// -----------------------------------------------------------------------
	currentTokens = 334000;
	userMessages.length = 0;
	let beforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		beforeCompactRes = await cb({}, mockCtx);
	}
	assert.strictEqual(beforeCompactRes?.cancel, true, "Case D: Compaction must cancel when dirty");
	assert.strictEqual(userMessages.length, 0, "Case D: session_before_compact must NOT send prompts from inside the hook");

	// -----------------------------------------------------------------------
	// Case E: The deep save succeeds and quest_mark_saved verifies the file
	// -----------------------------------------------------------------------
	await tools["quest_mark_saved"].execute("save_1", {}, null, null, mockCtx);
	userMessages.length = 0;

	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}

	// When clean, Pi's native auto-compaction safety gate allows compaction
	let cleanBeforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		cleanBeforeCompactRes = await cb({}, mockCtx);
	}
	assert.notStrictEqual(cleanBeforeCompactRes?.cancel, true, "Case E: session_before_compact must allow compaction when clean");

	// -----------------------------------------------------------------------
	// Case F: Actual threshold reached and quest is dirty -> session_before_compact cancels as a safety gate
	// -----------------------------------------------------------------------
	// Reset compaction state by simulating session_compact
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	// Mark dirty
	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "edit", input: { path: "mods/gig/gig.c" } }, mockCtx);
	}
	userMessages.length = 0;
	let beforeCompactResult: any = null;
	for (const cb of handlers["session_before_compact"] || []) {
		beforeCompactResult = await cb({}, mockCtx);
	}
	assert.strictEqual(beforeCompactResult?.cancel, true, "Case F: session_before_compact must cancel when dirty");
	assert.strictEqual(userMessages.length, 0, "Case F: session_before_compact must NOT send prompts from inside the hook");

	// -----------------------------------------------------------------------
	// Case G & H: Compaction succeeds (session_compact fires)
	// -----------------------------------------------------------------------
	// Save first so compaction can succeed
	await tools["quest_mark_saved"].execute("save_2", {}, null, null, mockCtx);
	userMessages.length = 0;

	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}

	// Case H: Post-compaction resume directive sent
	assert.strictEqual(userMessages.length, 1, "Case H: session_compact must send resume prompt");
	const resumeMsg = typeof userMessages[0].msg === "string" ? userMessages[0].msg : userMessages[0].msg?.text || "";
	assert.ok(userMessages[0].options?.deliverAs === "followUp" || !userMessages[0].options);
	assert.ok(resumeMsg.includes("Post-Compaction Autonomous Resumption Directive"), "Case H: Resume prompt must be sent");
	assert.ok(resumeMsg.includes(`docs/current/${questSlug}.md`), "Case H: Must reference active quest path");

	// -----------------------------------------------------------------------
	// Case I: Session reconstruction resets in-flight preCompactionSaveRequestPending to false
	// -----------------------------------------------------------------------
	const branchEntries: any[] = [
		{
			type: "custom",
			customType: "quest_journal",
			data: {
				active: questSlug,
				saveCount: 2,
				compactCount: 1,
				prompts: ["Test Cases A-H"],
				refinements: [],
				stack: [questSlug],
				dirty: true,
				preCompactionCheckpointPending: true,
				preCompactionSaveRequestPending: true, // Persisted as true before crash/interruption
				saveGeneration: null,
				economyTokens: 333000,
				warningMarginTokens: 30000,
			},
		},
	];

	mockCtx.sessionManager.getBranch = () => branchEntries;

	// Reconstruct session state (e.g. session_start event)
	for (const cb of handlers["session_start"] || []) {
		await cb({}, mockCtx);
	}

	// Verify that in-flight and warning flags were reset to false upon reconstruction
	userMessages.length = 0;
	let restartCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		restartCompactRes = await cb({}, mockCtx);
	}
	assert.strictEqual(restartCompactRes?.cancel, true, "Case I: session_before_compact must block dirty compaction after reconstruction");
	assert.strictEqual(userMessages.length, 0, "Case I: session_before_compact must not submit prompts");

	// Now verify recovery: turn_end in warning window safely queues ONE new save follow-up without deadlock
	currentTokens = 310000;
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}
	const recoveredSaveMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Context compaction is imminent") ||
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("FINAL EXHAUSTIVE DURABLE STATE SAVE"),
	);
	assert.strictEqual(recoveredSaveMsgs.length, 1, "Case I: turn_end must queue a new save follow-up after reconstruction");

	// -----------------------------------------------------------------------
	// Case J: sendMessage failure rolls back pending state so it can be retried without deadlock
	// -----------------------------------------------------------------------
	const originalSendMessage = mockPi.sendMessage;
	mockPi.sendMessage = () => {
		throw new Error("Simulated transport failure");
	};

	// Mark dirty and reset warning state
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "edit", input: { path: "mods/gig/gig.c" } }, mockCtx);
	}
	userMessages.length = 0;

	// Turn end runs with failed sendMessage -> must roll back and not leave state pending
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 0, "Case J: No message should be recorded on sendMessage failure");

	// Restore working sendMessage
	mockPi.sendMessage = originalSendMessage;

	// On next turn_end, it successfully retries and queues the save message!
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}
	const retrySaveMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Context compaction is imminent") ||
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("FINAL EXHAUSTIVE DURABLE STATE SAVE"),
	);
	assert.strictEqual(retrySaveMsgs.length, 1, "Case J: turn_end must successfully retry queueing save after previous sendMessage failure");

	// Clean up
	await rm(questFilePath, { force: true });
	await new Promise((resolve) => setTimeout(resolve, 80));
});
