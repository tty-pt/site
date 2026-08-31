import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_enforced_gates: complete verification of all 15 behavioral promise gates", async (t) => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const rootSlug = "test-enforced-root-quest";
	const rootPath = `${currentDir}/${rootSlug}.md`;
	const childSlug = "test-enforced-child-quest";
	const childPath = `${currentDir}/${childSlug}.md`;

	await rm(rootPath, { force: true });
	await rm(childPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];

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

	let currentTokens = 10000;
	let currentContextWindow = 1000000;

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
		ui: {
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "headless",
	};

	const emitToolCall = async (toolName: string, input?: any) => {
		for (const cb of handlers["tool_call"] || []) {
			const res = await cb({ toolName, input }, mockCtx);
			if (res) return res;
		}
		return null;
	};

	const emitToolResult = async (toolName: string, input?: any, output?: any, isError = false) => {
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName, input, output, isError }, mockCtx);
		}
	};

	// -----------------------------------------------------------------------
	// 1. New root quest cannot edit before research
	// -----------------------------------------------------------------------
	await t.step("1. new root quest cannot edit before research", async () => {
		await commands["quest"].handler(rootSlug, mockCtx);

		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Must return a gate result for mutation tool");
		assert.strictEqual(blockRes.block, true, "Implementation tool must be blocked before research");
		assert.ok(blockRes.reason.includes("RESEARCH_PENDING"), "Reason must identify RESEARCH_PENDING state");
		assert.ok(blockRes.reason.includes("quest_update_state"), "Reason must specify how to resolve");
	});

	// -----------------------------------------------------------------------
	// 2. Investigation/read tools remain available during research
	// -----------------------------------------------------------------------
	await t.step("2. investigation/read tools remain available during research", async () => {
		const readRes = await emitToolCall("read", { path: "mods/song/player.c" });
		assert.strictEqual(readRes?.block, undefined, "read tool must not be blocked");
		await emitToolResult("read", { path: "mods/song/player.c" }, "player code");

		const searchRes = await emitToolCall("search_code", { pattern: "player_init" });
		assert.strictEqual(searchRes?.block, undefined, "search tool must not be blocked");

		const bashRes = await emitToolCall("bash", { command: "git status" });
		assert.strictEqual(bashRes?.block, undefined, "bash inspection must not be blocked");
	});

	// -----------------------------------------------------------------------
	// 3. Completed valid research unlocks the next stage
	// -----------------------------------------------------------------------
	await t.step("3. completed valid research unlocks the next stage", async () => {
		const updateRes = await tools["quest_update_state"].execute(
			"call_1",
			{
				goal: "Implement test-enforced-root-quest with streaming support",
				status: "Research complete",
				understanding: "Audio subsystem uses libxylem with streaming buffers in hyle.",
				assumptions: "- Assumption 1: Buffer size 4096 is sufficient.\n- Assumption 2: Zero heap allocations in render loop.",
				openQuestions: "No critical blockers remaining; memory and latency constraints verified.",
				findings: ["Buffer streaming works seamlessly with axil chunking."],
				plan: "1. Create stream buffer\n2. Integrate with axil\n3. Verify with make test",
				planConfidence: "high",
				exactNextAction: "Ask user for confirmation before touching player.c",
				researchComplete: true,
			},
			null,
			null,
			mockCtx,
		);

		assert.ok(!updateRes.content[0].text.includes("refused"), "Valid research must be accepted");
	});

	// -----------------------------------------------------------------------
	// 4. Root quest cannot implement before required confirmation
	// -----------------------------------------------------------------------
	await t.step("4. root quest cannot implement before required confirmation", async () => {
		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Root quest before user confirmation must be blocked");
		assert.strictEqual(blockRes.block, true, "Root quest must be blocked until confirmed");
		assert.ok(blockRes.reason.includes("CONFIRMATION_PENDING"), "Reason must cite CONFIRMATION_PENDING");
	});

	// -----------------------------------------------------------------------
	// 5. Explicit confirmation unlocks root implementation
	// -----------------------------------------------------------------------
	await t.step("5. explicit confirmation unlocks root implementation", async () => {
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Looks good, go ahead and implement" }, mockCtx);
		}

		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Implementation must be allowed after confirmation");
	});

	// -----------------------------------------------------------------------
	// 6. ok / continue / thanks do not trigger reassessment
	// -----------------------------------------------------------------------
	await t.step("6. ok / continue / thanks do not trigger reassessment", async () => {
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "ok thanks" }, mockCtx);
		}

		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Conversational ack must not block implementation");
	});

	// -----------------------------------------------------------------------
	// 7. Ordinary questions do not become refinements
	// -----------------------------------------------------------------------
	await t.step("7. ordinary questions do not become refinements", async () => {
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "What is the size of the streaming buffer?" }, mockCtx);
		}

		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Ordinary question must not block implementation");
	});

	// -----------------------------------------------------------------------
	// 8. A real refinement triggers reassessment
	// -----------------------------------------------------------------------
	await t.step("8. a real refinement triggers reassessment", async () => {
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Also add support for Opus audio decoding in the player" }, mockCtx);
		}

		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Must block implementation after refinement");
		assert.strictEqual(blockRes.block, true, "Implementation must be blocked while reassessment is required");
		assert.ok(blockRes.reason.includes("REASSESSMENT_PENDING"), "Reason must state REASSESSMENT_PENDING");
	});

	// -----------------------------------------------------------------------
	// 9. Reassessment blocks implementation
	// -----------------------------------------------------------------------
	await t.step("9. reassessment blocks implementation", async () => {
		const blockRes = await emitToolCall("write", { path: "mods/song/opus.c" });
		assert.ok(blockRes, "write tool must also be blocked during reassessment");
		assert.strictEqual(blockRes.block, true, "write must be blocked");
	});

	// -----------------------------------------------------------------------
	// 10. Resolving reassessment unlocks implementation
	// -----------------------------------------------------------------------
	await t.step("10. resolving reassessment unlocks implementation", async () => {
		await emitToolCall("read", { path: "mods/song/opus.h" });
		await emitToolResult("read", { path: "mods/song/opus.h" }, "opus header code");

		const updateRes = await tools["quest_update_state"].execute(
			"call_2",
			{
				understanding: "Audio player supports Opus decoding via external libopus wrapper.",
				assumptions: "- Opus decoder latency is <5ms.",
				openQuestions: "No remaining open questions.",
				findings: ["libopus integration verified via XY module."],
				plan: "1. Add Opus decoder\n2. Test with sample file",
				planConfidence: "high",
				exactNextAction: "Implement Opus decoder wrapper in player.c",
				reassessmentComplete: true,
				reassessmentConclusion: "Investigated libopus integration; architecture supports it directly without changes to memory model.",
			},
			null,
			null,
			mockCtx,
		);

		assert.ok(!updateRes.content[0].text.includes("refused"), "Reassessment resolution must succeed");

		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Implementation must be allowed after resolving reassessment");
	});

	// -----------------------------------------------------------------------
	// 11. Reconstruction never accidentally grants implementation
	// -----------------------------------------------------------------------
	await t.step("11. reconstruction never accidentally grants implementation", async () => {
		// Simulate a branch where research was required
		const branchEntries = [
			{
				type: "custom",
				customType: "quest_journal",
				data: {
					active: rootSlug,
					stack: [rootSlug],
					researchRequired: true,
					researchComplete: false,
					reassessmentRequired: false,
					saveCount: 1,
					compactCount: 0,
					dirty: true,
				},
			},
		];

		mockCtx.sessionManager.getBranch = () => branchEntries;

		for (const cb of handlers["session_start"] || []) {
			await cb({}, mockCtx);
		}

		const blockRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.ok(blockRes, "Reconstruction must enforce research requirement");
		assert.strictEqual(blockRes.block, true, "Implementation must remain blocked after restart");
	});

	// -----------------------------------------------------------------------
	// 12. Stale quest blocks compaction
	// -----------------------------------------------------------------------
	await t.step("12. stale quest blocks compaction", async () => {
		let cancelRes: any;
		for (const cb of handlers["session_before_compact"] || []) {
			cancelRes = await cb({}, mockCtx);
		}
		assert.strictEqual(cancelRes?.cancel, true, "Dirty quest must block compaction");
	});

	// -----------------------------------------------------------------------
	// 13. Verified save allows compaction
	// -----------------------------------------------------------------------
	await t.step("13. verified save allows compaction", async () => {
		// Save quest
		await tools["quest_mark_saved"].execute("save_root", {}, null, null, mockCtx);

		// Now compaction should be allowed
		let allowRes: any;
		for (const cb of handlers["session_before_compact"] || []) {
			allowRes = await cb({}, mockCtx);
		}
		assert.notStrictEqual(allowRes?.cancel, true, "Clean verified quest must allow compaction");
	});

	// -----------------------------------------------------------------------
	// 14. Subquest execution remains autonomous where intended
	// -----------------------------------------------------------------------
	await t.step("14. subquest execution remains autonomous where intended", async () => {
		const subRes = await tools["quest_subquest"].execute(
			"call_sub",
			{
				name: childSlug,
				goal: "Investigate and implement opus decoder ring buffer",
				switchNow: true,
			},
			null,
			null,
			mockCtx,
		);
		assert.ok(!subRes.details?.error);

		// Sub-quest research is pending initially
		const blockRes = await emitToolCall("edit", { path: "mods/song/ring_buffer.c" });
		assert.strictEqual(blockRes?.block, true, "Sub-quest must research first");

		await emitToolCall("read", { path: "mods/song/ring_buffer.h" });
		await emitToolResult("read", { path: "mods/song/ring_buffer.h" }, "ring buffer header code");

		// Complete sub-quest research
		await tools["quest_update_state"].execute(
			"call_sub_update",
			{
				understanding: "Ring buffer sizing requires 8KB chunks.",
				assumptions: "- Sizing meets audio requirements.",
				openQuestions: "No remaining open questions.",
				findings: ["8KB chunks confirmed."],
				plan: "1. Implement ring buffer",
				planConfidence: "high",
				exactNextAction: "Write ring_buffer.c",
				researchComplete: true,
			},
			null,
			null,
			mockCtx,
		);

		// Autonomous: subquest does NOT need human confirmation
		const allowRes = await emitToolCall("edit", { path: "mods/song/ring_buffer.c" });
		assert.strictEqual(allowRes?.block, undefined, "Sub-quest can implement immediately after research");
	});

	// -----------------------------------------------------------------------
	// 15. Returning from a subquest correctly restores parent epistemic state
	// -----------------------------------------------------------------------
	await t.step("15. returning from a subquest correctly restores parent epistemic state", async () => {
		// Archive sub-quest
		await tools["quest_archive"].execute("call_arch", { questName: childSlug }, null, null, mockCtx);

		// Parent had completed research and confirmation before sub-quest, so returning restores mature state
		const allowRes = await emitToolCall("edit", { path: "mods/song/player.c" });
		assert.strictEqual(allowRes?.block, undefined, "Parent restored its mature permission state");
	});

	// Clean up
	await rm(rootPath, { force: true });
	await rm(childPath, { force: true });
});
