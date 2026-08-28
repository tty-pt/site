import assert from "node:assert";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

interface BeforeAgentStartResult {
	systemPrompt?: string;
}

type EventCallback = (event: { systemPrompt: string }, ctx: any) => Promise<BeforeAgentStartResult | undefined>;

async function testQuestJournalPromptInjection() {
	const handlers: Record<string, EventCallback[]> = {};

	const mockPi: any = {
		on(event: string, callback: EventCallback) {
			if (!handlers[event]) handlers[event] = [];
			handlers[event].push(callback);
		},
		appendEntry() {},
		registerEntryRenderer() {},
		registerTool() {},
		registerCommand() {},
		sendUserMessage() {},
	};

	// Register extension hooks
	questJournalExtension(mockPi);

	assert.ok(handlers["before_agent_start"] && handlers["before_agent_start"].length > 0, "before_agent_start handlers should be registered");

	const mockCtx = {
		cwd: "/home/quirinpa/site",
		getContextUsage: () => ({ percent: 20 }),
		sessionManager: {
			getBranch: () => [],
		},
	};

	let currentPrompt = "Base system prompt.";
	for (const cb of handlers["before_agent_start"]) {
		const res = await cb({ systemPrompt: currentPrompt }, mockCtx as any);
		if (res?.systemPrompt) {
			currentPrompt = res.systemPrompt;
		}
	}

	const injected = currentPrompt;

	// Check Session Awareness presence
	assert.ok(injected.includes("# Session awareness (auto-injected)"), "Should include session awareness header");
	assert.ok(injected.includes("- Now:"), "Should include current timestamp");
	assert.ok(injected.includes("- cwd: /home/quirinpa/site"), "Should include working directory");
	assert.ok(injected.includes("- Active quest:"), "Should include active quest line");

	// Check TDD rules presence
	assert.ok(injected.includes("Mandatory Quest Workflow Rules"), "Should include mandatory TDD rules");

	// Check Autonomous Quest Management presence
	assert.ok(injected.includes("Autonomous Quest Management"), "Should include Autonomous Quest Management section");
	assert.ok(injected.includes("Auto-Initialize New Quest"), "Should include Auto-Initialize New Quest rule");
	assert.ok(injected.includes("Auto-Refine Active Quest"), "Should include Auto-Refine Active Quest rule");
	assert.ok(injected.includes("Auto-Create Sub-Quests"), "Should include Auto-Create Sub-Quests rule");
	assert.ok(injected.includes("Auto-Archive Upon Completion"), "Should include Auto-Archive rule");
	assert.ok(injected.includes("Sub-Quest Completion (Autonomous Continuation)") || injected.includes("For Sub-Quests"), "Should mention autonomous sub-quest completion");
	assert.ok(injected.includes("quest_mark_saved") || injected.includes("quest_journal_mark_saved"), "Should reference quest_mark_saved tool");
	assert.ok(injected.includes("quest_subquest") || injected.includes("quest_journal_subquest"), "Should reference quest_subquest tool");

	console.log("PASS: quest_journal_prompt_test");
}

async function testQuestJournalCommandInference() {
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: any[] = [];

	const mockPi: any = {
		on() {},
		appendEntry() {},
		registerEntryRenderer() {},
		registerTool(tool: any) {
			tools[tool.name] = tool;
		},
		registerCommand(name: string, def: any) {
			commands[name] = def;
		},
		sendUserMessage(msg: any) {
			userMessages.push(msg);
		},
	};

	questJournalExtension(mockPi);

	assert.ok(commands["quest"], "/quest command should be registered");
	assert.strictEqual(commands["task"], undefined, "/task command should NOT be registered");
	assert.ok(commands["subquest"], "/subquest command should be registered");
	assert.strictEqual(commands["subtask"], undefined, "/subtask command should NOT be registered");
	assert.ok(commands["quest-draft"], "/quest-draft command should be registered");
	assert.strictEqual(commands["task-draft"], undefined, "/task-draft command should NOT be registered");
	assert.ok(tools["quest_subquest"], "quest_subquest tool should be registered");
	assert.ok(tools["quest_archive"], "quest_archive tool should be registered");
	assert.ok(tools["quest_mark_saved"], "quest_mark_saved tool should be registered");

	// Test 1: /quest with a full description infers slug and sets goal without prompting for name
	let inputPrompted = false;
	const mockCtxTui: any = {
		cwd: "/home/quirinpa/site",
		mode: "tui",
		hasUI: true,
		ui: {
			input: async (_prompt: string) => {
				inputPrompted = true;
				return "some input";
			},
			notify: () => {},
			setStatus: () => {},
		},
	};

	await commands["quest"].handler("Improve the search bar responsiveness", mockCtxTui);
	assert.strictEqual(inputPrompted, false, "Should not prompt user for input when description is provided");
	assert.ok(userMessages.length > 0, "Should send user message to initialize quest");
	const lastMsg = userMessages[userMessages.length - 1];
	const text = Array.isArray(lastMsg) ? lastMsg[0].text : lastMsg.text || "";
	assert.ok(text.includes("improve-the-search-bar-responsiveness"), "Should infer slug from description");
	assert.ok(text.includes("Improve the search bar responsiveness"), "Should include stated goal in message");

	// Clean up created file in test
	try {
		const fs = await import("node:fs/promises");
		await fs.unlink("docs/current/improve-the-search-bar-responsiveness.md");
	} catch {
		// ignore
	}

	console.log("PASS: quest_journal_command_inference_test");
}

async function testQuestJournalSessionStartNoModal() {
	const handlers: Record<string, Function[]> = {};
	let uiInputCalled = false;
	let uiSelectCalled = false;

	const mockPi: any = {
		on(event: string, callback: Function) {
			if (!handlers[event]) handlers[event] = [];
			handlers[event].push(callback);
		},
		appendEntry() {},
		registerEntryRenderer() {},
		registerTool() {},
		registerCommand() {},
		sendUserMessage() {},
	};

	questJournalExtension(mockPi);

	assert.ok(handlers["session_start"] && handlers["session_start"].length > 0, "session_start handler should be registered");

	const mockCtx = {
		cwd: "/home/quirinpa/site",
		mode: "tui",
		hasUI: true,
		ui: {
			input: async () => {
				uiInputCalled = true;
				return "some input";
			},
			select: async () => {
				uiSelectCalled = true;
				return "Cancel";
			},
			notify: () => {},
			setStatus: () => {},
		},
		sessionManager: {
			getBranch: () => [],
		},
	};

	for (const cb of handlers["session_start"]) {
		await cb({ reason: "startup" }, mockCtx);
	}

	assert.strictEqual(uiInputCalled, false, "session_start should NOT prompt ui.input on startup");
	assert.strictEqual(uiSelectCalled, false, "session_start should NOT prompt ui.select on startup");

	console.log("PASS: quest_journal_session_start_no_modal_test");
}

async function runAllTests() {
	await testQuestJournalPromptInjection();
	await testQuestJournalCommandInference();
	await testQuestJournalSessionStartNoModal();
}

runAllTests().catch((err) => {
	console.error("FAIL: quest_journal tests", err);
	process.exit(1);
});
