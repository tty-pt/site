import assert from "node:assert";
import { mkdir, rm } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { resolveQuestRecordBySlug } from "../src/paths.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_state_machine: deterministic lifecycle, no implicit activation, verified saves only, and loop prevention", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const mainQuestSlug = "main-sm-quest";
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
	const userMessages: any[] = [];
	let isIdle = true;

	const mockPi: any = {
		on(event: string, callback: EventCallback) { if (!handlers[event]) handlers[event] = []; handlers[event].push(callback); },
		appendEntry() {}, registerEntryRenderer() {},
		registerTool(tool: any) { tools[tool.name] = tool; },
		registerCommand(name: string, cmd: any) { commands[name] = cmd; },
		sendUserMessage(msg: any, options?: any) { userMessages.push({ msg, options }); },
		sendMessage(msg: any, options?: any) { userMessages.push({ msg: msg?.content || msg, options }); },
	};

	questJournalExtension(mockPi);

	const mockCtx: any = {
		cwd: "/home/quirinpa/site",
		hasUI: true, mode: "tui", isIdle: () => isIdle,
		getContextUsage: () => ({ tokens: 10000, contextWindow: 1000000 }),
		sessionManager: { getBranch: () => [] },
		compact: () => {}, ui: { notify: () => {}, setStatus: () => {} },
	};

	const initialStatus = await commands["quest-status"].handler("", mockCtx);
	assert.ok(initialStatus.includes("No active quest"));

	await commands["quest"].handler(mainQuestSlug, mockCtx);
	const rec = await resolveQuestRecordBySlug(mainQuestSlug);
	assert.ok(rec);
	const mainQuestPath = rec.path;

	let status = await commands["quest-status"].handler("", mockCtx);
	assert.ok(status.includes(mainQuestSlug));

	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "write", input: { path: ".pi/quest/current/arbitrary/other.md", content: "arbitrary" }, isError: false }, mockCtx);
	}
	status = await commands["quest-status"].handler("", mockCtx);
	assert.ok(status.includes(mainQuestSlug));

	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "write", input: { path: mainQuestPath, content: `# Quest: ${mainQuestSlug}\n\n## Goal\nUpdated goal\n` }, isError: false }, mockCtx);
	}

	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) await cb({ message: { role: "assistant" }, toolResults: [] }, mockCtx);
	assert.strictEqual(userMessages.length, 0, "Clean state on turn_end must not emit save request");
});

Deno.test("quest_journal_state_machine: periodic checkpoint, verified save gate, and compaction lifecycle", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const questSlug = "state-machine-cases-quest";
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
	const userMessages: Array<{ msg: any; options?: any }> = [];

	const mockPi: any = {
		on(event: string, callback: EventCallback) { if (!handlers[event]) handlers[event] = []; handlers[event].push(callback); },
		appendEntry() {}, registerEntryRenderer() {},
		registerTool(tool: any) { tools[tool.name] = tool; },
		registerCommand(name: string, cmd: any) { commands[name] = cmd; },
		sendUserMessage(msg: any, options?: any) { userMessages.push({ msg, options }); },
		sendMessage(msg: any, options?: any) { userMessages.push({ msg: msg?.content || msg, options }); },
	};

	questJournalExtension(mockPi);

	const mockCtx: any = {
		cwd: process.cwd(), hasUI: true, mode: "tui", isIdle: () => true,
		getContextUsage: () => ({ tokens: 10000, contextWindow: 1000000 }),
		sessionManager: { id: "session_cases_a_h", getBranch: () => [] },
		compact: () => {}, ui: { notify: () => {}, setStatus: () => {} },
	};

	await commands["quest"].handler(questSlug, mockCtx);

	// Normal substantive turns under 6 should not produce periodic checkpoint
	for (let i = 0; i < 3; i++) {
		for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: "mods/song/song.c" } }, mockCtx);
		for (const cb of handlers["turn_end"] || []) await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
	}
	assert.strictEqual(userMessages.filter((m) => (typeof m.msg === "string" ? m.msg : "").includes("Periodic Durable Checkpoint")).length, 0);

	// After 6 total substantive turns, periodic triggers
	for (let i = 0; i < 3; i++) {
		for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: `mods/song/${i}a.c` } }, mockCtx);
		for (const cb of handlers["turn_end"] || []) await cb({ toolResults: [{ toolName: "edit", input: { path: `mods/song/${i}a.c` } }] }, mockCtx);
	}
	userMessages.length = 0;
	for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: "mods/song/final.c" } }, mockCtx);
	for (const cb of handlers["turn_end"] || []) await cb({ toolResults: [{ toolName: "edit", input: { path: "mods/song/final.c" } }] }, mockCtx);
	// Might have triggered on 6th; ensure at least one periodic was sent in this window
	// reset and force 6 more to verify deterministically
	await tools["quest_mark_saved"].execute("save_1", {}, null, null, mockCtx);
	await new Promise((r) => setTimeout(r, 60));
	userMessages.length = 0;
	for (let i = 0; i < 6; i++) {
		for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: `mods/song/b${i}.c` } }, mockCtx);
		for (const cb of handlers["turn_end"] || []) await cb({ toolResults: [{ toolName: "edit", input: { path: `mods/song/b${i}.c` } }] }, mockCtx);
	}
	assert.ok(userMessages.some((m) => m.msg?.includes("Periodic Durable Checkpoint")), "6th turn after save should trigger periodic");

	// Dirty blocks compaction
	for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: "mods/gig/gig.c" } }, mockCtx);
	userMessages.length = 0;
	let beforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) beforeCompactRes = await cb({}, mockCtx);
	assert.strictEqual(beforeCompactRes?.cancel, true);
	assert.ok(userMessages[0].msg.includes("Compaction Blocked"));

	// Save allows compaction
	await tools["quest_mark_saved"].execute("save_2", {}, null, null, mockCtx);
	let cleanRes: any;
	for (const cb of handlers["session_before_compact"] || []) cleanRes = await cb({}, mockCtx);
	assert.notStrictEqual(cleanRes?.cancel, true);

	// Post-compaction resume
	userMessages.length = 0;
	for (const cb of handlers["session_compact"] || []) await cb({}, mockCtx);
	assert.ok(userMessages.some((m) => (typeof m.msg === "string" ? m.msg : "").includes("Post-Compaction Autonomous Resumption Directive")));

	await rm(".pi/quest/current", { recursive: true, force: true });
	await new Promise((resolve) => setTimeout(resolve, 80));
});
