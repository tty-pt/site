import assert from "node:assert";
import { mkdir, readFile, rm, writeFile } from "node:fs/promises";
import plugin, { canImplement, type StoredState } from "../../.pi/extensions/quest-journal.ts";

function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const registeredTools: any[] = [];
	const registeredCommands: any[] = [];
	const userMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];

	const mockPi = {
		on: (event: string, handler: any) => {
			if (!handlers[event]) handlers[event] = [];
			handlers[event].push(handler);
		},
		registerTool: (tool: any) => {
			registeredTools.push(tool);
		},
		registerCommand: (name: string, cmd: any) => {
			registeredCommands.push({ name, ...cmd });
		},
		sendMessage: (msg: any, options?: any) => {
			userMessages.push({
				msg: msg?.content || msg,
				options,
				customType: msg?.customType,
				display: msg?.display,
			});
		},
		sendUserMessage: (msg: any, options?: any) => {
			userMessages.push({ msg, options });
		},
		registerEntryRenderer: () => {},
	};

	return { mockPi, handlers, registeredTools, registeredCommands, userMessages };
}

function createMockContext(tokens = 50000, sessionId = `session_${Math.random().toString(36).slice(2)}`) {
	const branch: any[] = [];
	return {
		mode: "agent",
		hasUI: true,
		sessionManager: {
			id: sessionId,
			getBranch: () => branch,
			appendCustomEntry: (_type: string, data: any) => {
				branch.push({ type: "custom", customType: "quest_journal", data });
			},
		},
		getContextUsage: () => ({ tokens, percent: (tokens / 800000) * 100 }),
		ui: {
			notify: () => {},
			setStatus: () => {},
			input: async () => "",
			select: async () => "",
		},
	};
}

