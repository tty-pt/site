import assert from "node:assert";
import {
	findProjectRoot,
	resolveSubagentExecutor,
	setCustomSubagentRunner,
	type ExtensionAPI,
	type ExtensionContext,
} from "../index.ts";

// The pi-subagents bridge (prompt-template-bridge.ts) routes a request to the
// *structured* delegation path only when the emitted payload carries an
// ownerRunId / nodeId / result marker. Missing marker => legacy path => the
// bridge immediately replies "Legacy prompt-template direct delegation was
// removed; use ... structured delegation.", which surfaces upstream as a
// CRITICAL_REVIEW_ERROR. The previous implementation emitted a flat legacy
// payload (no marker, and an unsupported `async` field), so every critical
// review failed. These tests drive the REAL resolveSubagentExecutor bridge path
// (not setCustomSubagentRunner) and assert the exact structured request shape
// the bridge validates.

function createEventBus() {
	type Handler = (data: any) => void;
	const handlers: Record<string, Handler[]> = {};
	const emitted: Array<{ event: string; data: any }> = [];
	return {
		emitted,
		events: {
			on: (event: string, handler: Handler) => {
				if (!handlers[event]) handlers[event] = [];
				handlers[event].push(handler);
				return () => {
					const i = handlers[event].indexOf(handler);
					if (i >= 0) handlers[event].splice(i, 1);
				};
			},
			emit: (event: string, data: any) => {
				emitted.push({ event, data });
				for (const h of [...(handlers[event] || [])]) {
					try { h(data); } catch {}
				}
			},
		},
	};
}

function createMockExtensionAPI(tools: string[] = ["subagent"]) {
	const bus = createEventBus();
	const mockPi: ExtensionAPI = {
		on: () => {},
		registerTool: () => {},
		registerCommand: () => {},
		sendUserMessage: () => {},
		sendMessage: () => {},
		appendEntry: () => {},
		registerEntryRenderer: () => {},
		getAllTools: () => tools.map((name) => ({ name })),
		events: bus.events,
	};
	return { mockPi, bus };
}

function createMockContext(sessionId = `session_${Date.now().toString(36)}`): ExtensionContext {
	const branch: any[] = [];
	return {
		cwd: process.cwd(),
		mode: "agent",
		hasUI: true,
		sessionManager: {
			id: sessionId,
			sessionId,
			getBranch: () => branch,
			appendCustomEntry: (_type: string, data: any) => {
				branch.push({ type: "custom", customType: "quest_journal", data });
			},
		},
		getContextUsage: () => ({ tokens: 1000, percent: 1 }),
		ui: {
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => null,
		},
	};
}

function validId(value: unknown): boolean {
	return typeof value === "string" && value.trim().length > 0 && value.length <= 256 && !/[\r\n]/.test(value);
}

Deno.test("Structured Delegation Payload Suite: pi-subagents bridge contract (#59)", async (t) => {
	setCustomSubagentRunner(null);
	const { mockPi, bus } = createMockExtensionAPI(["subagent"]);
	const ctx = createMockContext();

	const executor = resolveSubagentExecutor(mockPi as any, ctx);
	assert.ok(executor, "executor must be resolved when the subagent tool is registered");

	let requestId = "";
	let ownerRunId = "";
	let nodeId = "";

	bus.events.on("prompt-template:subagent:request", (data: any) => {
		if (data && typeof data.requestId === "string") {
			requestId = data.requestId;
			ownerRunId = data.ownerRunId;
			nodeId = data.nodeId;
		}
	});

	await t.step("1. structured payload carries ownerRunId/nodeId/result and no unsupported async", async () => {
		const promise = executor!("DIRECTION REVIEW task", {
			agent: "reviewer",
			isCriticalReview: true,
			model: "x/y",
			async: true,
		});

		// The bridge would parse the emitted request via parseSubagentDelegationRequest:
		// - ownerRunId and nodeId must be valid ids (non-empty, <=256, no newlines)
		// - result must be {kind:"text"}
		// - `async` must NOT be present (it is not a supported delegation field)
		assert.ok(validId(ownerRunId), "ownerRunId must be a valid delegation id");
		assert.ok(validId(nodeId), "nodeId must be a valid delegation id");
		assert.ok(validId(requestId), "requestId must be a valid delegation id");

		// Mirror the bridge's hasStructuredDelegationMarker + byte-level field checks.
		const emitted = bus.emitted.find((e) => e.event === "prompt-template:subagent:request");
		assert.ok(emitted, "must emit prompt-template:subagent:request");
		const payload = emitted.data;
		assert.ok(Object.hasOwn(payload, "ownerRunId"), "payload must carry ownerRunId marker");
		assert.ok(Object.hasOwn(payload, "nodeId"), "payload must carry nodeId marker");
		assert.deepStrictEqual(payload.result, { kind: "text" }, "result must be {kind:'text'}");
		assert.ok(!Object.hasOwn(payload, "async"), "unsupported async field must be omitted");
		assert.strictEqual(payload.context, "fresh", "context must be valid");
		// cwd is re-anchored to the project root (not the extension dir) per #58.
		assert.strictEqual(payload.cwd, findProjectRoot(process.cwd()), "cwd must be the project root");
		assert.strictEqual(payload.agent, "reviewer");
		assert.strictEqual(payload.model, "x/y");

		// Resolve the pending promise so clean shutdown is not required.
		bus.events.emit("prompt-template:subagent:response", {
			requestId,
			ownerRunId,
			nodeId,
			status: "completed",
			result: { kind: "text", text: "VERDICT: PASS" },
		});
		await promise;
	});

	await t.step("2. requestId and nodeId are unique across invocations (same-turn safety)", async () => {
		const first: { requestId?: string; nodeId?: string } = {};
		const second: { requestId?: string; nodeId?: string } = {};
		let target: { requestId?: string; nodeId?: string } = first;

		bus.events.on("prompt-template:subagent:request", (data: any) => {
			target.requestId = data?.requestId;
			target.nodeId = data?.nodeId;
		});

		target = first;
		const p1 = executor!("review one", { reviewId: "correlation_turn_1" });
		bus.events.emit("prompt-template:subagent:response", {
			requestId: first.requestId,
			status: "failed",
			error: "first",
		});
		await p1.catch(() => {});

		target = second;
		const p2 = executor!("review two", { reviewId: "correlation_turn_1" });
		bus.events.emit("prompt-template:subagent:response", {
			requestId: second.requestId,
			status: "failed",
			error: "second",
		});
		await p2.catch(() => {});

		// Even with the SAME options.reviewId (same-turn correlation), requestId/nodeId must differ,
		// otherwise the second active delegation would collide with the first in the bridge.
		assert.ok(first.requestId, "first invocation must capture requestId");
		assert.ok(second.requestId, "second invocation must capture requestId");
		assert.notStrictEqual(first.requestId, second.requestId, "requestId must be unique per invocation");
		assert.notStrictEqual(first.nodeId, second.nodeId, "nodeId must be unique per invocation");
	});
});
