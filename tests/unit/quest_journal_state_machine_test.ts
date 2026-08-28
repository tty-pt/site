import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../../.pi/extensions/quest-journal.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_state_machine: deterministic lifecycle, no implicit activation, verified saves only, and loop prevention", async () => {
	const currentDir = "docs/current";
	await mkdir(currentDir, { recursive: true });

	const mainQuestSlug = "main-sm-quest";
	const mainQuestPath = `docs/current/${mainQuestSlug}.md`;
	const otherQuestSlug = "other-sm-quest";
	const otherQuestPath = `docs/current/${otherQuestSlug}.md`;

	await rm(mainQuestPath, { force: true });
	await rm(otherQuestPath, { force: true });

	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
	const userMessages: any[] = [];
	let isIdle = true;
	let compactInvocationCount = 0;

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
		cwd: "/home/quirinpa/site",
		hasUI: true,
		mode: "tui",
		isIdle: () => isIdle,
		getContextUsage: () => ({
			tokens: currentTokens,
			contextWindow: currentContextWindow,
			percent: (currentTokens / currentContextWindow) * 100,
		}),
		sessionManager: {
			getBranch: () => [],
		},
		compact: () => {
			compactInvocationCount++;
		},
		ui: {
			notify: () => {},
			setStatus: () => {},
		},
	};

	// 1. Initial state: IDLE (no active quest)
	const initialStatus = await commands["quest-status"].handler("", mockCtx);
	assert.ok(initialStatus.includes("No active quest"), "Initial state should be no active quest");

	// 2. Explicit activation: ACTIVATE_QUEST
	await writeFile(mainQuestPath, `# Quest: ${mainQuestSlug}\n\n## Goal\nMain goal\n`, "utf8");
	await commands["quest"].handler(mainQuestSlug, mockCtx);

	let status = await commands["quest-status"].handler("", mockCtx);
	assert.ok(status.includes(mainQuestSlug), "Active quest must be main-sm-quest");

	// 3. Test: Writing arbitrary files under docs/current/ must NOT change active quest!
	// Simulate tool_result writing to docs/current/other-sm-quest.md
	await writeFile(otherQuestPath, `# Quest: ${otherQuestSlug}\n\n## Goal\nOther goal\n`, "utf8");

	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "write",
				input: { path: otherQuestPath, content: "arbitrary content" },
				isError: false,
			},
			mockCtx,
		);
	}

	// Active quest must remain mainQuestSlug (deterministic, no implicit filesystem side-effect mutation)
	status = await commands["quest-status"].handler("", mockCtx);
	assert.ok(
		status.includes(mainQuestSlug),
		`Active quest must remain '${mainQuestSlug}' after writing '${otherQuestPath}', got: ${status}`,
	);
	assert.strictEqual(
		status.includes(otherQuestSlug),
		false,
		`Active quest must NOT be mutated to '${otherQuestSlug}' implicitly`,
	);

	// 4. Test: tool_result on active quest verifies and marks clean
	for (const cb of handlers["tool_result"] || []) {
		await cb(
			{
				toolName: "write",
				input: { path: mainQuestPath, content: `# Quest: ${mainQuestSlug}\n\n## Goal\nUpdated goal\n` },
				isError: false,
			},
			mockCtx,
		);
	}

	// 5. Test: Loop prevention on turn_end
	// When state is clean, turn_end must NOT emit redundant save request messages
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ message: { role: "assistant" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(
		userMessages.length,
		0,
		"Clean state on turn_end must not emit save request or trigger runaway prompt loops",
	);

	// Clean up
	await rm(mainQuestPath, { force: true });
	await rm(otherQuestPath, { force: true });
});