Deno.test("quest_journal_gate_continuation: gate opening continuation steering lifecycle", async (t) => {
	const QUEST_DIR = "docs/current";
	await mkdir(QUEST_DIR, { recursive: true });

	// -----------------------------------------------------------------------
	// 1. Reassessment resolution triggers continuation steer
	// -----------------------------------------------------------------------
	await t.step("1. Reassessment resolution triggers continuation steer to resume execution", async () => {
		const { mockPi, handlers, registeredTools, registeredCommands, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);
		const mockCtx = createMockContext();

		const questSlug = "reassessment-gate-test";
		const questFilePath = `${QUEST_DIR}/${questSlug}.md`;

		// 1. Create quest in REASSESSMENT_PENDING state
		await writeFile(
			questFilePath,
			`# Quest: ${questSlug}

## Goal
Implement robust socket connection.

## Current Status
- [ ] blocked

## Current Understanding
- Socket buffer was assumed non-blocking.

## Key Assumptions
- [ ] Non-blocking socket read succeeds without EAGAIN.

## Open Questions & Uncertainties
- [ ] Why did poll() return EWOULDBLOCK?

## Research Findings
- Initial socket creation succeeded.

## Plan Version
1

## Plan
1. Connect socket
2. Write payload

## Plan Confidence
low

## Plan Revisions
- Initial plan formulated.

## Latest Reassessment
- Reassessment triggered by test failure.

## Reassessment Status
REQUIRED (v1) - Socket write failed with EAGAIN

## Execution Snapshot
### Completed
- Socket initialized

### Files Modified
- mods/core/socket.c

### Test / Build Status
- Test failed with EAGAIN

### Remaining Work
- [ ] Handle EAGAIN retry loop

### Exact Next Action
Investigate socket non-blocking flags
`,
			"utf8",
		);

		// Activate quest
		const questCmd = registeredCommands.find((c) => c.name === "quest");
		if (questCmd) await questCmd.handler(questSlug, mockCtx);

		// Tool call edit on project code is attempted -> must be blocked
		let toolCallResult: any;
		for (const cb of handlers["tool_call"] || []) {
			toolCallResult = await cb({ toolName: "edit", input: { path: "mods/core/socket.c" } }, mockCtx);
		}

		assert.ok(toolCallResult, "Tool call must be intercepted by gate");
		assert.strictEqual(toolCallResult.block, true, "Mutation tool must be blocked while reassessment pending");
		assert.ok(toolCallResult.reason.includes("REASSESSMENT_PENDING"), "Block reason must cite REASSESSMENT_PENDING");
		assert.ok(toolCallResult.reason.includes("quest_update_state"), "Block reason must instruct resolving via quest_update_state");

		userMessages.length = 0;

		// Resolve reassessment via quest_update_state
		const updateTool = registeredTools.find((t) => t.name === "quest_update_state");
		assert.ok(updateTool, "quest_update_state must be registered");

		const res: any = await updateTool.execute(
			"call_resolve_1",
			{
				name: questSlug,
				reassessmentComplete: true,
				reassessmentConclusion: "Investigation showed O_NONBLOCK requires explicit epoll ET edge handling. Plan revised to add retry loop.",
				understanding: "O_NONBLOCK requires ET edge-triggered polling with retry loop on EAGAIN.",
				assumptions: ["ET edge-triggered polling handles EAGAIN without busy waiting."],
				openQuestions: ["No further socket blockers."],
				findings: ["Socket socket_write verified to return EAGAIN when buffer fills."],
				plan: ["1. Implement epoll ET loop in socket.c", "2. Run socket unit test suite"],
				planConfidence: "high",
				planConfidenceReason: "Verified against Linux epoll manpage and socket unit test.",
				completed: ["Investigated EAGAIN error condition in epoll worker"],
				filesModified: ["mods/core/socket.c"],
				testStatus: "Unit tests pending rerun with revised retry loop",
				remaining: ["Implement epoll ET loop in socket.c", "Run socket unit test suite"],
				exactNextAction: "Implement epoll ET loop in socket.c and rerun socket unit tests",
			},
			undefined,
			undefined,
			mockCtx,
		);

		assert.ok(res.details?.hash, "quest_update_state must succeed");
		assert.strictEqual(res.details.reassessmentRequired, false, "Reassessment must be marked resolved");

		// Verify that exactly ONE steer message was emitted informing the model that the gate is open
		const steerMessages = userMessages.filter((m) => m.options?.deliverAs === "steer");
		assert.strictEqual(steerMessages.length, 1, "Exactly one steer continuation must be emitted on reassessment resolution");
		const steerMsg = steerMessages[0].msg;
		assert.ok(steerMsg.includes("REASSESSMENT RESOLVED — IMPLEMENTATION GATE OPEN"), "Must identify reassessment resolved header");
		assert.ok(steerMsg.includes("Your implementation gate is now OPEN"), "Must state implementation gate is open");
		assert.ok(steerMsg.includes("EXACT NEXT ACTION"), "Must instruct model to follow current Exact Next Action");

		// Now, subsequent edit tool call must be ALLOWED (not blocked)
		let nextToolCallResult: any;
		for (const cb of handlers["tool_call"] || []) {
			nextToolCallResult = await cb({ toolName: "edit", input: { path: "mods/core/socket.c" } }, mockCtx);
		}
		assert.strictEqual(nextToolCallResult, undefined, "Edit must be permitted after gate opens");
	});

	// -----------------------------------------------------------------------
	// 2. Deduplication: repeated updates do not re-emit continuation steer
	// -----------------------------------------------------------------------
	await t.step("2. Deduplication: repeated updates when gate is already open do not emit duplicate steer", async () => {
		const { mockPi, registeredTools, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);
		const mockCtx = createMockContext();

		const questSlug = "dedup-gate-test";
		const updateTool = registeredTools.find((t) => t.name === "quest_update_state");

		// Initial update establishing open state
		await updateTool.execute(
			"call_1",
			{
				name: questSlug,
				goal: "Verify dedup",
				understanding: "Understanding facts",
				assumptions: ["Assumption valid"],
				openQuestions: ["None"],
				findings: ["Findings logged"],
				plan: ["Step 1", "Step 2"],
				planConfidence: "high",
				researchComplete: true,
				completed: ["Step 1"],
				filesModified: ["src/test.c"],
				testStatus: "Passing",
				remaining: ["Step 2"],
				exactNextAction: "Execute step 2",
			},
			undefined,
			undefined,
			mockCtx,
		);

		userMessages.length = 0;

		// Minor status update when already implementable
		await updateTool.execute(
			"call_2",
			{
				name: questSlug,
				status: "Step 2 in progress",
				exactNextAction: "Continue step 2",
			},
			undefined,
			undefined,
			mockCtx,
		);

		const duplicateSteers = userMessages.filter((m) => m.options?.deliverAs === "steer" && m.msg.includes("GATE OPEN"));
		assert.strictEqual(duplicateSteers.length, 0, "No duplicate continuation steer when gate was already open");
	});

	// -----------------------------------------------------------------------
	// 3. Root confirmation accepted continuation steer
	// -----------------------------------------------------------------------
	await t.step("3. Root confirmation accepted triggers confirmation continuation steer", async () => {
		const { mockPi, handlers, registeredTools, registeredCommands, userMessages } = createMockExtensionAPI();
		plugin(mockPi as any);
		const mockCtx = createMockContext();

		const questSlug = "root-confirm-continuation";
		const questCmd = registeredCommands.find((c) => c.name === "quest");
		if (questCmd) await questCmd.handler(questSlug, mockCtx);

		const updateTool = registeredTools.find((t) => t.name === "quest_update_state");

		// Complete research on root quest -> enters awaitingUserConfirmation
		await updateTool.execute(
			"call_research_1",
			{
				name: questSlug,
				goal: "Root confirmation flow",
				understanding: "Core architecture understood",
				assumptions: ["Assumptions tested"],
				openQuestions: ["Uncertainties resolved"],
				findings: ["Findings verified"],
				plan: ["Implement feature", "Run tests"],
				planConfidence: "high",
				researchComplete: true,
				exactNextAction: "Ask user for confirmation",
			},
			undefined,
			undefined,
			mockCtx,
		);

		userMessages.length = 0;

		// User confirms via ask_questions
		for (const cb of handlers["tool_result"] || []) {
			await cb(
				{
					toolName: "ask_questions",
					input: {
						questions: [
							{
								header: "Confirmation",
								question: "Do you confirm the plan?",
								options: [{ label: "Yes, proceed" }],
							},
						],
					},
					content: [
						{
							type: "text",
							text: "User answers:\n1. Question: Do you confirm the plan?\n   Answer: Yes, proceed",
						},
					],
					details: {
						status: "answered",
						questions: [
							{
								header: "Confirmation",
								question: "Do you confirm the plan?",
								options: [{ label: "Yes, proceed" }],
							},
						],
						answers: [
							{
								questionIndex: 0,
								header: "Confirmation",
								question: "Do you confirm the plan?",
								answer: "Yes, proceed",
							},
						],
					},
				},
				mockCtx,
			);
		}

		// Assert confirmation continuation steer was emitted
		const confirmSteers = userMessages.filter((m) => m.options?.deliverAs === "steer" && m.msg.includes("CONFIRMATION ACCEPTED"));
		assert.strictEqual(confirmSteers.length, 1, "Confirmation acceptance must emit confirmation continuation steer");
		assert.ok(confirmSteers[0].msg.includes("Your implementation gate is now OPEN"));
	});

	// Clean up
	await rm(QUEST_DIR, { recursive: true, force: true });
});
