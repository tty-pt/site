import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_lifecycle_e2e: complete 12-step autonomous compaction and resumption lifecycle", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const questSlug = "e2e-lifecycle-economy-quest";
	const questPath = `docs/current/${questSlug}.md`;
	await rm(questPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any }> = [];
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
	// Step 2: Substantial coding objective initializes quest with Execution Snapshot template
	// -----------------------------------------------------------------------
	const objective = "Refactor song multi-select filter and implement strict type bounds in mods/song/song.c";
	for (const cb of handlers["before_agent_start"] || []) {
		await cb({ prompt: objective, systemPrompt: "Base prompt." }, mockCtx);
	}

	const statusAfterPrompt = await commands["quest-status"].handler("", mockCtx);
	const activeSlugMatch = statusAfterPrompt.match(/docs\/current\/([^.\s]+)\.md/);
	assert.ok(activeSlugMatch, "Step 2: Substantive prompt must automatically create active quest");
	const activeSlug = activeSlugMatch[1];
	const activeQuestPath = `docs/current/${activeSlug}.md`;

	const diskContentInitial = await readFile(activeQuestPath, "utf8");
	assert.ok(diskContentInitial.includes("## Execution Snapshot"), "Step 2: Template must include '## Execution Snapshot'");
	assert.ok(diskContentInitial.includes("### Objective"), "Step 2: Template must include '### Objective'");
	assert.ok(diskContentInitial.includes("### Important Discoveries"), "Step 2: Template must include '### Important Discoveries'");
	assert.ok(diskContentInitial.includes("### Exact Next Action"), "Step 2: Template must include '### Exact Next Action'");

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
	// Step 5 & 6: Tokens enter warning window (no message) -> Compaction Boundary triggers Final Save Directive
	// -----------------------------------------------------------------------
	await commands["quest-economy"].handler("333k 30k", mockCtx); // Warning threshold: 303k, Compaction: 333k
	currentTokens = 310000; // >= 303k, < 333k
	userMessages.length = 0;

	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
	}

	assert.strictEqual(userMessages.length, 0, "Step 5: Warning window must NOT send disruptive messages");

	// Now tokens reach 335k (compaction threshold)
	currentTokens = 335000;
	userMessages.length = 0;

	let beforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		beforeCompactRes = await cb({}, mockCtx);
	}
	assert.strictEqual(beforeCompactRes?.cancel, true, "Step 6: session_before_compact must cancel when dirty");

	const preCompactMsgs = userMessages.filter((m) =>
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("Context compaction is now being requested") ||
		(typeof m.msg === "string" ? m.msg : m.msg?.text || "").includes("FINAL EXHAUSTIVE DURABLE STATE SAVE"),
	);
	assert.strictEqual(preCompactMsgs.length, 1, "Step 6: Pre-compaction deep save protocol must be sent at compaction boundary");
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
	// Step 8, 9, 10: Tokens reach 335k -> Auto economy compaction -> onComplete sends resume directive
	// -----------------------------------------------------------------------
	currentTokens = 335000; // >= 333k threshold
	compactInvocationCount = 0;
	userMessages.length = 0;

	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [] }, mockCtx);
	}

	// Wait for setTimeout in checkAndTriggerEconomyCompaction
	await new Promise((resolve) => setTimeout(resolve, 80));
	assert.strictEqual(compactInvocationCount, 1, "Step 8: Economy auto-compaction must be triggered");

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
		resumeDirectives[0].msg.includes(`docs/current/${activeSlug}.md`),
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

	// Clean up
	await rm(activeQuestPath, { force: true });
});

Deno.test("quest_journal_lifecycle_e2e: subquest launch compaction and archive compaction autonomous resumption", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const parentSlug = "e2e-subquest-parent";
	const parentPath = `docs/current/${parentSlug}.md`;
	const childSlug = "e2e-subquest-child";
	const childPath = `docs/current/${childSlug}.md`;

	await rm(parentPath, { force: true });
	await rm(childPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any }> = [];
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
	await writeFile(parentPath, `# Quest: ${parentSlug}\n\n## Goal\nParent Goal\n\n## Original request\n> Parent Goal\n\n## Current Status\n- [ ] in progress\n`, "utf8");
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
		childResumeMsgs[0].msg.includes(`docs/current/${childSlug}.md`),
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
		parentResumeMsgs[0].msg.includes(`docs/current/${parentSlug}.md`),
		"Resumption directive must target parent quest",
	);

	// Clean up
	await rm(parentPath, { force: true });
	await rm(childPath, { force: true });
	await rm(`docs/archive/${childSlug}.md`, { force: true });
});
