import assert from "node:assert";
import taskJournalExtension from "../../.pi/extensions/task-journal.ts";

interface BeforeAgentStartResult {
	systemPrompt?: string;
}

type EventCallback = (event: { systemPrompt: string }, ctx: any) => Promise<BeforeAgentStartResult | undefined>;

async function testTaskJournalPromptInjection() {
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
	taskJournalExtension(mockPi);

	assert.ok(handlers["before_agent_start"] && handlers["before_agent_start"].length > 0, "before_agent_start handlers should be registered");

	let currentPrompt = "Base system prompt.";
	for (const cb of handlers["before_agent_start"]) {
		const res = await cb({ systemPrompt: currentPrompt }, {} as any);
		if (res?.systemPrompt) {
			currentPrompt = res.systemPrompt;
		}
	}

	const injected = currentPrompt;

	// Check TDD rules presence
	assert.ok(injected.includes("Mandatory Task Workflow Rules"), "Should include mandatory TDD rules");

	// Check Task Management & Verbal Requests rules presence
	assert.ok(injected.includes("Task Management & Verbal Requests"), "Should include Task Management & Verbal Requests section");
	assert.ok(injected.includes("Refine Active Task"), "Should include Refine Active Task rule");
	assert.ok(injected.includes("Task Refinements & Iterations"), "Should reference Task Refinements & Iterations section");
	assert.ok(injected.includes("Start / Switch Task"), "Should include Start / Switch Task rule");
	assert.ok(injected.includes("Draft Future Task"), "Should include Draft Future Task rule");
	assert.ok(injected.includes("Archive Task"), "Should include Archive Task rule");
	assert.ok(injected.includes("task_journal_mark_saved"), "Should reference task_journal_mark_saved tool");

	console.log("PASS: task_journal_prompt_test");
}

testTaskJournalPromptInjection().catch((err) => {
	console.error("FAIL: task_journal_prompt_test", err);
	process.exit(1);
});
