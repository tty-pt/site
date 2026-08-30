import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension, { CompactionPressure, getCompactionPressure } from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_compaction_escalation: complete verification of all 10 escalation state machine scenarios", async (t) => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const questSlug = "persistent-compaction-test";
	const questFilePath = `docs/current/${questSlug}.md`;
	await rm(questFilePath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];

	let currentTokens = 100000;
	let currentContextWindow = 1000000;
	let compactCallCount = 0;
	let shouldFailCompaction = false;

	const uiNotifications: Array<{ msg: string; type: string }> = [];

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens / currentContextWindow) * 100,
		}),
		sessionManager: {
			id: "session_escalation_test",
			getBranch: () => [],
		},
		compact: (opts?: any) => {
			compactCallCount++;
			if (shouldFailCompaction) {
				if (typeof opts?.onError === "function") {
					opts.onError(new Error("Simulated compaction failure"));
				}
			} else {
				currentTokens = 20000; // Reset tokens post-compaction
				if (typeof opts?.onComplete === "function") {
					opts.onComplete();
				}
			}
		},
		ui: {
			notify: (msg: string, type = "info") => {
				uiNotifications.push({ msg, type });
			},
			setStatus: () => {},
		},
		hasUI: true,
		mode: "headless",
	};

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

	// Setup active quest and configure economy threshold: 300k, warning margin: 50k (warning window: 250k - 300k)
	await writeFile(
		questFilePath,
		`# Quest: ${questSlug}\n\n## Goal\nTest compaction escalation\n\n## Original request\n> Test compaction escalation\n`,
		"utf8",
	);
	await commands["quest"].handler(questSlug, mockCtx);
	await commands["quest-economy"].handler("300k 50k", mockCtx);

	const emitTurnEnd = async (toolResults: any[] = [{ toolName: "edit", input: { path: "mods/song/song.c" } }]) => {
		// Advance time slightly to clear sub-50ms debounce between distinct turns
		await new Promise((resolve) => setTimeout(resolve, 60));
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults }, mockCtx);
		}
	};

	const emitToolCall = async (toolName: string, input?: any) => {
		for (const cb of handlers["tool_call"] || []) {
			const res = await cb({ toolName, input }, mockCtx);
			if (res) return res;
		}
		return null;
	};

	// -----------------------------------------------------------------------
	// Scenario 1: Below warning threshold -> no automatic checkpoint steering
	// -----------------------------------------------------------------------
	await t.step("1. Below warning threshold: no automatic checkpoint steering", async () => {
		currentTokens = 200000; // < 250k warning threshold
		userMessages.length = 0;

		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.NONE);

		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 0, "No synthetic messages below warning threshold");
	});

	// -----------------------------------------------------------------------
	// Scenario 2: Enter warning window -> steering is issued (and exactly 1 UI notification)
	// -----------------------------------------------------------------------
	await t.step("2. Enter warning window: steering is issued", async () => {
		currentTokens = 260000; // In lower warning window (250k - 300k, fraction = 0.2 < 0.5)
		userMessages.length = 0;
		uiNotifications.length = 0;

		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.WARNING);
		assert.ok(pressureInfo.fraction < 0.5, "Fraction in lower warning window");

		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 1, "Warning steering must be issued when entering warning window");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer", "Warning must be delivered as steer");
		assert.strictEqual(uiNotifications.length, 1, "UI must notify once on NONE -> WARNING state transition");
		const msg = userMessages[0].msg;
		assert.ok(msg.includes("Context Compaction Warning"), "Message header must identify warning");
		assert.ok(msg.includes("Approaching Threshold"), "Escalation label must indicate approaching threshold");
		assert.ok(msg.includes("EXACT NEXT ACTION"), "Must require EXACT NEXT ACTION");
	});

	// -----------------------------------------------------------------------
	// Scenario 3: Next turn while still in warning window -> steering is issued again (0 extra UI notifications)
	// -----------------------------------------------------------------------
	await t.step("3. Next turn while still in warning window: steering is issued again", async () => {
		currentTokens = 280000; // In upper warning window (fraction = 0.6 >= 0.5)
		userMessages.length = 0;
		uiNotifications.length = 0;

		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.WARNING);
		assert.ok(pressureInfo.fraction >= 0.5, "Fraction in upper warning window");

		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 1, "Warning steering must be issued again on next turn");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer", "Warning must be delivered as steer");
		assert.strictEqual(uiNotifications.length, 0, "No duplicate UI notifications within same pressure state");
		const msg = userMessages[0].msg;
		assert.ok(msg.includes("Close to Threshold"), "Escalation label must indicate close to threshold");
		assert.ok(msg.includes("Prioritize an exhaustive durable checkpoint now"), "Advice must escalate urgency");
	});

	// -----------------------------------------------------------------------
	// Scenario 4: Several turns in warning window -> steering continues (0 UI spam)
	// -----------------------------------------------------------------------
	await t.step("4. Several turns in warning window: steering continues", async () => {
		currentTokens = 290000;
		userMessages.length = 0;
		uiNotifications.length = 0;

		for (let i = 0; i < 3; i++) {
			await emitTurnEnd();
		}

		assert.strictEqual(userMessages.length, 3, "Steering must continue on every turn inside warning window");
		assert.ok(userMessages.every((m) => m.options?.deliverAs === "steer"), "All warnings must use steer");
		assert.strictEqual(uiNotifications.length, 0, "Zero UI notification spam across repeated warning turns");
	});

	// -----------------------------------------------------------------------
	// Scenario 5: Cross threshold -> critical directive is issued (exactly 1 UI notification)
	// -----------------------------------------------------------------------
	await t.step("5. Cross threshold: critical directive is issued", async () => {
		currentTokens = 310000; // >= 300k threshold
		userMessages.length = 0;
		uiNotifications.length = 0;

		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.CRITICAL);

		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 1, "Critical directive must be issued");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer", "Critical directive must be delivered as steer");
		assert.strictEqual(uiNotifications.length, 1, "UI must notify once on WARNING -> CRITICAL transition");
		const msg = userMessages[0].msg;
		assert.ok(msg.includes("CRITICAL QUEST JOURNAL EXECUTION DIRECTIVE"), "Must use critical header");
		assert.ok(msg.includes("STOP treating checkpointing as optional"), "Must establish top-priority directive");
		assert.ok(msg.includes("This directive supersedes your current implementation plan"), "Must explicitly supersede plan");
		assert.ok(msg.includes("ONLY AFTER THE DURABLE SAVE IS VERIFIED"), "Must instruct that save is required before ordinary work");
	});

	// -----------------------------------------------------------------------
	// Scenario 6: Next turn still above threshold -> critical directive is issued again (0 extra UI notifications)
	// -----------------------------------------------------------------------
	await t.step("6. Next turn still above threshold: critical directive is issued again", async () => {
		currentTokens = 315000;
		userMessages.length = 0;
		uiNotifications.length = 0;

		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 1, "Critical directive must repeat on next turn");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer", "Must be delivered as steer");
		assert.strictEqual(uiNotifications.length, 0, "No duplicate UI notifications within CRITICAL");
		assert.ok(userMessages[0].msg.includes("CRITICAL QUEST JOURNAL EXECUTION DIRECTIVE"));
	});

	// -----------------------------------------------------------------------
	// Scenario 6b: Code mutations are blocked while in CRITICAL without verified save (0 UI notifications)
	// -----------------------------------------------------------------------
	await t.step("6b. Code mutations are blocked while in CRITICAL without verified save", async () => {
		uiNotifications.length = 0;
		// Project code edit must be blocked silently to UI and return reason to model
		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Must block project code edit");
		assert.strictEqual(blockRes.block, true);
		assert.ok(blockRes.reason.includes("CRITICAL_COMPACTION_CHECKPOINT_REQUIRED"));
		assert.strictEqual(uiNotifications.length, 0, "Blocked tool call must not spam UI notifications");

		// Quest file edit must NOT be blocked
		const allowQuestEdit = await emitToolCall("edit", { path: `docs/current/${questSlug}.md` });
		assert.strictEqual(allowQuestEdit?.block, undefined, "Quest markdown edits must remain allowed");
	});

	// -----------------------------------------------------------------------
	// Scenario 7: Save succeeds but context is still above threshold -> critical compaction steering continues and initiates c.compact
	// -----------------------------------------------------------------------
	await t.step("7. Save succeeds but context is still above threshold: critical compaction steering continues and initiates c.compact", async () => {
		// Perform verified save
		await tools["quest_mark_saved"].execute("save_esc_1", {}, null, null, mockCtx);
		userMessages.length = 0;
		uiNotifications.length = 0;
		const compactCountBefore = compactCallCount;

		// Next turn_end: tokens are still 315k >= 300k
		await emitTurnEnd([{ toolName: "quest_mark_saved" }]);
		assert.strictEqual(userMessages.length, 1, "Critical compaction steering must continue even after save");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer", "Must use deliverAs: steer");
		assert.strictEqual(uiNotifications.length, 0, "No extra UI notifications when remaining in CRITICAL");
		const msg = userMessages[0].msg;
		assert.ok(msg.includes("DURABLE STATE SAVED"), "Must indicate durable state is saved and ready for compaction");
		assert.ok(msg.includes("auto-compaction is now being initiated") || msg.includes("Context auto-compaction is now required"), "Must insist on compaction");

		// Wait for scheduled c.compact call (50ms timeout)
		await new Promise((resolve) => setTimeout(resolve, 80));
		assert.strictEqual(compactCallCount, compactCountBefore + 1, "c.compact() must be directly invoked by extension when verified save exists above threshold");
	});

	// -----------------------------------------------------------------------
	// Scenario 8: Compaction succeeds -> pressure resets and repeated steering stops
	// -----------------------------------------------------------------------
	await t.step("8. Compaction succeeds: pressure resets and repeated steering stops", async () => {
		userMessages.length = 0;

		// Simulate session_compact event (post-compaction reset, tokens = 20,000)
		for (const cb of handlers["session_compact"] || []) {
			await cb({}, mockCtx);
		}

		currentTokens = 20000;
		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.NONE, "Pressure resets to NONE post-compaction");

		userMessages.length = 0;
		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 0, "Repeated steering stops after successful compaction");
	});

	// -----------------------------------------------------------------------
	// Scenario 9: Compaction fails -> critical state remains active
	// -----------------------------------------------------------------------
	await t.step("9. Compaction fails: critical state remains active", async () => {
		// Context climbs back up to critical
		currentTokens = 320000;
		// Make quest dirty
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: "mods/gig/gig.c" } }, mockCtx);
		}

		// Simulate session_compact_failed
		for (const cb of handlers["session_compact_failed"] || []) {
			await cb({}, mockCtx);
		}

		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.CRITICAL, "Critical pressure remains active after compaction failure");

		userMessages.length = 0;
		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 1, "Critical directive continues to be emitted");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer");
		assert.ok(userMessages[0].msg.includes("CRITICAL QUEST JOURNAL EXECUTION DIRECTIVE"));
	});

	// -----------------------------------------------------------------------
	// Scenario 10: Context pressure rises rapidly across threshold -> critical steering occurs immediately
	// -----------------------------------------------------------------------
	await t.step("10. Rapid jump across threshold: critical steering occurs immediately without warning phase", async () => {
		// Reset state
		for (const cb of handlers["session_compact"] || []) {
			await cb({}, mockCtx);
		}
		currentTokens = 100000; // Low
		userMessages.length = 0;
		uiNotifications.length = 0;

		// Sudden massive context jump (e.g. large file reads / tool output) straight to 350k
		currentTokens = 350000;

		const pressureInfo = getCompactionPressure(mockCtx);
		assert.strictEqual(pressureInfo.pressure, CompactionPressure.CRITICAL);

		await emitTurnEnd();
		assert.strictEqual(userMessages.length, 1, "Critical steering fires immediately on sudden jump");
		assert.strictEqual(userMessages[0].options?.deliverAs, "steer");
		assert.strictEqual(uiNotifications.length, 1, "UI notifies on rapid NONE -> CRITICAL jump");
		assert.ok(userMessages[0].msg.includes("CRITICAL QUEST JOURNAL EXECUTION DIRECTIVE"));
	});

	// -----------------------------------------------------------------------
	// Scenario 11: Turn-aware deduplication: multiple context events in same turn do NOT duplicate steer
	// -----------------------------------------------------------------------
	await t.step("11. Turn-aware deduplication: multiple context events in same turn do NOT duplicate steer", async () => {
		userMessages.length = 0;
		uiNotifications.length = 0;

		// Fire multiple context events in the same turn without pressure change
		for (const cb of handlers["context"] || []) {
			await cb({}, mockCtx);
			await cb({}, mockCtx);
			await cb({}, mockCtx);
		}

		assert.strictEqual(userMessages.length, 0, "No duplicate steering messages should be emitted for same turn without change");
		assert.strictEqual(uiNotifications.length, 0, "No extra UI notifications on repeated context events");
	});

	// Clean up
	await rm(questFilePath, { force: true });
});
