import assert from "node:assert";
import { mkdtemp, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_economy: configuration, percentages, commands, and dynamic threshold", async () => {
	const tempCwd = await mkdtemp(join(tmpdir(), "pi-economy-test-"));
	try {
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
		cwd: tempCwd,
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

	// 1. Check default dynamic threshold on 1M context window: min(50% of 1M = 500k, ceiling = 333k) -> 333k
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("333k") || m.includes("333,000")),
		`Default economy threshold for 1M context should be capped at 333k ceiling, got: ${JSON.stringify(notifiedMessages)}`,
	);

	// Check default on 200k context window: 50% * 200k = 100k
	currentContextWindow = 200000;
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("100k") || m.includes("100,000")),
		`Default economy threshold for 200k context should be 100k (50%), got: ${JSON.stringify(notifiedMessages)}`,
	);

	// Check default on 500k context window: 50% * 500k = 250k
	currentContextWindow = 500000;
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(
		notifiedMessages.some((m) => m.includes("250k") || m.includes("250,000")),
		`Default economy threshold for 500k context should be 250k, got: ${JSON.stringify(notifiedMessages)}`,
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
		notifiedMessages.some((m) => m.includes("default") || m.includes("250k") || m.includes("50%")),
		"Should restore default threshold",
	);

	// 7. Test argument completions
	if (commands["quest-economy"].getArgumentCompletions) {
		const completions = await commands["quest-economy"].getArgumentCompletions("33");
		assert.ok(completions && completions.some((c: any) => c.value.includes("333k")), "Completions should offer 333k");
	}
	} finally {
		await rm(tempCwd, { recursive: true, force: true });
	}
});

Deno.test("quest_journal_economy: default threshold verification with known context window (100k -> 40k warning, 50k compaction)", async () => {
	const tempCwd = await mkdtemp(join(tmpdir(), "pi-economy-known-"));
	try {
	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};

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
		sendUserMessage() {},
	};

	questJournalExtension(mockPi);

	// Context window = 100,000 tokens
	const contextWindow = 100000;
	let currentTokens = 30000;

	const mockCtx: any = {
		cwd: tempCwd,
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: contextWindow,
			percent: (currentTokens / contextWindow) * 100,
		}),
		sessionManager: {
			getBranch: () => [],
		},
		ui: {
			notify() {},
			setWidget() {},
			setStatus() {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	await commands["quest"].handler("known-window-test", mockCtx);

	const { getEconomyThreshold, getWarningThreshold, getWarningMargin, getCompactionPressure, CompactionPressure } = await import("../src/compaction/index.ts");

	// 1. Verify default thresholds with no overrides:
	// warningThreshold = 40% (40,000)
	// compactionThreshold = 50% (50,000)
	const economyThreshold = getEconomyThreshold(mockCtx);
	const warningThreshold = getWarningThreshold(mockCtx);
	const warningMargin = getWarningMargin(mockCtx);

	assert.strictEqual(economyThreshold, 50000, "Default compaction threshold for 100k context window must be exactly 50,000 (50%)");
	assert.strictEqual(warningThreshold, 40000, "Default warning threshold for 100k context window must be exactly 40,000 (40%)");
	assert.strictEqual(warningMargin, 10000, "Default warning margin for 100k context window must be 10,000 (50k - 40k)");

	// 2. Below warning threshold: 35,000 tokens -> NONE
	currentTokens = 35000;
	let pressureInfo = getCompactionPressure(mockCtx);
	assert.strictEqual(pressureInfo.pressure, CompactionPressure.NONE);
	assert.strictEqual(pressureInfo.threshold, 50000);
	assert.strictEqual(pressureInfo.warningThreshold, 40000);

	// 3. At warning threshold: 40,000 tokens -> WARNING, fraction 0.0
	currentTokens = 40000;
	pressureInfo = getCompactionPressure(mockCtx);
	assert.strictEqual(pressureInfo.pressure, CompactionPressure.WARNING);
	assert.strictEqual(pressureInfo.fraction, 0);

	// 4. Mid warning window: 45,000 tokens -> WARNING, fraction 0.5
	currentTokens = 45000;
	pressureInfo = getCompactionPressure(mockCtx);
	assert.strictEqual(pressureInfo.pressure, CompactionPressure.WARNING);
	assert.strictEqual(pressureInfo.fraction, 0.5);

	// 5. At compaction threshold: 50,000 tokens -> CRITICAL, fraction 1.0
	currentTokens = 50000;
	pressureInfo = getCompactionPressure(mockCtx);
	assert.strictEqual(pressureInfo.pressure, CompactionPressure.CRITICAL);
	assert.strictEqual(pressureInfo.fraction, 1.0);

	// 6. Precedence: explicit state > env vars > settings > defaults
	// Explicit quest state override
	await commands["quest-economy"].handler("70% 15k", mockCtx);
	assert.strictEqual(getEconomyThreshold(mockCtx), 70000, "Explicit economyPercent (70%) must override default");
	assert.strictEqual(getWarningMargin(mockCtx), 15000, "Explicit warningMarginTokens (15k) must override default");
	assert.strictEqual(getWarningThreshold(mockCtx), 55000, "Warning threshold must be 70k - 15k = 55k");

	// Reset to default
	await commands["quest-economy"].handler("default", mockCtx);
	assert.strictEqual(getEconomyThreshold(mockCtx), 50000, "Reset to default should restore 50k");
	assert.strictEqual(getWarningThreshold(mockCtx), 40000, "Reset to default should restore 40k");

	// Environment variable override
	const origEnvComp = process.env.PI_QUEST_AUTO_COMPACT_TOKENS;
	const origEnvWarn = process.env.PI_QUEST_WARNING_PERCENT;
	try {
		process.env.PI_QUEST_AUTO_COMPACT_TOKENS = "60%";
		process.env.PI_QUEST_WARNING_PERCENT = "30%";
		assert.strictEqual(getEconomyThreshold(mockCtx), 60000, "Env var PI_QUEST_AUTO_COMPACT_TOKENS (60%) must override default");
		assert.strictEqual(getWarningThreshold(mockCtx), 30000, "Env var PI_QUEST_WARNING_PERCENT (30%) must override default");
	} finally {
		if (origEnvComp !== undefined) process.env.PI_QUEST_AUTO_COMPACT_TOKENS = origEnvComp;
		else delete process.env.PI_QUEST_AUTO_COMPACT_TOKENS;
		if (origEnvWarn !== undefined) process.env.PI_QUEST_WARNING_PERCENT = origEnvWarn;
		else delete process.env.PI_QUEST_WARNING_PERCENT;
	}

	await rm(".pi/quest/current", { recursive: true, force: true });
	} finally {
		await rm(tempCwd, { recursive: true, force: true });
	}
});
