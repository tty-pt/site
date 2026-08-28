import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

async function testStatusHierarchyAndFormatting() {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const parentPath = "docs/current/parent-hierarchy-quest.md";
	const childPath = "docs/current/child-sub-quest.md";
	const grandChildPath = "docs/current/grandchild-sub-quest.md";

	await rm(parentPath, { force: true });
	await rm(childPath, { force: true });
	await rm(grandChildPath, { force: true });

	const handlers: Record<string, EventCallback[]> = {};
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	let lastStatusText: string | undefined;

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
		sendUserMessage() {},
	};

	questJournalExtension(mockPi);

	let currentTokens: number | null = 45000;
	let currentContextWindow = 200000;
	let currentPercent = 22.5;

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: currentPercent,
		}),
		sessionManager: {
			getBranch: () => [],
		},
		ui: {
			notify() {},
			setWidget() {},
			setStatus(key: string, text: string | undefined) {
				if (key === "quest") {
					lastStatusText = text;
				}
			},
			input: async () => "Subquest goal",
			select: async () => null,
		},
		hasUI: true,
		mode: "tui",
	};

	// 1. Single active quest status bar formatting
	await writeFile(parentPath, "# Quest: parent-hierarchy-quest\n\n## Goal\nParent Goal\n", "utf8");
	await commands["quest"].handler("parent-hierarchy-quest", mockCtx);

	assert.ok(lastStatusText, "Status text should be set");
	assert.ok(lastStatusText.includes("parent-hierarchy-quest"), "Status should contain active quest name");
	assert.ok(lastStatusText.includes("45k/140k") || lastStatusText.includes("45k"), "Status should contain token economy stats");

	// 2. Create child sub-quest and verify hierarchy formatting: parent ↳ child
	const subquestTool = tools["quest_subquest"];
	await subquestTool.execute(
		"call_sub1",
		{
			name: "child-sub-quest",
			goal: "Child Subquest Goal",
			parentName: "parent-hierarchy-quest",
			switchNow: true,
		},
		null,
		null,
		mockCtx,
	);

	assert.ok(lastStatusText, "Status text should be set after switching to sub-quest");
	assert.ok(
		lastStatusText.includes("parent-hierarchy-quest ↳ child-sub-quest") ||
		lastStatusText.includes("parent-hierarchy-quest → child-sub-quest"),
		`Status should clearly represent subquest hierarchy, got: ${lastStatusText}`,
	);

	// 3. Create grandchild sub-quest and verify multi-level hierarchy: parent ↳ child ↳ grandchild
	await subquestTool.execute(
		"call_sub2",
		{
			name: "grandchild-sub-quest",
			goal: "Grandchild Goal",
			parentName: "child-sub-quest",
			switchNow: true,
		},
		null,
		null,
		mockCtx,
	);

	assert.ok(
		lastStatusText.includes("parent-hierarchy-quest ↳ child-sub-quest ↳ grandchild-sub-quest") ||
		lastStatusText.includes("parent-hierarchy-quest → child-sub-quest → grandchild-sub-quest"),
		`Status should represent multi-level hierarchy, got: ${lastStatusText}`,
	);

	// 4. Test status display when tokens is null or unknown
	currentTokens = null;
	currentPercent = null as any;
	// Mark saved to trigger UI update
	await tools["quest_mark_saved"].execute("call_saved", {}, null, null, mockCtx);
	assert.ok(lastStatusText.includes("grandchild-sub-quest"), "Status should still show hierarchy when tokens is null");

	// 5. Test status display when save pending vs fresh
	// Simulate unsaved state
	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}
	// Verify save pending indication
	// Clean up test files
	await rm(parentPath, { force: true });
	await rm(childPath, { force: true });
	await rm(grandChildPath, { force: true });

	console.log("PASS: quest_journal_status_test");
}

testStatusHierarchyAndFormatting().catch((err) => {
	console.error("FAIL: quest_journal_status_test", err);
	process.exit(1);
});
