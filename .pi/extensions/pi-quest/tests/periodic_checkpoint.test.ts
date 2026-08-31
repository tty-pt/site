import assert from "node:assert";
import { mkdir, rm } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("periodic_checkpoint: every 6 substantive turns when dirty", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const handlers: Record<string, EventCallback[]> = {};
	const commands: Record<string, any> = {};
	const tools: Record<string, any> = {};
	let userMessages: any[] = [];

	const mockPi: any = {
		on(event: string, callback: EventCallback) { if (!handlers[event]) handlers[event] = []; handlers[event].push(callback); },
		appendEntry() {}, registerEntryRenderer() {},
		registerTool(toolDef: any) { tools[toolDef.name] = toolDef; },
		registerCommand(name: string, commandDef: any) { commands[name] = commandDef; },
		sendUserMessage(msg: any, options?: any) { userMessages.push({ msg, options }); },
		sendMessage(msg: any, options?: any) { userMessages.push({ msg: msg?.content || msg, options, customType: msg?.customType }); },
	};

	questJournalExtension(mockPi);

	const mockCtx: any = {
		cwd: process.cwd(),
		getContextUsage: () => ({ tokens: 50000, contextWindow: 1000000 }),
		sessionManager: { getBranch: () => [] },
		compact: (_opts: any) => {},
		ui: { notify() {}, setWidget() {}, setStatus() {}, input: async () => "", select: async () => null },
		hasUI: true, mode: "tui",
	};

	await commands["quest"].handler("periodic-test", mockCtx);

	// 5 substantive turns — no steer
	userMessages = [];
	for (let i = 0; i < 5; i++) {
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: `mods/song/${i}.c` } }, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit", args: { path: `mods/song/${i}.c` } }] }, mockCtx);
		}
	}
	assert.strictEqual(userMessages.filter((m) => typeof m.msg === "string" && m.msg.includes("Periodic Durable Checkpoint")).length, 0, "5 turns should not trigger periodic");

	// 6th substantive turn — should steer exactly once
	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "edit", input: { path: "mods/song/5.c" } }, mockCtx);
	}
	userMessages = [];
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "edit", args: { path: "mods/song/5.c" } }] }, mockCtx);
	}
	const periodic = userMessages.filter((m) => typeof m.msg === "string" && m.msg.includes("Periodic Durable Checkpoint"));
	assert.strictEqual(periodic.length, 1, "6th substantive turn should trigger periodic checkpoint");
	assert.strictEqual(periodic[0].options?.deliverAs, "steer");

	// Pure read/search turns do not count — should not trigger another periodic immediately
	userMessages = [];
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "read", args: { path: "docs/ARCHITECTURE.md" } }] }, mockCtx);
	}
	assert.strictEqual(userMessages.filter((m) => typeof m.msg === "string" && m.msg.includes("Periodic")).length, 0, "read-only turn must not trigger periodic");

	// After save, counter resets — need another 6 substantive turns
	userMessages = [];
	await tools["quest_mark_saved"].execute("call_saved", {}, null, null, mockCtx);
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "quest_mark_saved" }] }, mockCtx);
	}
	// immediately after save, no dirty so no steer even if counter was 0
	userMessages = [];
	for (const cb of handlers["turn_end"] || []) {
		await cb({ toolResults: [{ toolName: "read", args: { path: "docs/ARCHITECTURE.md" } }] }, mockCtx);
	}
	assert.strictEqual(userMessages.filter((m) => m.msg?.includes("Periodic")).length, 0);

	// Another 6 edits → periodic again (wait burst guard)
	await new Promise((r) => setTimeout(r, 60));
	for (let i = 0; i < 6; i++) {
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: `mods/song/again${i}.c` } }, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit", args: { path: `mods/song/again${i}.c` } }] }, mockCtx);
		}
	}
	assert.ok(userMessages.some((m) => typeof m.msg === "string" && m.msg.includes("Periodic Durable Checkpoint")), "second batch of 6 should contain periodic");

	// session_before_compact dirty → cancel
	userMessages = [];
	// ensure dirty after edits
	for (const cb of handlers["tool_result"] || []) {
		await cb({ toolName: "edit", input: { path: "mods/song/dirty.c" } }, mockCtx);
	}
	let beforeRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		beforeRes = await cb({}, mockCtx);
	}
	assert.strictEqual(beforeRes?.cancel, true, "dirty should block compaction");
	assert.ok(userMessages.some((m) => typeof m.msg === "string" && m.msg.includes("Compaction Blocked")));

	// After save, compaction allowed
	await tools["quest_mark_saved"].execute("call_saved2", {}, null, null, mockCtx);
	userMessages = [];
	let afterRes: any;
	for (const cb of handlers["session_before_compact"] || []) {
		afterRes = await cb({}, mockCtx);
	}
	assert.notStrictEqual(afterRes?.cancel, true, "clean save should allow compaction");

	await rm(currentDir, { recursive: true, force: true });
});
