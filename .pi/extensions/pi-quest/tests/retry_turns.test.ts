import assert from "node:assert";
import { mkdir } from "node:fs/promises";
import questJournalExtension from "../index.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

let harnessSeq = 0;
function buildHarness(sessionId?: string) {
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const handlers: Record<string, EventCallback[]> = {};
	const userMessages: Array<{ msg: any; options?: any }> = [];
	const sid = sessionId || `session_retry_${++harnessSeq}`;

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
		sessionManager: { id: sid, getBranch: () => [] },
		compact: () => {}, ui: { notify: () => {}, setStatus: () => {} },
	};

	return { tools, commands, handlers, userMessages, mockCtx };
}

Deno.test("retry_turns: truncated turn (stopReason length, no tools, no text) auto-steers continuation", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const { commands, handlers, userMessages, mockCtx } = buildHarness();
	const slug = "retry-truncated-quest";
	await commands["quest"].handler(slug, mockCtx);

	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 1, "Truncated turn must send one continuation steer");
	assert.ok(String(userMessages[0].msg).includes("Continuation Retry"), "steer should carry the continuation directive");
});

Deno.test("retry_turns: error stopReason with no text is treated as truncation and retried", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const { commands, handlers, userMessages, mockCtx } = buildHarness();
	const slug = "retry-error-quest";
	await commands["quest"].handler(slug, mockCtx);

	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "error" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 1, "error stopReason with no output should auto-retry");
});

Deno.test("retry_turns: clean empty turn (no stopReason) must NOT emit any message", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const { commands, handlers, userMessages, mockCtx } = buildHarness();
	const slug = "retry-clean-quest";
	await commands["quest"].handler(slug, mockCtx);

	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 1, message: { role: "assistant" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 0, "clean empty turn must not nag or retry");

	for (const cb of handlers["turn_end"] || []) {
		await cb({}, mockCtx);
	}
	assert.strictEqual(userMessages.length, 0, "empty turn_end event must not nag or retry");
});

Deno.test("retry_turns: substantive turn (tools ran) resets the retry budget", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const { commands, handlers, userMessages, mockCtx } = buildHarness();
	const slug = "retry-reset-quest";
	await commands["quest"].handler(slug, mockCtx);

	// First a truncated turn increments the budget...
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
	}
	const afterTruncated = userMessages.length;
	assert.strictEqual(afterTruncated, 1);

	// ...then a substantive turn with tool results resets it.
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 2, message: { role: "assistant", stopReason: "stop" }, toolResults: [{ toolName: "edit", input: { path: "mods/song/song.c" } }] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 0, "substantive turn should not emit retry message");

	// And a new truncation starts a fresh budget (uses 1 again, not 2).
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 3, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 1, "reset budget should fire retry on next truncation");
});

Deno.test("retry_turns: budget exhausts and reports CONTINUATION_FAILURE", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	// Env override to a small budget so the test stays fast/stable.
	const prev = Deno.env.get("PI_QUEST_RETRY_MAX_TURNS");
	Deno.env.set("PI_QUEST_RETRY_MAX_TURNS", "2");

	try {
		const { commands, handlers, userMessages, mockCtx } = buildHarness();
		// Ensure a stable slug via a fixed quest name is not required; use a fresh quest.
		const slug = "retry-exhaust-quest-" + Math.random().toString(36).slice(2, 8);
		await commands["quest"].handler(slug, mockCtx);

		// Two retryable turns use up the budget...
		for (const cb of handlers["turn_end"] || []) {
			await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
		}
		assert.strictEqual(userMessages.filter((m) => String(m.msg).includes("Continuation Retry")).length, 1);

		for (const cb of handlers["turn_end"] || []) {
			await cb({ turnIndex: 2, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
		}
		assert.strictEqual(userMessages.filter((m) => String(m.msg).includes("Continuation Retry")).length, 2);

		// The third truncation is exhausted -> a blocking report, not a steer.
		for (const cb of handlers["turn_end"] || []) {
			await cb({ turnIndex: 3, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
		}
		const exhaustMsgs = userMessages.filter((m) => String(m.msg).includes("CONTINUATION_FAILURE"));
		assert.strictEqual(exhaustMsgs.length, 1, "exhaustion should report a blocking error");
		assert.ok(String(exhaustMsgs[0].msg).includes("Exact Next Action"), "error should reference the exact next action");
	} finally {
		if (prev === undefined) Deno.env.delete("PI_QUEST_RETRY_MAX_TURNS");
		else Deno.env.set("PI_QUEST_RETRY_MAX_TURNS", prev);
	}
});

Deno.test("retry_turns: maxTurns 0 disables retry entirely", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const prev = Deno.env.get("PI_QUEST_RETRY_MAX_TURNS");
	Deno.env.set("PI_QUEST_RETRY_MAX_TURNS", "0");

	try {
		const { commands, handlers, userMessages, mockCtx } = buildHarness();
		const slug = "retry-disabled-quest";
		await commands["quest"].handler(slug, mockCtx);

		userMessages.length = 0;
		for (const cb of handlers["turn_end"] || []) {
			await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
		}
		assert.strictEqual(userMessages.length, 0, "retry disabled (0) must not emit any message");
	} finally {
		if (prev === undefined) Deno.env.delete("PI_QUEST_RETRY_MAX_TURNS");
		else Deno.env.set("PI_QUEST_RETRY_MAX_TURNS", prev);
	}
});

Deno.test("retry_turns: same-turn re-entry must not fire a duplicate steer", async () => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const { commands, handlers, userMessages, mockCtx } = buildHarness();
	const slug = "retry-reentry-quest";
	await commands["quest"].handler(slug, mockCtx);

	// Fire the same truncated turn twice (simulating handler re-entry).
	userMessages.length = 0;
	for (const cb of handlers["turn_end"] || []) {
		await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
		await cb({ turnIndex: 1, message: { role: "assistant", stopReason: "length" }, toolResults: [] }, mockCtx);
	}
	assert.strictEqual(userMessages.length, 1, "same-turn re-entry must not double-fire");
});
