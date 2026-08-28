import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import { join } from "node:path";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

interface BeforeAgentStartResult {
	systemPrompt?: string;
}

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function testSubQuestFeature() {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const parentPath = "docs/current/parent-test-quest.md";
	const childPath = "docs/current/child-remark-quest.md";
	const anotherChildPath = "docs/current/implement-second-follow-up-feature.md";
	await rm(parentPath, { force: true });
	await rm(childPath, { force: true });
	await rm(anotherChildPath, { force: true });

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
	assert.ok(tools["quest_journal_subquest"], "Tool quest_journal_subquest should be registered");

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
	const parentContent = [
		"# Quest: parent-test-quest",
		"",
		"## Goal",
		"Main parent quest goal",
		"",
		"## Original request",
		"> Main parent quest goal",
		"",
		"## Current Status",
		"- [ ] in progress",
		"",
		"## Sub-Quests",
		"> Sub-quests, follow-ups, or tangent quests spawned from this quest.",
		"- [ ] ",
		"",
		"## Why this matters",
		"Test parent context",
	].join("\n");
	await writeFile(parentPath, parentContent, "utf8");

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

	// Verify child quest file was created
	const childContent = await readFile(childPath, "utf8");
	assert.ok(childContent.includes("## Parent Quest"), "Child quest file should have Parent Quest section");
	assert.ok(childContent.includes("[[parent-test-quest]]"), "Child quest file should link parent");
	assert.ok(childContent.includes("Investigate tangent remark from user"), "Child quest file should have goal");

	// Verify parent quest file was updated with child sub-quest
	const updatedParentContent = await readFile(parentPath, "utf8");
	assert.ok(updatedParentContent.includes("[[child-remark-quest]]"), "Parent quest file should link child sub-quest");
	assert.ok(updatedParentContent.includes("Investigate tangent remark from user"), "Parent quest file should include sub-quest description");

	// 5. Test command execution `/subquest` with description (slug inferred)
	userMessages = [];
	await commands["subquest"].handler("Implement second follow-up feature", mockCtx);

	const anotherChildContent = await readFile(anotherChildPath, "utf8");
	assert.ok(anotherChildContent.includes("## Parent Quest"), "Second child quest file should have Parent Quest");
	assert.ok(anotherChildContent.includes("Implement second follow-up feature"), "Second child quest file should have goal");

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
	await commands["quest-status"].handler("", mockCtx);
	assert.ok(notifiedMessages.some((m) => m.includes("parent: [[parent-test-quest]]")), "quest-status should show parent quest");

	// 8. Test linking sub-quest into parent that has no existing ## Sub-Quests section
	const bareParentPath = "docs/current/bare-parent-quest.md";
	const bareParentContent = "# Quest: bare-parent-quest\n\n## Goal\nBare parent goal\n\n## Why this matters\nImportant\n";
	await writeFile(bareParentPath, bareParentContent, "utf8");

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

	const updatedBareParentContent = await readFile(bareParentPath, "utf8");
	assert.ok(updatedBareParentContent.includes("## Sub-Quests"), "Parent should have Sub-Quests section added");
	assert.ok(updatedBareParentContent.includes("[[bare-child-quest]]"), "Parent should link bare-child-quest");

	const bareChildPath = "docs/current/bare-child-quest.md";
	await rm(bareParentPath, { force: true });
	await rm(bareChildPath, { force: true });

	// 9. Test LIFO archival: archiving active child quest should pop and return to parent quest
	const archiveTool = tools["quest_archive"];
	assert.ok(archiveTool, "quest_archive tool should be registered");

	// Currently implement-second-follow-up-feature is active with parent parent-test-quest
	const archiveRes = await archiveTool.execute("call_arch", { compact: false }, null, null, mockCtx);
	assert.ok(archiveRes && archiveRes.content[0].text.includes("Resumed parent/previous quest 'parent-test-quest'"), "Archiving child should resume parent quest via LIFO stack");

	// Verify parent quest file now has the child marked as done: - [x] [[implement-second-follow-up-feature]]
	const finalParentContent = await readFile(parentPath, "utf8");
	assert.ok(finalParentContent.includes("- [x] [[implement-second-follow-up-feature]]"), "Parent quest file should mark completed child sub-quest as [x]");

	// 10. Verify Prompt injection contains Sub-Quests instructions
	let prompt = "Base prompt";
	for (const cb of handlers["before_agent_start"] || []) {
		const res = await cb({ systemPrompt: prompt }, mockCtx);
		if (res?.systemPrompt) prompt = res.systemPrompt;
	}

	assert.ok(prompt.includes("Sub-Quests"), "System prompt should mention Sub-Quests");
	assert.ok(prompt.includes("quest_subquest") || prompt.includes("quest_journal_subquest"), "System prompt should mention quest_subquest tool");

	// Cleanup test files
	await rm(parentPath, { force: true });
	await rm(childPath, { force: true });
	await rm(anotherChildPath, { force: true });

	console.log("PASS: quest_journal_subquest_test");
}

testSubQuestFeature().catch((err) => {
	console.error("FAIL: quest_journal_subquest_test", err);
	process.exit(1);
});
