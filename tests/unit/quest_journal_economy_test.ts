import assert from "node:assert";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function testEconomyConfigurationAndCommand() {
	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	let userMessages: any[] = [];
	let notifiedMessages: string[] = [];
	let lastStatusText: string | undefined;

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

	assert.ok(commands["quest-economy"], "/quest-economy command should be registered");

	let currentTokens: number | null = 50000;
	let currentContextWindow = 200000;

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens! / currentContextWindow) * 100,
		}),
		sessionManager: {
			getBranch: () => [],
		},
		ui: {
			notify(msg: string) {
				notifiedMessages.push(msg);
			},
			setWidget() {},
			setStatus(key: string, text: string | undefined) {
				if (key === "quest") lastStatusText = text;
			},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 1. Check default economy threshold (140k) and warning margin (30k)
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("140k") || m.includes("140,000")),
		"Default economy threshold should be 140k tokens",
	);
	assert.ok(
		notifiedMessages.some((m) => m.includes("30k") || m.includes("30,000")),
		"Default warning margin should be 30k tokens",
	);

	// 2. Set economy threshold and warning margin via /quest-economy 140k 25k
	notifiedMessages = [];
	await commands["quest-economy"].handler("140k 25k", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => (m.includes("140k") || m.includes("140,000")) && (m.includes("25k") || m.includes("25,000"))),
		"Should set both economy threshold to 140k and warning margin to 25k",
	);

	// 3. Set warning margin directly via /quest-warning 35k
	assert.ok(commands["quest-warning"], "/quest-warning command should be registered");
	notifiedMessages = [];
	await commands["quest-warning"].handler("35k", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("35k") || m.includes("35,000")),
		"Should successfully update warning margin to 35k",
	);

	// 4. Set economy threshold with pure numbers: /quest-economy 150000
	notifiedMessages = [];
	await commands["quest-economy"].handler("150000", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("150k") || m.includes("150,000")),
		"Should accept raw integer values",
	);

	// 5. Disable economy auto-compaction: /quest-economy off
	notifiedMessages = [];
	await commands["quest-economy"].handler("off", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("disabled") || m.includes("off")),
		"Should allow disabling economy auto-compaction",
	);

	// 6. Reset to default: /quest-economy default
	notifiedMessages = [];
	await commands["quest-economy"].handler("default", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("140k") || m.includes("140,000")),
		"Should restore default threshold of 140k",
	);

	// 6. Test argument completions
	if (commands["quest-economy"].getArgumentCompletions) {
		const completions = await commands["quest-economy"].getArgumentCompletions("14");
		assert.ok(completions && completions.some((c: any) => c.value.includes("140k")), "Completions should offer 140k");
	}

	console.log("PASS: quest_journal_economy_test");
}

testEconomyConfigurationAndCommand().catch((err) => {
	console.error("FAIL: quest_journal_economy_test", err);
	process.exit(1);
});
