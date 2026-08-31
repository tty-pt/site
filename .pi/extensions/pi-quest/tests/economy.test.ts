import assert from "node:assert";
import { mkdtemp, rm } from "node:fs/promises";
import { join } from "node:path";
import { tmpdir } from "node:os";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_economy: periodic checkpoint (token thresholds removed) and subquest threshold", async () => {
	const tempCwd = await mkdtemp(join(tmpdir(), "pi-economy-test-"));
	try {
	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	let notifiedMessages: string[] = [];

	const mockPi: any = {
		on(event: string, callback: EventCallback) { if (!handlers[event]) handlers[event] = []; handlers[event].push(callback); },
		appendEntry() {}, registerEntryRenderer() {},
		registerTool(toolDef: any) { tools[toolDef.name] = toolDef; },
		registerCommand(name: string, commandDef: any) { commands[name] = commandDef; },
		sendUserMessage(msg: any) { notifiedMessages.push(typeof msg === "string" ? msg : JSON.stringify(msg)); },
	};

	questJournalExtension(mockPi);

	const mockCtx: any = {
		cwd: tempCwd,
		getContextUsage: () => ({ tokens: 50000, contextWindow: 1000000 }),
		sessionManager: { getBranch: () => [] },
		ui: { notify(msg: string) { notifiedMessages.push(msg); }, setWidget() {}, setStatus() {}, input: async () => "", select: async () => null },
		hasUI: true, mode: "tui",
	};

	assert.ok(commands["quest-economy"], "/quest-economy command should be registered");

	// Economy status now reports periodic checkpoint, not token threshold
	notifiedMessages = [];
	await commands["quest-economy"].handler("", mockCtx);
	assert.ok(notifiedMessages.some((m) => m.includes("periodic") || m.includes("Periodic") || m.includes("subquest")), `Should report periodic checkpoint: ${JSON.stringify(notifiedMessages)}`);

	// Subquest threshold still configurable
	notifiedMessages = [];
	await commands["quest-economy"].handler("333k 30k 40k", mockCtx);
	assert.ok(notifiedMessages.some((m) => m.includes("40k") || m.includes("40,000")), `Should support configuring subquest launch threshold to 40k via third arg, got: ${JSON.stringify(notifiedMessages)}`);

	// /quest-subquest-threshold still works
	notifiedMessages = [];
	await commands["quest-subquest-threshold"].handler("40k", mockCtx);
	assert.ok(notifiedMessages.some((m) => m.includes("40k") || m.includes("40,000")), `subquest threshold command: ${JSON.stringify(notifiedMessages)}`);

	// /quest-economy off disables subquest launch
	notifiedMessages = [];
	await commands["quest-economy"].handler("off", mockCtx);
	assert.ok(notifiedMessages.some((m) => m.toLowerCase().includes("disabled") || m.includes("off")), `Should allow disabling subquest launch, got: ${JSON.stringify(notifiedMessages)}`);

	// reset to default
	notifiedMessages = [];
	await commands["quest-economy"].handler("default", mockCtx);
	assert.ok(notifiedMessages.some((m) => m.toLowerCase().includes("default") || m.includes("periodic")), `Should restore default, got: ${JSON.stringify(notifiedMessages)}`);

	} finally {
		await rm(tempCwd, { recursive: true, force: true });
	}
});

Deno.test("quest_journal_economy: periodic constant and deprecated stubs", async () => {
	const { DEFAULT_CHECKPOINT_INTERVAL_TURNS } = await import("../src/constants.ts");
	assert.strictEqual(DEFAULT_CHECKPOINT_INTERVAL_TURNS, 6, "periodic interval must be 6");

	const { getEconomyThreshold, getWarningThreshold, getCompactionPressure, CompactionPressure } = await import("../src/compaction/index.ts");
	// Deprecated stubs always return neutral values (periodic replaces pressure)
	const ctx: any = { cwd: process.cwd(), getContextUsage: () => ({ tokens: 999999, contextWindow: 100000 }), sessionManager: { getBranch: () => [] } };
	assert.strictEqual((getEconomyThreshold as any)(ctx), 0);
	assert.strictEqual((getWarningThreshold as any)(ctx), 0);
	const p = (getCompactionPressure as any)(ctx);
	assert.strictEqual(p.pressure, CompactionPressure.NONE);
});
