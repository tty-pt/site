import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { questPath, resolveQuestRecordBySlug } from "../src/paths.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_lifecycle_e2e: complete 12-step autonomous compaction and resumption lifecycle", async () => {
	const currentDir = ".pi/quest/current";
	await rm(currentDir, { recursive: true, force: true });
	await mkdir(currentDir, { recursive: true });

	const questSlug = "e2e-lifecycle-economy-quest";

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
	const uiNotifications: Array<{ msg: string; level?: string }> = [];
	let compactInvocationCount = 0;
	let lastCompactOptions: any = null;

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
			id: "session_lifecycle_e2e",
			getBranch: () => [],
		},
		compact: (opts: any) => {
			compactInvocationCount++;
			lastCompactOptions = opts;
		},
		ui: {
			notify(msg: string, level?: string) {
				uiNotifications.push({ msg, level });
			},
			setStatus() {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// -----------------------------------------------------------------------
	// Step 1: Start with no active quest
	// -----------------------------------------------------------------------
	const initialStatus = await commands["quest-status"].handler("", mockCtx);
	assert.ok(initialStatus.includes("No active quest"), "Step 1: Must start with no active quest");

	// -----------------------------------------------------------------------
	// Step 2: Substantial coding objective enters provisional state, then agent initializes quest
	// -----------------------------------------------------------------------
	const objective = "Refactor song multi-select filter and implement strict type bounds in mods/song/song.c";
	for (const cb of handlers["before_agent_start"] || []) {
		await cb({ prompt: objective, systemPrompt: "Base prompt." }, mockCtx);
	}

	const statusProvisional = await commands["quest-status"].handler("", mockCtx);
	assert.ok(statusProvisional.includes("PROVISIONAL ROOT INITIALIZATION"), "Step 2: Must enter provisional root initialization");

	// Agent performs initial research and initializes durable quest with semantic name
	const activeSlug = "song-filter-type-bounds";
	await tools["quest_update_state"].execute(
		"call_init",
		{
			name: activeSlug,
			goal: objective,
			status: "Research complete",
			understanding: "Song multi-select filter requires type-bound checks in mods/song/song.c.",
			assumptions: ["All song filters use standard state macros"],
			openQuestions: ["None"],
			findings: ["Buffer handling is type-safe"],
			plan: ["1. Add strict bounds", "2. Run tests"],
			planConfidence: "high",
			exactNextAction: "Add type-bounds checks to mods/song/song.c",
			researchComplete: true,
		},
		null,
		null,
		mockCtx,
	);

	const statusAfterPrompt = await commands["quest-status"].handler("", mockCtx);
	assert.ok(statusAfterPrompt.includes(activeSlug), "Step 2: Quest state initialized on disk");

	const rec = await resolveQuestRecordBySlug(activeSlug);
	assert.ok(rec, "Step 2: Quest record must exist");
	const activeQuestPath = rec.path;

	const diskContentInitial = await readFile(activeQuestPath, "utf8");
	assert.ok(diskContentInitial.includes("## Goal"), "Step 2: Template must include '## Goal'");
	assert.ok(diskContentInitial.includes("## Original request"), "Step 2: Template must include '## Original request'");
	assert.ok(diskContentInitial.includes("## Current Understanding"), "Step 2: Template must include '## Current Understanding'");
	assert.ok(diskContentInitial.includes("## Exact Next Action"), "Step 2: Template must include '## Exact Next Action'");

	// -----------------------------------------------------------------------
	// Step 3 & 4: Agent works across substantive turns (continuous durable memory without artificial checkpoints)
	// -----------------------------------------------------------------------
	userMessages.length = 0;
	for (let turn = 0; turn < 4; turn++) {
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
		}
	}

	const incMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Quest Incremental Checkpoint"),
	);
	assert.strictEqual(incMsgs.length, 0, "Step 4: No artificial incremental checkpoint during normal execution");

	// -----------------------------------------------------------------------
	// Step 5 & 6: Periodic checkpoint after 6 substantive turns
	// -----------------------------------------------------------------------
	userMessages.length = 0;
	for (let i = 0; i < 6; i++) {
		for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: `mods/song/song${i}.c` } }, mockCtx);
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit", input: { path: `mods/song/song${i}.c` } }] }, mockCtx);
		}
	}

	const preCompactMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Periodic Durable Checkpoint"),
	);
	assert.strictEqual(preCompactMsgs.length, 1, "Step 6: Periodic checkpoint must be sent after 6 turns");
	assert.ok(
		preCompactMsgs[0].msg.includes("EXACT NEXT ACTION"),
		"Step 6: Protocol must mandate 'EXACT NEXT ACTION'",
	);

	// -----------------------------------------------------------------------
	// Step 7: Agent saves rich execution snapshot and calls quest_mark_saved
	// -----------------------------------------------------------------------
	const richSnapshotMarkdown = [
		`# Quest: ${activeSlug}`,
		"",
		"## Goal",
		objective,
		"",
		"## Original request",
		`> ${objective}`,
		"",
		"## Current Status",
		"- [x] Phase 1 Analysis complete · in progress",
		"",
		"## Execution Snapshot",
		"",
		"### Objective",
		`> ${objective}`,
		"",
		"### Completed",
		"- Analyzed mods/song/song.c multi-select handler.",
		"- Added unit tests for filter boundary checking.",
		"",
		"### In Progress",
		"- Integrating renderEscapedLabel() for safe DOM output.",
		"",
		"### Important Discoveries",
		"- Found that bud_picker_collect fails when options have unescaped HTML characters.",
		"- The C XY module boundary requires XY_IMPL in song.c and XY_DECL in song.h.",
		"",
		"### Decisions",
		"- Keep all UX isomorphic in mods/song/ux/song_ux.c.",
		"- Avoid any JavaScript additions; use pure C SSR hooks.",
		"",
		"### Constraints",
		"- No-JS fallback must remain 100% functional.",
		"- Accent-sensitive search must match axil_slugify.",
		"",
		"### Files Examined",
		"- mods/song/song.c",
		"- mods/song/song.h",
		"- mods/song/ux/song_ux.c",
		"",
		"### Files Modified",
		"- mods/song/song.c",
		"- mods/song/ux/song_ux.c",
		"",
		"### Test / Build Status",
		"- Build: make -> 0 errors, 0 warnings.",
		"- Targeted tests: AUTH_SKIP_CONFIRM=1 deno test tests/e2e/song-type.test.ts -> PASSED.",
		"",
		"### Known Problems / Uncertainties",
		"- Need to verify picker dropdown keyboard navigation in Firefox.",
		"",
		"### Remaining Work",
		"- [ ] Update song_ux.c render loop.",
		"- [ ] Run full test suite: make test.",
		"",
		"### Exact Next Action",
		"> Open mods/song/ux/song_ux.c and replace raw string formatting with renderEscapedLabel() at line 142; then run make.",
		"",
		"### Resume Context",
		"> Working on song multi-select filter. Phase 1 complete. Discoveries and decisions documented above. Next action is editing line 142 in mods/song/ux/song_ux.c.",
		"",
		"## Sub-Quests",
		"- [ ] None",
		"",
		"## Quest Refinements & User Feedback Loops",
		"- None",
		"",
		"## Resume Context",
		"> Briefing.",
	].join("\n");

	await writeFile(activeQuestPath, richSnapshotMarkdown, "utf8");
	const markRes = await tools["quest_mark_saved"].execute("call_mark", { name: activeSlug }, null, null, mockCtx);
	assert.ok(markRes.content[0].text.includes("verified and marked as saved"), "Step 7: Quest must be verified and marked saved");

	// -----------------------------------------------------------------------
	// Step 8, 9, 10: Tokens reach 335k -> Auto economy compaction -> session_compact sends resume directive
	// -----------------------------------------------------------------------
	currentTokens = 335000; // >= 333k threshold
	userMessages.length = 0;

	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}

	// Verify session_before_compact allows compaction when clean
	let beforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		beforeCompactRes = await cb({}, mockCtx);
	}
	assert.notStrictEqual(beforeCompactRes?.cancel, true, "Step 8: session_before_compact must allow compaction when clean");

	// Simulate Pi emitting session_compact event (single authoritative completion path)
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}

	// Step 9: Verify session_compact sends the Post-Compaction Autonomous Resumption Directive
	const resumeDirectives = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Post-Compaction Autonomous Resumption Directive"),
	);
	assert.strictEqual(resumeDirectives.length, 1, "Step 9: session_compact MUST send the resumption directive");
	assert.ok(
		resumeDirectives[0].options?.deliverAs === "followUp" || !resumeDirectives[0].options,
		"Step 9: Resume directive must be delivered to agent",
	);
	assert.ok(
		resumeDirectives[0].msg.includes(activeSlug) || resumeDirectives[0].msg.includes("quest.md"),
		"Step 9: Directive must name active quest file",
	);

	// -----------------------------------------------------------------------
	// Step 11 & 12: Next agent turn starts -> System Prompt includes rich extracted Execution Snapshot
	// -----------------------------------------------------------------------
	let nextAgentPrompt = "";
	for (const cb of handlers["before_agent_start"] || []) {
		const res = await cb({ prompt: "Continue objective", systemPrompt: "Base prompt." }, mockCtx);
		if (res?.systemPrompt) nextAgentPrompt = res.systemPrompt;
	}

	assert.ok(nextAgentPrompt.includes("Active Quest Resume Context"), "Step 11: Next turn system prompt must include Active Quest Resume Context");
	assert.ok(nextAgentPrompt.includes("### Execution Snapshot"), "Step 11: Resume context must prioritize Execution Snapshot");
	assert.ok(nextAgentPrompt.includes("renderEscapedLabel()"), "Step 12: Resume context must contain exact technical details from snapshot");
	assert.ok(nextAgentPrompt.includes("Open mods/song/ux/song_ux.c and replace raw string formatting"), "Step 12: Resume context must contain EXACT NEXT ACTION");
});

