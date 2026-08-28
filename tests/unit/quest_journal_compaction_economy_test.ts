import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function testCompactionAndDeepPreservation() {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const testQuestPath = "docs/current/economy-compaction-quest.md";
	await rm(testQuestPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	let userMessages: any[] = [];
	let notifiedMessages: string[] = [];
	let compactCalled = false;
	let compactOptions: any = null;

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

	let currentTokens = 50000;
	let currentContextWindow = 200000;

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
		compact: (options: any) => {
			compactCalled = true;
			compactOptions = options;
		},
		ui: {
			notify(msg: string) {
				notifiedMessages.push(msg);
			},
			setWidget() {},
			setStatus() {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 1. Initialize active quest
	await writeFile(testQuestPath, "# Quest: economy-compaction-quest\n\n## Goal\nTest economy goal\n", "utf8");
	await commands["quest"].handler("economy-compaction-quest", mockCtx);

	// 2. Test turn_end when tokens reach 30k window before 140k threshold (110k tokens)
	currentTokens = 110000;
	userMessages = [];
	compactCalled = false;

	// Simulate turn end with unsaved quest state after previous compaction
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	// Now saveCount === compactCount (save pending)

	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}

	assert.ok(userMessages.length > 0, "Should send pre-compaction preservation message at 30k tokens before threshold (110k/140k)");
	const warningMsg = Array.isArray(userMessages[0]) ? userMessages[0][0].text : userMessages[0].text || "";
	assert.ok(
		warningMsg.toLowerCase().includes("compaction") &&
		(warningMsg.includes("30k") || warningMsg.toLowerCase().includes("soon") || warningMsg.toLowerCase().includes("limit")),
		`Warning message should explicitly notify of upcoming compaction within 30k, got: ${warningMsg}`,
	);
	assert.ok(
		warningMsg.toLowerCase().includes("re-research") ||
		warningMsg.toLowerCase().includes("exhaustive") ||
		warningMsg.toLowerCase().includes("snapshot"),
		`Warning message should instruct deep preservation against re-research, got: ${warningMsg}`,
	);
	assert.strictEqual(compactCalled, false, "Should not compact before the agent updates the quest file");

	// 3. Test compaction gate: session_before_compact when save is pending should cancel
	notifiedMessages = [];
	let gateBlocked = false;
	for (const cb of handlers["session_before_compact"] || []) {
		const res = await cb({}, mockCtx);
		if (res && res.cancel) {
			gateBlocked = true;
		}
	}
	assert.strictEqual(gateBlocked, true, "session_before_compact should block compaction when save is pending");

	// 4. Mark quest as saved (agent updates the quest file with extensive findings)
	compactCalled = false;
	compactOptions = null;
	await tools["quest_mark_saved"].execute("call_saved", {}, null, null, mockCtx);

	// 5. Verify that after updating the quest file, compaction triggers immediately!
	// Either triggered directly by quest_mark_saved or in the following turn_end
	if (!compactCalled) {
		for (const cb of handlers["turn_end"] || []) {
			await cb({}, mockCtx);
		}
	}

	assert.strictEqual(compactCalled, true, "After updating the quest file in the 30k window, compaction should trigger");
	assert.ok(compactOptions && compactOptions.customInstructions, "Compaction should pass custom instructions");

	// 6. Test compaction gate now allows compaction
	let gateAllowed = true;
	for (const cb of handlers["session_before_compact"] || []) {
		const res = await cb({}, mockCtx);
		if (res && res.cancel) {
			gateAllowed = false;
		}
	}
	assert.strictEqual(gateAllowed, true, "session_before_compact should allow compaction after fresh save");

	// 7. Verify System prompt contains Pre-Compaction Deep Preservation Protocol
	let systemPrompt = "Base system prompt.";
	for (const cb of handlers["before_agent_start"] || []) {
		const res = await cb({ systemPrompt }, mockCtx);
		if (res?.systemPrompt) systemPrompt = res.systemPrompt;
	}

	assert.ok(systemPrompt.includes("Economy") || systemPrompt.includes("Compaction"), "System prompt should mention Economy / Compaction rules");
	assert.ok(systemPrompt.toLowerCase().includes("re-research"), "System prompt should emphasize no re-research across compactions");

	// 8. Test file watch tool_result: writing/editing active quest file in warning window triggers compaction
	// Reset to unsaved state after compaction
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}
	compactCalled = false;
	compactOptions = null;
	currentTokens = 120000;

	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "write",
				input: { path: testQuestPath },
			},
			mockCtx,
		);
	}

	assert.strictEqual(compactCalled, true, "Writing to the active quest file via tool_result should trigger compaction when in warning window");

	// Clean up test files
	await rm(testQuestPath, { force: true });

	console.log("PASS: quest_journal_compaction_economy_test");
}

testCompactionAndDeepPreservation().catch((err) => {
	console.error("FAIL: quest_journal_compaction_economy_test", err);
	process.exit(1);
});
