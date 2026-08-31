import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import questJournalExtension from "../index.ts";
import { resolveQuestRecordBySlug } from "../src/paths.ts";

interface BeforeAgentStartResult {
	systemPrompt?: string;
}

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function getQuestContentBySlug(slug: string): Promise<string> {
	const rec = await resolveQuestRecordBySlug(slug);
	if (!rec) throw new Error(`Quest record not found for slug: ${slug}`);
	return readFile(rec.path, "utf8");
}

async function testSubQuestFeature() {
	const currentDir = ".pi/quest/current";
	await rm(currentDir, { recursive: true, force: true });
	await mkdir(currentDir, { recursive: true });

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

	// Register extension
	questJournalExtension(mockPi);

	// 1. Verify commands registered
	assert.ok(commands["subquest"], "Command /subquest should be registered");
	assert.ok(commands["sub-quest"], "Command /sub-quest should be registered");
	assert.strictEqual(commands["subtask"], undefined, "Command /subtask should NOT be registered");
	assert.strictEqual(commands["task-subquest"], undefined, "Command /task-subquest should NOT be registered");

	// 2. Verify tools registered
	assert.ok(tools["quest_subquest"], "Tool quest_subquest should be registered");

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({ percent: 20 }),
		sessionManager: {
			getBranch: () => [],
		},
		ui: {
			notify(msg: string) {
				notifiedMessages.push(msg);
			},
			setWidget() {},
			setStatus() {},
			input: async () => "Test goal",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 3. Test creating a parent quest
	// Switch to parent quest
	await commands["quest"].handler("parent-test-quest", mockCtx);

	// 4. Test tool execution `quest_subquest`
	const subquestTool = tools["quest_subquest"];
	const toolRes = await subquestTool.execute(
		"call_1",
		{
			name: "child-remark-quest",
			goal: "Investigate tangent remark from user",
			parentName: "parent-test-quest",
			switchNow: false,
		},
		null,
		null,
		mockCtx,
	);

	assert.ok(toolRes && toolRes.content && toolRes.content[0].text.includes("Created sub-quest"), "Tool should succeed");

	// Verify parent quest file was updated with child sub-quest
	const updatedParentContent = await getQuestContentBySlug("parent-test-quest");
	assert.ok(updatedParentContent.includes("[[child-remark-quest]]"), "Parent quest file should link child sub-quest");
	assert.ok(updatedParentContent.includes("Investigate tangent remark from user"), "Parent quest file should include sub-quest description");

	// 5. Test command execution `/subquest` with description (slug inferred)
	userMessages = [];
	await commands["subquest"].handler("Implement second follow-up feature", mockCtx);

	const parentContentAfterSecondSub = await getQuestContentBySlug("parent-test-quest");
	assert.ok(parentContentAfterSecondSub.includes("[[implement-second-follow-up-feature]]"), "Parent quest file should link second subquest");
	assert.ok(parentContentAfterSecondSub.includes("Implement second follow-up feature"), "Parent quest file should have goal");

	// 6. Test `/quests` hierarchical widget display
	let widgetRows: string[] = [];
	mockCtx.ui.setWidget = (_name: string, rows: string[]) => {
		widgetRows = rows;
	};
	await commands["quests"].handler("", mockCtx);
	const widgetText = widgetRows.join("\n");
	assert.ok(widgetText.includes("parent-test-quest"), "Widget should list parent quest");
	assert.ok(widgetText.includes("↳ child-remark-quest"), "Widget should list child-remark-quest nested under parent");
	assert.ok(widgetText.includes("↳ implement-second-follow-up-feature"), "Widget should list implement-second-follow-up-feature nested under parent");

	// 7. Test `/quest-status` displaying parent info for active subquest
	notifiedMessages = [];
	const statusResult = await commands["quest-status"].handler("", mockCtx);
	assert.ok(statusResult.includes("parent: [[parent-test-quest]]") || notifiedMessages.some((m) => m.includes("parent: [[parent-test-quest]]")), "quest-status should show parent quest");

	// 8. Test linking sub-quest into parent that has no existing ## Sub-Quests section
	await commands["quest"].handler("bare-parent-quest", mockCtx);

	await subquestTool.execute(
		"call_2",
		{
			name: "bare-child-quest",
			goal: "Child of bare parent",
			parentName: "bare-parent-quest",
			switchNow: false,
		},
		null,
		null,
		mockCtx,
	);

	const updatedBareParentContent = await getQuestContentBySlug("bare-parent-quest");
	assert.ok(updatedBareParentContent.includes("## Sub-Quests"), "Parent should have Sub-Quests section added");
	assert.ok(updatedBareParentContent.includes("[[bare-child-quest]]"), "Parent should link bare-child-quest");

	// 8b. Test upfront planning of multiple sub-quests at the beginning of a quest (switchNow: false)
	// Switch to parent quest to test planning subquests on parent
	await commands["quest"].handler("parent-test-quest", mockCtx);

	// Plan stage 1 sub-quest upfront
	const plan1Res = await subquestTool.execute(
		"call_plan1",
		{
			name: "planned-sub-stage-1",
			goal: "Stage 1: Architecture refactor",
			parentName: "parent-test-quest",
			switchNow: false,
		},
		null,
		null,
		mockCtx,
	);
	assert.ok(plan1Res.content[0].text.includes("Kept parent quest active"), "switchNow: false should keep parent active");

	// Plan stage 2 sub-quest via command `/subquest --plan`
	await commands["subquest"].handler("--plan Stage 2: Verification and benchmarks", mockCtx);

	// Verify parent quest file links both planned subquests
	const parentWithPlans = await getQuestContentBySlug("parent-test-quest");
	assert.ok(parentWithPlans.includes("[[planned-sub-stage-1]]"), "Parent quest links planned subquest 1");
	assert.ok(parentWithPlans.includes("[[stage-2-verification-and-benchmarks]]"), "Parent quest links planned subquest 2");

	// 8c. Test switchNow default: calling quest_subquest with switchNow omitted should default to switchNow: true

	await subquestTool.execute(
		"call_switch_default",
		{
			name: "switch-default-subquest",
			goal: "Test switchNow defaults to true when omitted",
			parentName: "parent-test-quest",
			// switchNow is intentionally omitted
		},
		null,
		null,
		mockCtx,
	);

	const statusDefault = await commands["quest-status"].handler("", mockCtx);
	assert.ok(
		statusDefault.includes("switch-default-subquest"),
		`Omitting switchNow should default to switching to the subquest, got status: ${statusDefault}`,
	);

	// 8d. Test linkSubQuestInParent when parent quest file does not exist on disk at all
	await subquestTool.execute(
		"call_unwritten_parent",
		{
			name: "unwritten-child-quest",
			goal: "Child of unwritten parent",
			parentName: "unwritten-parent-quest",
			switchNow: false,
		},
		null,
		null,
		mockCtx,
	);

	// Verify unwritten-parent-quest.md was auto-created and has child linked
	const autoCreatedParent = await getQuestContentBySlug("unwritten-parent-quest");
	assert.ok(autoCreatedParent.includes("unwritten-parent-quest"), "Parent file should be auto-created");
	assert.ok(autoCreatedParent.includes("[[unwritten-child-quest]]"), "Auto-created parent should link child quest");

	// 9. Test LIFO archival: archiving active child quest should pop and return to parent quest
	const archiveTool = tools["quest_archive"];
	assert.ok(archiveTool, "quest_archive tool should be registered");

	// Switch to child sub-quest on top of stack
	await commands["quest"].handler("parent-test-quest", mockCtx);
	await commands["quest"].handler("implement-second-follow-up-feature", mockCtx);
	const archiveRes = await archiveTool.execute("call_arch", { compact: false }, null, null, mockCtx);
	assert.ok(archiveRes && archiveRes.content[0].text.includes("Resumed parent/previous quest 'parent-test-quest'"), "Archiving child should resume parent quest via LIFO stack");

	// Verify parent quest file now has the child marked as done: - [x] [[implement-second-follow-up-feature]]
	const finalParentContent = await getQuestContentBySlug("parent-test-quest");
	assert.ok(finalParentContent.includes("- [x] [[implement-second-follow-up-feature]]"), "Parent quest file should mark completed child sub-quest as [x]");

	// 10. Verify Prompt injection contains Sub-Quests instructions
	let prompt = "Base prompt";
	for (const cb of handlers["before_agent_start"] || []) {
		const res = await cb({ systemPrompt: prompt }, mockCtx);
		if (res?.systemPrompt) prompt = res.systemPrompt;
	}

	assert.ok(prompt.includes("Sub-Quests"), "System prompt should mention Sub-Quests");
	assert.ok(prompt.includes("quest_subquest"), "System prompt should mention quest_subquest tool");

	// Cleanup test files
	await rm(currentDir, { recursive: true, force: true });

	console.log("PASS: quest_journal_subquest_test");
}

Deno.test("quest_journal_subquest: subquest lifecycle and LIFO stack", async () => {
	await testSubQuestFeature();
});