Deno.test("quest_journal_lifecycle_e2e: subquest launch compaction and archive compaction autonomous resumption", async () => {
	const currentDir = ".pi/quest/current";
	await rm(currentDir, { recursive: true, force: true });
	await mkdir(currentDir, { recursive: true });

	const parentSlug = "e2e-subquest-parent";
	const childSlug = "e2e-subquest-child";

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
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
			id: "session_subquest_lifecycle_e2e",
			getBranch: () => [],
		},
		compact: (opts: any) => {
			compactInvocationCount++;
			lastCompactOptions = opts;
		},
		ui: {
			notify() {},
			setStatus() {},
			input: async () => "",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 1. Initialize parent quest
	await commands["quest"].handler(parentSlug, mockCtx);

	// 2. Set tokens above subquest launch threshold (default 60k)
	currentTokens = 75000;
	compactInvocationCount = 0;
	userMessages.length = 0;

	// Launch subquest with switchNow: true
	await tools["quest_subquest"].execute(
		"call_sub_launch",
		{
			name: childSlug,
			goal: "Child subquest investigation",
			switchNow: true,
		},
		null,
		null,
		mockCtx,
	);

	// Normal lifecycle turn_end triggers deferred launch compaction
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "quest_subquest" }] }, mockCtx);
	}

	// Wait for setTimeout in subquest launch compaction
	await new Promise((resolve) => setTimeout(resolve, 80));
	assert.strictEqual(compactInvocationCount, 1, "Subquest launch compaction must trigger ctx.compact");

	// 3. Simulate Pi emitting session_compact event on completion
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}

	// Verify child quest resumption directive is sent directly from session_compact
	const childResumeMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Post-Compaction Autonomous Resumption Directive"),
	);
	assert.strictEqual(childResumeMsgs.length, 1, "Subquest launch session_compact MUST resume the child quest");
	assert.ok(
		childResumeMsgs[0].msg.includes(childSlug) || childResumeMsgs[0].msg.includes(".md"),
		"Resumption directive must target child quest",
	);

	// 4. Child subquest completes and is archived with compact: true
	currentTokens = 85000;
	compactInvocationCount = 0;
	userMessages.length = 0;

	await tools["quest_archive"].execute(
		"call_child_archive",
		{
			name: childSlug,
			compact: true,
		},
		null,
		null,
		mockCtx,
	);

	// Normal lifecycle turn_end triggers deferred archive compaction
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "quest_archive" }] }, mockCtx);
	}

	// Wait for setTimeout in archive compaction
	await new Promise((resolve) => setTimeout(resolve, 80));
	assert.strictEqual(compactInvocationCount, 1, "Subquest archive compaction must trigger ctx.compact");

	// 5. Simulate Pi emitting session_compact event on archive completion
	for (const cb of handlers["session_compact"] || []) {
		await cb({}, mockCtx);
	}

	// Verify parent quest resumption directive is sent directly from session_compact
	const parentResumeMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Post-Compaction Autonomous Resumption Directive"),
	);
	assert.strictEqual(parentResumeMsgs.length, 1, "Archive session_compact MUST automatically resume the parent quest");
	assert.ok(
		parentResumeMsgs[0].msg.includes(parentSlug) || parentResumeMsgs[0].msg.includes("quest.md"),
		"Resumption directive must target parent quest",
	);
});
