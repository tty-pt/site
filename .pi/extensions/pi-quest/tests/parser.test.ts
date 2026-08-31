import assert from "node:assert";
import { readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { resolveQuestRecordBySlug } from "../src/paths.ts";

Deno.test("quest_journal_parser: robust markdown parsing and structured state update", async () => {
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};

	const mockPi: any = {
		on() {},
		appendEntry() {},
		registerEntryRenderer() {},
		registerTool(tool: any) {
			tools[tool.name] = tool;
		},
		registerCommand(name: string, cmd: any) {
			commands[name] = cmd;
		},
		sendUserMessage() {},
	};

	questJournalExtension(mockPi);

	assert.ok(tools["quest_update_state"], "quest_update_state tool should be registered");

	const mockCtx: any = {
		cwd: "/home/quirinpa/site",
		hasUI: true,
		mode: "tui",
		ui: {
			notify: () => {},
			setStatus: () => {},
		},
		sessionManager: {
			getBranch: () => [],
		},
	};

	const testQuestSlug = "test-parser-quest";

	// Switch to test quest
	await commands["quest"].handler(testQuestSlug, mockCtx);
	const rec = await resolveQuestRecordBySlug(testQuestSlug);
	assert.ok(rec, "Quest record must exist");
	const testQuestPath = rec.path;

	// 1. Test updating state via quest_update_state structured tool
	const updateRes = await tools["quest_update_state"].execute(
		"call_up",
		{
			status: "- [x] Phase 1 Complete · in progress",
			findings: ["Found bottleneck in parser", "AST parsing reduces regex brittleness"],
			decisions: ["Adopt parseMarkdownSections", "Default switchNow to true"],
			remaining: ["Phase 2 execution", "Phase 3 verification"],
			nextStep: "Proceed with Phase 2 implementation",
		},
		null,
		null,
		mockCtx,
	);

	assert.ok(updateRes && updateRes.content[0].text.includes("Successfully updated quest state"), "quest_update_state should succeed");

	// Verify disk contents
	const diskContent = await readFile(testQuestPath, "utf8");
	assert.ok(diskContent.includes("Phase 1 Complete"), "Disk content should have updated status");
	assert.ok(diskContent.includes("AST parsing reduces regex brittleness"), "Disk content should have findings");
	assert.ok(diskContent.includes("Default switchNow to true"), "Disk content should have decisions");
	assert.ok(diskContent.includes("- [ ] Phase 2 execution"), "Disk content should have remaining work checklist");
	assert.ok(diskContent.includes("Proceed with Phase 2 implementation"), "Disk content should have next step");

	// 1b. Test surgical updates on custom document structure
	const complexDoc = [
		"# Quest: test-parser-quest",
		"",
		"## Goal",
		"Original goal",
		"",
		"## Custom Benchmark Matrix",
		"| Engine | Latency | Memory |",
		"| --- | --- | --- |",
		"| Hyle | 1.2ms | 4MB |",
		"",
		"## Current Status",
		"- [ ] not started",
		"",
		"## Unmanaged Appendix",
		"Appendix notes that must survive.",
	].join("\n");

	await writeFile(testQuestPath, complexDoc, "utf8");

	await tools["quest_update_state"].execute(
		"call_up2",
		{
			status: "- [x] Done",
			nextStep: "Ship to production",
		},
		null,
		null,
		mockCtx,
	);

	const updatedComplex = await readFile(testQuestPath, "utf8");
	assert.ok(updatedComplex.includes("## Custom Benchmark Matrix"), "Must preserve custom table section header");
	assert.ok(updatedComplex.includes("| Hyle | 1.2ms | 4MB |"), "Must preserve custom table content");
	assert.ok(updatedComplex.includes("## Unmanaged Appendix"), "Must preserve appendix section header");
	assert.ok(updatedComplex.includes("Appendix notes that must survive."), "Must preserve appendix body");
	assert.ok(updatedComplex.includes("- [x] Done"), "Must update status");
	assert.ok(updatedComplex.includes("Ship to production"), "Must add/update next step");

	// Cleanup
	await rm(testQuestPath, { force: true });

	// 2. Test Multi-Session State Isolation
	const ctxSessionA: any = {
		cwd: "/home/quirinpa/site",
		hasUI: true,
		sessionManager: { id: "session_A", getBranch: () => [] },
		ui: { notify: () => {}, setStatus: () => {} },
	};
	const ctxSessionB: any = {
		cwd: "/home/quirinpa/site",
		hasUI: true,
		sessionManager: { id: "session_B", getBranch: () => [] },
		ui: { notify: () => {}, setStatus: () => {} },
	};

	await commands["quest"].handler("quest-session-a", ctxSessionA);
	await commands["quest"].handler("quest-session-b", ctxSessionB);

	// Verify session A and session B maintain their own active quests
	const statusA = await commands["quest-status"].handler("", ctxSessionA);
	const statusB = await commands["quest-status"].handler("", ctxSessionB);

	assert.ok(statusA.includes("quest-session-a"), "Session A should have quest-session-a active");
	assert.ok(statusB.includes("quest-session-b"), "Session B should have quest-session-b active");

	// 3. Test Interleaved Asynchronous Execution (Race Condition Prevention via AsyncLocalStorage)
	const interleaveTaskA = async () => {
		await commands["quest"].handler("quest-session-a", ctxSessionA);
		// Delay to allow task B to run concurrently
		await new Promise((resolve) => setTimeout(resolve, 20));
		return await commands["quest-status"].handler("", ctxSessionA);
	};

	const interleaveTaskB = async () => {
		// Small delay to start during task A's execution
		await new Promise((resolve) => setTimeout(resolve, 5));
		await commands["quest"].handler("quest-session-b", ctxSessionB);
		return await commands["quest-status"].handler("", ctxSessionB);
	};

	const [finalStatusA, finalStatusB] = await Promise.all([interleaveTaskA(), interleaveTaskB()]);

	assert.ok(finalStatusA.includes("quest-session-a"), "Interleaved Task A must remain on quest-session-a");
	assert.ok(finalStatusB.includes("quest-session-b"), "Interleaved Task B must remain on quest-session-b");

	await rm(".pi/quest/current", { recursive: true, force: true });
});
