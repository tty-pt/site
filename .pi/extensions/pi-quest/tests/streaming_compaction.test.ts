import assert from "node:assert";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_streaming_compaction: safe message queueing and deferred compaction", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const testQuestSlug = "test-streaming-quest";
	const testQuestPath = `${currentDir}/${testQuestSlug}.md`;
	await rm(testQuestPath, { force: true });
	await writeFile(testQuestPath, `# Quest: ${testQuestSlug}\n\n## Goal\nTest streaming safe dispatch\n`, "utf8");

	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
	const sentUserMessages: Array<{ msg: any; options?: any }> = [];
	let isAgentStreaming = false;
	let compactInvocationCount = 0;
	let compactOptions: any = null;

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
			if (isAgentStreaming && (!options || !options.deliverAs)) {
				throw new Error("Agent is already processing. Specify streamingBehavior ('steer' or 'followUp') to queue the message.");
			}
			sentUserMessages.push({ msg, options });
		},
	};

	questJournalExtension(mockPi);

	let isIdle = true;
	const mockCtx: any = {
		cwd: "/home/quirinpa/site",
		hasUI: true,
		mode: "tui",
		isIdle: () => isIdle,
		getContextUsage: () => ({ tokens: 10000, contextWindow: 1000000, percent: 1 }),
		sessionManager: {
			getBranch: () => [],
		},
		compact: (opts: any) => {
			compactInvocationCount++;
			compactOptions = opts;
		},
		ui: {
			notify: () => {},
			setStatus: () => {},
		},
	};

	// Initialize quest
	await commands["quest"].handler(testQuestSlug, mockCtx);

	// 1. Test sending user message while agent is streaming
	isAgentStreaming = true;
	isIdle = false;

	// Trigger turn_end event which sends save prompts or subquest prompts
	// Under streaming/non-idle state, safeSendUserMessage must not throw
	for (const cb of handlers["turn_end"] || []) {
		await cb({ message: { role: "assistant" }, toolResults: [] }, mockCtx);
	}

	// 2. Test deferred compaction on quest_archive
	isAgentStreaming = false;
	isIdle = true;

	const archiveResult = await tools["quest_archive"].execute(
		"call_arch_1",
		{ questName: testQuestSlug, compact: true },
		null,
		null,
		mockCtx,
	);

	assert.ok(archiveResult && (archiveResult.content[0].text.includes("Quest archived") || archiveResult.content[0].text.includes("Archived")), "quest_archive must succeed");

	// Post-tool turn_end triggers deferred archive compaction
	for (const cb of handlers["turn_end"] || []) {
		await cb({ message: { role: "assistant" }, toolResults: [{ toolName: "quest_archive" }] }, mockCtx);
	}
	
	// Wait a tick for deferred compaction to trigger
	await new Promise((resolve) => setTimeout(resolve, 60));

	assert.strictEqual(compactInvocationCount, 1, "Compaction must be invoked exactly once after archive");
	assert.ok(
		compactOptions && compactOptions.customInstructions.includes(testQuestSlug),
		"Compaction instructions must reference archived quest slug",
	);

	// Clean up
	await rm(testQuestPath, { force: true });
});
