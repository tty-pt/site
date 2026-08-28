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

	// Check Quest Management & Verbal Requests rules presence
	assert.ok(injected.includes("Quest Management & Verbal Requests"), "Should include Quest Management & Verbal Requests section");
	assert.ok(injected.includes("Refine Active Quest"), "Should include Refine Active Quest rule");
	assert.ok(injected.includes("Quest Refinements & User Feedback Loops"), "Should reference Quest Refinements & User Feedback Loops section");
	assert.ok(injected.includes("Start / Switch Quest"), "Should include Start / Switch Quest rule");
	assert.ok(injected.includes("Draft Future Quest"), "Should include Draft Future Quest rule");
	assert.ok(injected.includes("Archive Quest"), "Should include Archive Quest rule");
	assert.ok(injected.includes("quest_journal_mark_saved"), "Should reference quest_journal_mark_saved tool");

	console.log("PASS: quest_journal_prompt_test");
}

testQuestJournalPromptInjection().catch((err) => {
	console.error("FAIL: quest_journal_prompt_test", err);
	process.exit(1);
});
