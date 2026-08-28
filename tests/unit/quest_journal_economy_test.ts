import assert from "node:assert";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_economy: configuration, percentages, commands, and dynamic threshold", async () => {
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
	let currentContextWindow = 1000000; // 1M context window

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

	// 1. Check default dynamic threshold on 1M context window: min(80% of 1M = 800k, ceiling = 333k) -> 333k
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("333k") || m.includes("333,000")),
		`Default economy threshold for 1M context should be capped at 333k ceiling, got: ${JSON.stringify(notifiedMessages)}`,
	);

	// Check default on 200k context window: clamp(80% * 200k) = 160k (clamped so it doesn't exceed 200k or ceiling)
	currentContextWindow = 200000;
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("160k") || m.includes("160,000") || m.includes("80%")),
		`Default economy threshold for 200k context should be 160k or 80%, got: ${JSON.stringify(notifiedMessages)}`,
	);

	// Check default on 500k context window: min(80% * 500k = 400k, ceiling = 333k) -> 333k
	currentContextWindow = 500000;
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("333k") || m.includes("333,000")),
		`Default economy threshold for 500k context should be 333k ceiling, got: ${JSON.stringify(notifiedMessages)}`,
	);

	// 2. Set explicit percentage threshold: /quest-economy 75%
	notifiedMessages = [];
	await commands["quest-economy"].handler("75%", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("75%") || m.includes("375k")),
		`Should accept percentage notation '75%', got: ${JSON.stringify(notifiedMessages)}`,
	);

	// 3. Set explicit token threshold: /quest-economy 333k 30k
	notifiedMessages = [];
	await commands["quest-economy"].handler("333k 30k", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => (m.includes("333k") || m.includes("333,000")) && (m.includes("30k") || m.includes("30,000"))),
		`Should set economy threshold to 333k and warning margin to 30k, got: ${JSON.stringify(notifiedMessages)}`,
	);

	// 4. Configure subquest launch compaction threshold: /quest-subquest-threshold 40k
	assert.ok(commands["quest-subquest-threshold"] || commands["quest-economy"], "Subquest threshold command or parameter should exist");
	notifiedMessages = [];
	await commands["quest-economy"].handler("333k 30k 40k", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("40k") || m.includes("40,000")),
		`Should support configuring subquest launch threshold to 40k, got: ${JSON.stringify(notifiedMessages)}`,
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
		notifiedMessages.some((m) => m.includes("default") || m.includes("80%") || m.includes("400k")),
		"Should restore default threshold",
	);

	// 7. Test argument completions
	if (commands["quest-economy"].getArgumentCompletions) {
		const completions = await commands["quest-economy"].getArgumentCompletions("33");
		assert.ok(completions && completions.some((c: any) => c.value.includes("333k")), "Completions should offer 333k");
	}
});
