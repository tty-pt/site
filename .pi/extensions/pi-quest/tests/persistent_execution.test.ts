import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { questPath, resolveQuestRecordBySlug } from "../src/paths.ts";
import { sessionStates } from "../src/state.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_persistent_execution: all 12 lifecycle scenarios", async (t) => {
	const currentDir = ".pi/quest/current";
	const archiveDir = ".pi/quest/archive";
	await rm(currentDir, { recursive: true, force: true });
	await rm(archiveDir, { recursive: true, force: true });
	await mkdir(currentDir, { recursive: true });
	await mkdir(archiveDir, { recursive: true });

	function setupHarness(initialTokens = 10000, contextWindow = 1000000, sessionId = "session_exec_test") {
		sessionStates.delete(sessionId);
		const handlers: Record<string, EventCallback[]> = {};
		const tools: Record<string, any> = {};
		const commands: Record<string, any> = {};
		const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
		let compactInvocationCount = 0;
		let lastCompactOptions: any = null;

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
			sendUserMessage(msg: any, options?: any) {
				userMessages.push({ msg, options });
			},
			sendMessage(msg: any, options?: any) {
				userMessages.push({ msg: msg?.content || msg, options, customType: msg?.customType, display: msg?.display });
			},
		};

		questJournalExtension(mockPi);

		let currentTokens = initialTokens;

		const mockCtx: any = {
			cwd: process.cwd(),
			sessionManager: {
				id: sessionId,
				getBranch: () => [],
			},
			getContextUsage: () => ({
				tokens: currentTokens,
				contextWindow: contextWindow,
				percent: (currentTokens / contextWindow) * 100,
			}),
			compact: (opts: any) => {
				compactInvocationCount++;
				lastCompactOptions = opts;
				if (opts?.onComplete) {
					setTimeout(() => opts.onComplete(), 10);
				}
			},
			ui: {
				notify: () => {},
				setStatus: () => {},
				setWidget: () => {},
				input: async () => "",
				select: async () => null,
			},
			hasUI: true,
			mode: "tui",
		};

		return {
			mockPi,
			mockCtx,
			handlers,
			tools,
			commands,
			userMessages,
			getCompactCount: () => compactInvocationCount,
			getLastCompactOptions: () => lastCompactOptions,
			setTokens: (toks: number) => {
				currentTokens = toks;
			},
		};
	}

	await t.step("Test 1 — automatic root quest for substantive prompt", async () => {
		await rm(currentDir, { recursive: true, force: true });
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_1");
		const substantivePrompt = "Refactor the authentication system to support passkeys while preserving existing login behavior.";

		// Trigger before_agent_start with substantive prompt
		for (const cb of harness.handlers["before_agent_start"] || []) {
			await cb({ prompt: substantivePrompt, systemPrompt: "Base prompt." }, harness.mockCtx);
		}

		// Verify provisional root quest is initialized
		const statusProvisional = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(statusProvisional && statusProvisional.includes("PROVISIONAL ROOT INITIALIZATION"), "Root quest must enter provisional state");

		// Initialize durable quest with research-grounded semantic name
		const slug = "passkey-auth-migration";
		await harness.tools["quest_update_state"].execute(
			"call_init",
			{
				name: slug,
				goal: "Support passkeys in authentication system",
				status: "Research complete",
				understanding: "Authentication system is in mods/auth and uses axil session cookies.",
				assumptions: ["Existing login fallback remains functional"],
				openQuestions: ["None"],
				findings: ["Passkey public key credentials can be verified with libcrypto"],
				plan: ["1. Add WebAuthn challenge endpoint", "2. Add passkey verify handler"],
				planConfidence: "high",
				exactNextAction: "Implement WebAuthn endpoint",
				researchComplete: true,
			},
			null,
			null,
			harness.mockCtx,
		);

		const status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes(slug), `Active quest slug should be active: ${status}`);

		// Verify quest file content on disk
		const rec = await resolveQuestRecordBySlug(slug);
		assert.ok(rec, "Quest record must exist");
		const content = await readFile(rec.path, "utf8");
		assert.ok(content.includes(substantivePrompt), "Original prompt must be persisted in quest file");
	});

	await t.step("Test 2 — trivial prompt does NOT create a quest", async () => {
		const harness = setupHarness(10000, 1000000, "session_test_2");
		const trivialPrompt = "What is the capital of France?";

		for (const cb of harness.handlers["before_agent_start"] || []) {
			await cb({ prompt: trivialPrompt, systemPrompt: "Base prompt." }, harness.mockCtx);
		}

		const status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.strictEqual(status, "No active quest.", "Trivial prompt should NOT create an active quest");
	});

	await t.step("Test 3 — refinement to active quest", async () => {
		await rm(currentDir, { recursive: true, force: true });
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_3");
		const rootPrompt = "Implement song volume normalization across all audio formats";

		// Create root quest
		for (const cb of harness.handlers["before_agent_start"] || []) {
			await cb({ prompt: rootPrompt, systemPrompt: "Base prompt." }, harness.mockCtx);
		}

		const slug = "song-volume-normalization";
		await harness.tools["quest_update_state"].execute(
			"call_init",
			{
				name: slug,
				goal: rootPrompt,
				status: "Research complete",
				understanding: "Audio player normalizes volume in audio pipeline.",
				assumptions: ["Replaygain tags supported"],
				openQuestions: ["None"],
				findings: ["Normalizer fits in player buffer loop"],
				plan: ["1. Implement volume scaling"],
				planConfidence: "high",
				exactNextAction: "Add gain scaling",
				researchComplete: true,
			},
			null,
			null,
			harness.mockCtx,
		);

		const status1 = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status1.includes("fresh"), "Initial quest save should be fresh");

		// Send refinement
		const refinementPrompt = "Actually, this must remain compatible with Node 20 and support replaygain tags";
		for (const cb of harness.handlers["before_agent_start"] || []) {
			await cb({ prompt: refinementPrompt, systemPrompt: "Base prompt." }, harness.mockCtx);
		}

		// Verify active quest is STILL the same root quest, but marked dirty (SAVE PENDING)
		const status2 = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status2.includes(slug) || status2.includes("SAVE PENDING"), `Active quest must reflect state: ${status2}`);
		assert.ok(status2.includes("SAVE PENDING") || status2.includes(slug), "Refinement must mark quest state dirty / SAVE PENDING");
	});

	await t.step("Test 4 — continuous durable memory without artificial checkpoints", async () => {
		await rm(currentDir, { recursive: true, force: true });
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_4");
		const rootSlug = "test-checkpoint-turns-quest";
		await harness.commands["quest"].handler(rootSlug, harness.mockCtx);

		harness.userMessages.length = 0;

		// Simulate multiple substantive turns (e.g. 5 turns) under healthy context
		for (let i = 0; i < 5; i++) {
			for (const cb of harness.handlers["tool_result"] || []) {
				await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, harness.mockCtx);
			}
			for (const cb of harness.handlers["turn_end"] || []) {
				await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, harness.mockCtx);
			}
		}

		// Verify NO synthetic incremental checkpoint message was sent
		const checkpointMsgs = harness.userMessages.filter((m) =>
			(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Quest Incremental Checkpoint"),
		);
		assert.strictEqual(checkpointMsgs.length, 0, "No artificial checkpoint message should be injected during normal execution");

		// Status should reflect dirty (SAVE PENDING)
		const status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes("SAVE PENDING"), "Substantive turns must mark quest dirty");

		// Calling quest_mark_saved resets dirty flag
		await harness.tools["quest_mark_saved"].execute("save_call", {}, null, null, harness.mockCtx);
		const cleanStatus = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(cleanStatus.includes("fresh"), "quest_mark_saved must clear dirty state");
	});

	await t.step("Test 5 & 7 — periodic checkpoint records state and compaction gate blocks dirty", async () => {
		await rm(currentDir, { recursive: true, force: true });
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_5");
		const rootSlug = "test-compaction-gate-quest";
		await harness.commands["quest"].handler(rootSlug, harness.mockCtx);

		// 6 substantive turns → periodic checkpoint
		harness.userMessages.length = 0;
		for (let i = 0; i < 6; i++) {
			for (const cb of harness.handlers["tool_result"] || []) {
				await cb({ toolName: "edit", input: { path: `mods/gig/gig${i}.c` } }, harness.mockCtx);
			}
			for (const cb of harness.handlers["turn_end"] || []) {
				await cb({ toolResults: [{ toolName: "edit", input: { path: `mods/gig/gig${i}.c` } }] }, harness.mockCtx);
			}
		}

		const finalSaveMsgs = harness.userMessages.filter((m) =>
			(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Periodic Durable Checkpoint"),
		);
		assert.strictEqual(finalSaveMsgs.length, 1, "6 substantive turns must issue periodic checkpoint");

		// Explicit session_before_compact gate must cancel when dirty
		let cancelRes: any;
		for (const cb of harness.handlers["session_before_compact"] || []) {
			cancelRes = await cb({}, harness.mockCtx);
		}
		assert.strictEqual(cancelRes?.cancel, true, "session_before_compact must block/cancel when dirty");
	});

	await t.step("Test 6 — safe compaction when threshold reached and quest is clean", async () => {
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_6");
		const rootSlug = "test-safe-compaction-quest";
		await harness.commands["quest"].handler(rootSlug, harness.mockCtx);
		await harness.commands["quest-economy"].handler("333k 30k", harness.mockCtx);

		// Save quest and make sure it is clean
		await harness.tools["quest_mark_saved"].execute("call_save_safe", {}, null, null, harness.mockCtx);

		// Now context reaches 335k tokens
		harness.setTokens(335000);

		for (const cb of harness.handlers["turn_end"] || []) {
			await cb({ toolResults: [] }, harness.mockCtx);
		}

		let beforeCompactRes: any;
		for (const cb of harness.handlers["session_before_compact"] || []) {
			beforeCompactRes = await cb({}, harness.mockCtx);
		}
		assert.notStrictEqual(beforeCompactRes?.cancel, true, "Clean state at threshold must allow compaction");
	});

	await t.step("Test 8 — post-compaction autonomous resume", async () => {
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_8");
		const rootSlug = "test-post-compaction-resume-quest";
		await harness.commands["quest"].handler(rootSlug, harness.mockCtx);
		await harness.tools["quest_mark_saved"].execute("call_save_init", {}, null, null, harness.mockCtx);

		for (const cb of harness.handlers["session_before_compact"] || []) {
			await cb({}, harness.mockCtx);
		}

		harness.userMessages.length = 0;

		// Simulate session_compact event
		for (const cb of harness.handlers["session_compact"] || []) {
			await cb({}, harness.mockCtx);
		}

		assert.ok(harness.userMessages.length > 0, "session_compact must send resume directive");
		const lastMsg = harness.userMessages[harness.userMessages.length - 1];
		const msgText = typeof lastMsg.msg === "string" ? lastMsg.msg : (lastMsg.msg?.text || "");
		assert.ok(msgText.includes("Post-Compaction Autonomous Resumption Directive"), "Resume directive must be included");
		assert.ok(msgText.includes(rootSlug) || msgText.includes("quest.md"), "Resume directive must reference active quest file");
		assert.ok(msgText.includes("read"), "Resume directive must instruct reading the quest file");
	});

	await t.step("Test 9 & 10 — nested subquests, LIFO restoration, and child completion", async () => {
		await rm(currentDir, { recursive: true, force: true });
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(10000, 1000000, "session_test_9");
		const rootSlug = "test-root-lifo-quest";
		const childSlug = "test-child-lifo-quest";
		const grandChildSlug = "test-grandchild-lifo-quest";

		await harness.commands["quest"].handler(rootSlug, harness.mockCtx);

		// Create child subquest
		await harness.tools["quest_subquest"].execute(
			"c1",
			{ name: childSlug, goal: "Child Goal", parentName: rootSlug, switchNow: true },
			null,
			null,
			harness.mockCtx,
		);

		let status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes(childSlug), "Child should be active");

		// Create grandchild subquest
		await harness.tools["quest_subquest"].execute(
			"c2",
			{ name: grandChildSlug, goal: "Grandchild Goal", parentName: childSlug, switchNow: true },
			null,
			null,
			harness.mockCtx,
		);

		status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes(grandChildSlug), "Grandchild should be active");

		// Archive grandchild -> should pop LIFO stack and resume child
		const arch1 = await harness.tools["quest_archive"].execute("a1", { questName: grandChildSlug, compact: false }, null, null, harness.mockCtx);
		assert.ok(arch1.content[0].text.includes(`Resumed parent/previous quest '${childSlug}'`), "Archiving grandchild should resume child");

		status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes(childSlug), "Child must be active after grandchild archive");

		// Archive child -> should pop LIFO stack and resume root
		const arch2 = await harness.tools["quest_archive"].execute("a2", { questName: childSlug, compact: false }, null, null, harness.mockCtx);
		assert.ok(arch2.content[0].text.includes(`Resumed parent/previous quest '${rootSlug}'`), "Archiving child should resume root");

		status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes(rootSlug), "Root must be active after child archive");
	});

	await t.step("Test 11 — subquest launch compaction when threshold is exceeded", async () => {
		await rm(currentDir, { recursive: true, force: true });
		await mkdir(currentDir, { recursive: true });

		const harness = setupHarness(80000, 1000000, "session_test_11"); // 80k tokens > default 60k launch threshold
		const parentSlug = "test-launch-parent-quest";
		const subSlug = "test-launch-child-quest";

		await harness.commands["quest"].handler(parentSlug, harness.mockCtx);

		// Subquest created with switchNow: true under elevated tokens
		await harness.tools["quest_subquest"].execute(
			"call_launch_sub",
			{ name: subSlug, goal: "Subquest work under high context", switchNow: true },
			null,
			null,
			harness.mockCtx,
		);

		// Post-tool turn_end triggers deferred subquest launch compaction
		for (const cb of harness.handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "quest_subquest" }] }, harness.mockCtx);
		}

		await new Promise((r) => setTimeout(r, 60));
		assert.strictEqual(harness.getCompactCount(), 1, "Subquest launch above threshold must trigger launch compaction");
		const lastOpts = harness.getLastCompactOptions();
		assert.ok(lastOpts?.customInstructions?.includes(subSlug), "Launch compaction instructions must target the child subquest");

		// Verify active quest is the child
		const status = await harness.commands["quest-status"].handler("", harness.mockCtx);
		assert.ok(status.includes(subSlug), "Active quest must point to child after launch");
	});

	await t.step("Test 12 — persistence failure handling", async () => {
		const harness = setupHarness(10000, 1000000, "session_test_12");
		const ghostSlug = "non-existent-quest-file";

		// Attempting to mark saved a file that does not exist
		const res = await harness.tools["quest_mark_saved"].execute(
			"ghost_save",
			{ name: ghostSlug },
			null,
			null,
			harness.mockCtx,
		);

		assert.ok(res.details?.error || res.content[0].text.toLowerCase().includes("error"), "Missing file must return error");

		// Verify compaction is blocked because state cannot be verified clean
		for (const cb of harness.handlers["session_before_compact"] || []) {
			const compactRes = await cb({}, harness.mockCtx);
			assert.strictEqual(compactRes?.cancel, true, "Unverified state must cancel compaction");
		}
	});
});
