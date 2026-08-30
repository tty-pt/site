import assert from "node:assert";
import { mkdir, rm } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_ask_questions_confirmation: ask_questions tool result confirmation lifecycle", async (t) => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const rootSlug = "refactor-stream-ring-buffer";
	const rootQuestPath = `docs/current/${rootSlug}.md`;
	await rm(rootQuestPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];

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
		cwd: process.cwd(),
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens / currentContextWindow) * 100,
		}),
		sessionManager: {
			id: "session_ask_questions_test",
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

	const emitToolResult = async (toolName: string, input?: any, content?: any[], details?: any, isError = false) => {
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName, input, content, details, isError }, mockCtx);
		}
	};

	// -----------------------------------------------------------------------
	// 1. Initialize root quest and complete research -> awaitingUserConfirmation = true
	// -----------------------------------------------------------------------
	await t.step("1. root quest completes research and enters awaitingUserConfirmation state", async () => {
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Refactor audio streaming buffer and implement lockless ring buffer in player.c", systemPrompt: "Base." }, mockCtx);
		}

		await tools["quest_update_state"].execute(
			"call_root_init",
			{
				name: rootSlug,
				goal: "Refactor streaming buffer and implement lockless ring buffer",
				status: "Research complete",
				understanding: "Audio player uses ring buffer with atomic head/tail pointers in mods/song/player.c.",
				assumptions: [
					"- 64KB power-of-two buffer size allows bitwise mask wrapping",
					"- Memory order acquire/release is sufficient for single-producer single-consumer thread model",
				],
				openQuestions: ["None"],
				findings: [
					"Single producer thread feeds buffer; single consumer thread renders audio.",
					"Lockless ring buffer eliminates audio glitching under high CPU load.",
				],
				plan: [
					"1. Implement lockless ring buffer in mods/song/player.c",
					"2. Add thread-safety verification test in tests/unit/ring_buffer_test.c",
				],
				planConfidence: "high",
				exactNextAction: "Ask user for confirmation via ask_questions",
				researchComplete: true,
			},
			null,
			null,
			mockCtx,
		);

		// Implementation must be blocked awaiting user confirmation
		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Must block edit before confirmation");
		assert.strictEqual(blockRes.block, true, "Edit tool call is blocked");
		assert.ok(blockRes.reason.includes("CONFIRMATION_PENDING"), "Reason must be CONFIRMATION_PENDING");
	});

	// -----------------------------------------------------------------------
	// 2. Non-confirmation ask_questions call does NOT unlock implementation
	// -----------------------------------------------------------------------
	await t.step("2. non-confirmation ask_questions answer does NOT clear confirmation gate", async () => {
		const nonConfirmEvent = {
			toolName: "ask_questions",
			input: {
				questions: [
					{
						header: "Audio Format Preference",
						question: "Which audio output format would you prefer for default test benchmarks?",
						options: [
							{ label: "FLAC 24-bit 96kHz", description: "High resolution audio format" },
							{ label: "PCM 16-bit 44.1kHz", description: "Standard CD quality" },
						],
					},
				],
			},
			content: [
				{
					type: "text",
					text: "User answers:\n1. Question: Which audio output format would you prefer for default test benchmarks?\n   Answer: FLAC 24-bit 96kHz",
				},
			],
			details: {
				status: "answered",
				questions: [
					{
						header: "Audio Format Preference",
						question: "Which audio output format would you prefer for default test benchmarks?",
						options: ["FLAC 24-bit 96kHz", "PCM 16-bit 44.1kHz"],
					},
				],
				answers: [
					{
						questionIndex: 0,
						header: "Audio Format Preference",
						question: "Which audio output format would you prefer for default test benchmarks?",
						answer: "FLAC 24-bit 96kHz",
						wasCustom: false,
						optionIndex: 0,
					},
				],
			},
			isError: false,
		};

		await emitToolResult("ask_questions", nonConfirmEvent.input, nonConfirmEvent.content, nonConfirmEvent.details, false);

		// Implementation must STILL be blocked
		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Non-confirmation ask_questions must NOT unlock edit");
		assert.strictEqual(blockRes.block, true);
	});

	// -----------------------------------------------------------------------
	// 3. Negative answer to confirmation question does NOT unlock implementation
	// -----------------------------------------------------------------------
	await t.step("3. negative answer to confirmation question leaves implementation blocked", async () => {
		const negativeEvent = {
			toolName: "ask_questions",
			input: {
				questions: [
					{
						header: "Confirmation",
						question: "Would you like to proceed with implementing the lockless ring buffer in player.c?",
						options: [
							{ label: "Yes, proceed with implementation", description: "Start implementing the changes" },
							{ label: "No, let me review the plan first", description: "Hold off on changes" },
						],
					},
				],
			},
			content: [
				{
					type: "text",
					text: "User answers:\n1. Question: Would you like to proceed with implementing the lockless ring buffer in player.c?\n   Answer: No, let me review the plan first",
				},
			],
			details: {
				status: "answered",
				questions: [
					{
						header: "Confirmation",
						question: "Would you like to proceed with implementing the lockless ring buffer in player.c?",
						options: ["Yes, proceed with implementation", "No, let me review the plan first"],
					},
				],
				answers: [
					{
						questionIndex: 0,
						header: "Confirmation",
						question: "Would you like to proceed with implementing the lockless ring buffer in player.c?",
						answer: "No, let me review the plan first",
						wasCustom: false,
						optionIndex: 1,
					},
				],
			},
			isError: false,
		};

		await emitToolResult("ask_questions", negativeEvent.input, negativeEvent.content, negativeEvent.details, false);

		// Implementation must STILL be blocked
		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Negative answer must NOT unlock edit");
		assert.strictEqual(blockRes.block, true);
	});

	// -----------------------------------------------------------------------
	// 4. Affirmative answer to ask_questions immediately unlocks edit in the same turn
	// -----------------------------------------------------------------------
	await t.step("4. affirmative answer to ask_questions immediately unlocks edit before next tool call", async () => {
		const affirmativeEvent = {
			toolName: "ask_questions",
			input: {
				questions: [
					{
						header: "Confirmation",
						question: "Would you like to proceed with implementing the lockless ring buffer in player.c?",
						options: [
							{ label: "Yes, proceed with implementation", description: "Start implementing the changes" },
							{ label: "No, let me review the plan first", description: "Hold off on changes" },
						],
					},
				],
			},
			content: [
				{
					type: "text",
					text: "User answers:\n1. Question: Would you like to proceed with implementing the lockless ring buffer in player.c?\n   Answer: Yes, proceed with implementation",
				},
			],
			details: {
				status: "answered",
				questions: [
					{
						header: "Confirmation",
						question: "Would you like to proceed with implementing the lockless ring buffer in player.c?",
						options: ["Yes, proceed with implementation", "No, let me review the plan first"],
					},
				],
				answers: [
					{
						questionIndex: 0,
						header: "Confirmation",
						question: "Would you like to proceed with implementing the lockless ring buffer in player.c?",
						answer: "Yes, proceed with implementation",
						wasCustom: false,
						optionIndex: 0,
					},
				],
			},
			isError: false,
		};

		// 1. Process the tool result
		await emitToolResult("ask_questions", affirmativeEvent.input, affirmativeEvent.content, affirmativeEvent.details, false);

		// 2. Immediately issue an edit tool call
		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Edit tool call must be allowed immediately following affirmative ask_questions answer");
	});

	// -----------------------------------------------------------------------
	// 5. Varied affirmative phrases in ask_questions work reliably
	// -----------------------------------------------------------------------
	await t.step("5. varied affirmative answers (e.g. 'Yes, proceed with finalization and verification') are accepted", async () => {
		// Reset to unconfirmed for testing varied phrases
		const anotherSlug = "another-test-quest";
		const anotherPath = `docs/current/${anotherSlug}.md`;
		await tools["quest_update_state"].execute(
			"call_another_init",
			{
				name: anotherSlug,
				goal: "Another test quest",
				status: "Research complete",
				understanding: "Valid understanding.",
				assumptions: ["Valid assumption."],
				openQuestions: ["None"],
				findings: ["Valid finding."],
				plan: ["1. Do step 1"],
				planConfidence: "high",
				exactNextAction: "Ask confirmation",
				researchComplete: true,
			},
			null,
			null,
			mockCtx,
		);

		// Confirm blocked initially
		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(blockRes?.block, true);

		// User selects "Yes, proceed with finalization and verification"
		const variedEvent = {
			toolName: "ask_questions",
			input: {
				questions: [
					{
						header: "Implementation Approval",
						question: "Should I proceed with the proposed plan?",
						options: [
							{ label: "Yes, proceed with finalization and verification" },
						],
					},
				],
			},
			content: [
				{
					type: "text",
					text: "User answers:\n1. Question: Should I proceed with the proposed plan?\n   Answer: Yes, proceed with finalization and verification",
				},
			],
			details: {
				status: "answered",
				questions: [
					{
						header: "Implementation Approval",
						question: "Should I proceed with the proposed plan?",
						options: ["Yes, proceed with finalization and verification"],
					},
				],
				answers: [
					{
						questionIndex: 0,
						header: "Implementation Approval",
						question: "Should I proceed with the proposed plan?",
						answer: "Yes, proceed with finalization and verification",
						wasCustom: false,
						optionIndex: 0,
					},
				],
			},
			isError: false,
		};

		await emitToolResult("ask_questions", variedEvent.input, variedEvent.content, variedEvent.details, false);

		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Varied affirmative phrase unlocks edit");

		await rm(anotherPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// 6. Refinement answer via ask_questions triggers reassessment
	// -----------------------------------------------------------------------
	await t.step("6. refinement answer via ask_questions triggers reassessment and blocks implementation", async () => {
		const refinementEvent = {
			toolName: "ask_questions",
			input: {
				questions: [
					{
						header: "Confirmation",
						question: "Would you like to proceed?",
						options: [
							{ label: "Yes, but also add support for FLAC stream chunking in player.c" },
						],
					},
				],
			},
			content: [
				{
					type: "text",
					text: "User answers:\n1. Question: Would you like to proceed?\n   Answer: Yes, but also add support for FLAC stream chunking in player.c",
				},
			],
			details: {
				status: "answered",
				questions: [
					{
						header: "Confirmation",
						question: "Would you like to proceed?",
						options: ["Yes, but also add support for FLAC stream chunking in player.c"],
					},
				],
				answers: [
					{
						questionIndex: 0,
						header: "Confirmation",
						question: "Would you like to proceed?",
						answer: "Yes, but also add support for FLAC stream chunking in player.c",
						wasCustom: false,
						optionIndex: 0,
					},
				],
			},
			isError: false,
		};

		await emitToolResult("ask_questions", refinementEvent.input, refinementEvent.content, refinementEvent.details, false);

		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Refinement answer must block edit until reassessment is completed");
		assert.strictEqual(blockRes.block, true);
		assert.ok(blockRes.reason.includes("REASSESSMENT_PENDING"), "Reason must cite REASSESSMENT_PENDING");
	});

	// Clean up
	await rm(rootQuestPath, { force: true });
});
