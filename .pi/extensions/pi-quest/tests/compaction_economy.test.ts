import assert from "node:assert";
import { mkdir, rm } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("quest_journal_compaction_dynamics: periodic checkpoint, subquest launch, and post-compaction resume", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	let userMessages: any[] = [];
	let notifiedMessages: string[] = [];
	let compactCalled = false;

	const mockPi: any = {
		on(event: string, callback: EventCallback) { if (!handlers[event]) handlers[event] = []; handlers[event].push(callback); },
		appendEntry() {}, registerEntryRenderer() {},
		registerTool(toolDef: any) { tools[toolDef.name] = toolDef; },
		registerCommand(name: string, commandDef: any) { commands[name] = commandDef; },
		sendUserMessage(msg: any, options?: any) { userMessages.push({ msg, options }); },
		sendMessage(msg: any, options?: any) { userMessages.push({ msg: msg?.content || msg, options, customType: msg?.customType, display: msg?.display }); },
	};

	questJournalExtension(mockPi);

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({ tokens: 50000, contextWindow: 1000000 }),
		sessionManager: { getBranch: () => [] },
		compact: (options: any) => { compactCalled = true; },
		ui: { notify(msg: string) { notifiedMessages.push(msg); }, setWidget() {}, setStatus() {}, input: async () => "", select: async () => null },
		hasUI: true, mode: "tui",
	};

	await commands["quest"].handler("parent-compaction-quest", mockCtx);

	// Sub-quest creation should NOT trigger premature launch compaction
	compactCalled = false;
	await tools["quest_subquest"].execute("call_subquest_1", { goal: "Refactor database engine", name: "sub-compaction-quest", switchNow: true }, null, null, mockCtx);
	assert.strictEqual(compactCalled, false, "Sub-quest creation should NOT trigger premature launch compaction");

	// Periodic checkpoint: 6 substantive turns should steer
	for (let i = 0; i < 5; i++) {
		for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: `mods/song/${i}.c` } }, mockCtx);
		for (const cb of handlers["turn_end"] || []) await cb({ toolResults: [{ toolName: "edit", args: { path: `mods/song/${i}.c` } }] }, mockCtx);
	}
	userMessages = [];
	for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: "mods/song/5.c" } }, mockCtx);
	for (const cb of handlers["turn_end"] || []) await cb({ toolResults: [{ toolName: "edit", args: { path: "mods/song/5.c" } }] }, mockCtx);
	assert.ok(userMessages.some((m) => typeof m.msg === "string" && m.msg.includes("Periodic Durable Checkpoint")), "6th substantive turn should send periodic checkpoint");
	const steer = userMessages.find((m) => m.msg?.includes("Periodic"));
	assert.strictEqual(steer?.options?.deliverAs, "steer");

	// Post-save allows compaction (periodic replaces token gate)
	await tools["quest_mark_saved"].execute("call_saved_mid", {}, null, null, mockCtx);
	let beforeCompactRes: any;
	for (const cb of handlers["session_before_compact"] || []) beforeCompactRes = await cb({}, mockCtx);
	assert.notStrictEqual(beforeCompactRes?.cancel, true, "Clean save should allow compaction");

	// Post-compaction resume
	userMessages = [];
	for (const cb of handlers["session_compact"] || []) await cb({}, mockCtx);
	assert.ok(userMessages.length > 0, "session_compact should send post-compaction resumption directive");
	const postCompactMsg = typeof userMessages[0].msg === "string" ? userMessages[0].msg : (Array.isArray(userMessages[0].msg) ? userMessages[0].msg[0].text : userMessages[0].msg.text || "");
	assert.ok(postCompactMsg.includes("Post-Compaction Autonomous Resumption Directive"), `Post-compaction directive missing: ${postCompactMsg}`);

	// CRB provider still registered
	const g = globalThis as any;
	assert.ok(g.__pi_crb_providers && g.__pi_crb_providers.length > 0, "CRB provider hook should be registered");

	// System prompt mandates autonomous resumption
	let systemPrompt = "Base system prompt.";
	for (const cb of handlers["before_agent_start"] || []) { const res = await cb({ systemPrompt }, mockCtx); if (res?.systemPrompt) systemPrompt = res.systemPrompt; }
	assert.ok(systemPrompt.toLowerCase().includes("resume") || systemPrompt.toLowerCase().includes("autonomously"), "System prompt should mandate autonomous resumption");

	// Dirty blocks compaction
	for (const cb of handlers["tool_result"] || []) await cb({ toolName: "edit", input: { path: "mods/song/dirty.c" } }, mockCtx);
	userMessages = [];
	for (const cb of handlers["session_before_compact"] || []) { const r = await cb({}, mockCtx); assert.strictEqual(r?.cancel, true); }
	assert.strictEqual(userMessages.length, 1);
	assert.ok(userMessages[0].msg.includes("Compaction Blocked"));

	await rm(currentDir, { recursive: true, force: true });
});
