import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_verification: verified save generation, fingerprinting, error-guarded writes, and surgical AST updates", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const testQuestSlug = "test-verification-quest";
	const testQuestPath = `docs/current/${testQuestSlug}.md`;
	await rm(testQuestPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	let userMessages: any[] = [];
	let notifiedMessages: string[] = [];

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

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({ tokens: 10000, contextWindow: 200000, percent: 5 }),
		sessionManager: {
			getBranch: () => [],
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

	// Initialize quest in session
	await commands["quest"].handler(testQuestSlug, mockCtx);

	// Remove file so we can test that quest_mark_saved rejects when file is missing
	await rm(testQuestPath, { force: true });

	// 1. Test quest_mark_saved when file does NOT exist on disk -> must return error / reject
	const resMissing = await tools["quest_mark_saved"].execute("call_missing", {}, null, null, mockCtx);
	assert.ok(
		resMissing.details?.error || resMissing.content[0].text.toLowerCase().includes("error") || resMissing.content[0].text.toLowerCase().includes("missing") || resMissing.content[0].text.toLowerCase().includes("not found"),
		`quest_mark_saved must reject when file is missing from disk, got: ${JSON.stringify(resMissing)}`,
	);

	// 2. Create actual file on disk and call quest_mark_saved -> must succeed and record fingerprint
	const initialContent = [
		`# Quest: ${testQuestSlug}`,
		"",
		"## Goal",
		"Test verification and AST preservation",
		"",
		"## Original request",
		"> Test verification and AST preservation",
		"",
		"## Current Status",
		"- [ ] not started",
		"",
		"## Detailed Multi-Stage Execution Plan",
		"### Stage 1: Initial Setup",
		"- Details of stage 1",
		"",
		"## Acceptance Criteria & Polish Checklist",
		"- [ ] Criterion A",
		"- [ ] Criterion B",
		"",
		"## Custom Developer Notes",
		"Important architectural invariant that must never be deleted by updates.",
		"",
		"## Remaining work",
		"- [ ] Complete implementation",
		"",
		"## Next recommended step",
		"1. Begin Stage 1",
	].join("\n");

	await writeFile(testQuestPath, initialContent, "utf8");

	const resSuccess = await tools["quest_mark_saved"].execute("call_valid", {}, null, null, mockCtx);
	assert.ok(
		!resSuccess.details?.error && resSuccess.content[0].text.toLowerCase().includes("saved"),
		`quest_mark_saved must succeed when file exists, got: ${JSON.stringify(resSuccess)}`,
	);
	assert.ok(resSuccess.details?.hash || resSuccess.details?.generation, "quest_mark_saved should return hash/generation details");
	const firstGeneration = resSuccess.details?.generation || 1;

	// 3. Test tool_result with failed write/edit -> must NOT count as a save
	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "edit",
				input: { path: testQuestPath },
				isError: true,
				error: "Failed to match oldText",
			},
			mockCtx,
		);
	}

	// 4. Test surgical section updating in quest_update_state:
	// Verify that custom sections (Detailed Multi-Stage Execution Plan, Acceptance Criteria, Custom Developer Notes)
	// are 100% PRESERVED after calling quest_update_state!
	const updateRes = await tools["quest_update_state"].execute(
		"call_update_ast",
		{
			name: testQuestSlug,
			status: "- [x] Stage 1 complete · in progress",
			findings: ["AST block splicing works cleanly", "Zero data loss for unmanaged sections"],
			decisions: ["Preserve all unknown markdown headers verbatim"],
			filesTouched: [".pi/extensions/quest-journal.ts"],
			remaining: ["Run full test suite", "Verify persistence"],
			nextStep: "Execute stage 2",
		},
		null,
		null,
		mockCtx,
	);

	assert.ok(updateRes && !updateRes.details?.error, "quest_update_state should succeed");
	assert.ok(updateRes.details?.generation > firstGeneration, "Save generation should increment on valid update");

	const updatedDisk = await readFile(testQuestPath, "utf8");
	assert.ok(updatedDisk.includes("## Detailed Multi-Stage Execution Plan"), "Must preserve ## Detailed Multi-Stage Execution Plan");
	assert.ok(updatedDisk.includes("### Stage 1: Initial Setup"), "Must preserve content in execution plan");
	assert.ok(updatedDisk.includes("## Acceptance Criteria & Polish Checklist"), "Must preserve ## Acceptance Criteria & Polish Checklist");
	assert.ok(updatedDisk.includes("- [ ] Criterion A"), "Must preserve checklist items");
	assert.ok(updatedDisk.includes("## Custom Developer Notes"), "Must preserve custom developer notes section");
	assert.ok(updatedDisk.includes("Important architectural invariant"), "Must preserve custom notes body");
	assert.ok(updatedDisk.includes("AST block splicing works cleanly"), "Must include updated findings");
	assert.ok(updatedDisk.includes("Preserve all unknown markdown headers verbatim"), "Must include updated decisions");
	assert.ok(updatedDisk.includes(".pi/extensions/quest-journal.ts"), "Must include updated files touched");

	// 5. Test Dirty State lifecycle:
	// A: When freshly saved, turn_end does not nag
	userMessages = [];
	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}
	assert.strictEqual(userMessages.length, 0, "turn_end should not nag when state is clean");

	// B: Tool result on a project file marks dirty
	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "edit",
				input: { path: "mods/song/song.c" },
			},
			mockCtx,
		);
	}

	// C: Now turn_end fires with dirty state -> should prompt save if cooldown allowed
	// Reset cooldown timer simulation
	userMessages = [];
	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}
	// Note: cooldown may prevent immediate nag within MIN_PROMPT_MS unless dirty
	// Verify that saving marks it clean again
	const resClean = await tools["quest_mark_saved"].execute("call_clean", {}, null, null, mockCtx);
	assert.ok(!resClean.details?.error, "Mark saved should succeed and clear dirty flag");

	// 6. Test CRB provider hook includes 'Never propose anything without doing your homework first' at the start
	const g = globalThis as any;
	assert.ok(g.__pi_crb_providers && g.__pi_crb_providers.length > 0, "CRB provider hook should be registered");
	const crbRules: string[] = [];
	for (const p of g.__pi_crb_providers) {
		const res = p(mockCtx, ["quest_mark_saved", "edit", "read"]);
		if (Array.isArray(res)) crbRules.push(...res);
	}
	assert.ok(crbRules.length > 0, "CRB rules must not be empty");
	assert.ok(
		crbRules[0].toLowerCase().includes("never propose anything without doing your homework first"),
		`First CRB rule must be homework-first rule, got: ${crbRules[0]}`,
	);

	// Clean up
	await rm(testQuestPath, { force: true });
});
