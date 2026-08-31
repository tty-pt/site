import assert from "node:assert";
import { existsSync, mkdirSync, readFileSync, rmSync, writeFileSync } from "node:fs";
import { mkdir, rm, writeFile } from "node:fs/promises";
import questJournalExtension from "../index.ts";
import { questDirPath, questPath } from "../src/paths.ts";
import { QuestErrorCode } from "../src/constants.ts";
import { analyzeTurnToolResults, classifyActivityPhase } from "../src/hooks.ts";
import {
	clearQuestLog,
	formatContextFields,
	formatLogEntry,
	getQuestLogPath,
	isQuestLoggingDegraded,
	logAgentMessageTransition,
	logEvent,
	logGateTransition,
	logImplementationOutcome,
	logReassessmentTransition,
	logResearchTransition,
	logToolActivity,
	logTurnBoundary,
	logVerificationTransition,
	mapEventTypeToMajorPhase,
	normalizeLogPath,
	parseLogEntry,
	QuestLogContext,
	readQuestLog,
	resetQuestLoggingDegraded,
	sanitizeLogString,
	summarizeQuestJournalLog,
} from "../src/logging.ts";
import { getActiveContext, getSessionId, getState, state } from "../src/state.ts";

type EventCallback = (event: any, ctx: any) => Promise<any>;

Deno.test("observability_logging: path normalization, quoted context parsing, and phase classification", async (t) => {
	await t.step("1. normalizeLogPath cleans paths consistently without altering project semantics", () => {
		const cwd = "/home/user/project";
		assert.strictEqual(normalizeLogPath("./src/router.ts", cwd), "src/router.ts");
		assert.strictEqual(normalizeLogPath("/home/user/project/src/router.ts", cwd), "src/router.ts");
		assert.strictEqual(normalizeLogPath("/home/user/project/.pi/quest/current/qid123/quest.md", cwd), ".pi/quest/current/qid123/quest.md");
		assert.strictEqual(normalizeLogPath("tests/unit/test.ts", cwd), "tests/unit/test.ts");
		assert.strictEqual(normalizeLogPath('"src/state.ts"', cwd), "src/state.ts");
		assert.strictEqual(normalizeLogPath("./a//b///c.ts", cwd), "a/b/c.ts");
		assert.strictEqual(normalizeLogPath("", cwd), "");
		assert.strictEqual(normalizeLogPath(undefined, cwd), "");
	});

	await t.step("2. formatContextFields quotes values with spaces and parseLogEntry round-trips them", () => {
		const ctx: QuestLogContext = {
			tool: "bash",
			operation: "failure",
			phase: "verification",
			command: "deno test tests/foo.test.ts",
			query: "compaction resume state",
			path: "src/router.ts",
			reason: "assertion failed: expected 2 got 1",
			turn: 2,
			correlationId: "turn_2_abc123",
		};

		const formatted = formatContextFields(ctx);
		assert.ok(formatted.includes('command="deno test tests/foo.test.ts"'));
		assert.ok(formatted.includes('query="compaction resume state"'));
		assert.ok(formatted.includes('reason="assertion failed: expected 2 got 1"'));
		assert.ok(formatted.includes("tool=bash"));
		assert.ok(formatted.includes("operation=failure"));
		assert.ok(formatted.includes("phase=verification"));
		assert.ok(formatted.includes("path=src/router.ts"));
		assert.ok(formatted.includes("turn=2"));
		assert.ok(formatted.includes("correlationId=turn_2_abc123"));

		const line = formatLogEntry("TOOL_ACTIVITY", "bash command execution", ctx);
		const parsed = parseLogEntry(line);

		assert.ok(parsed);
		assert.strictEqual(parsed.type, "TOOL_ACTIVITY");
		assert.strictEqual(parsed.context.tool, "bash");
		assert.strictEqual(parsed.context.operation, "failure");
		assert.strictEqual(parsed.context.phase, "verification");
		assert.strictEqual(parsed.context.command, "deno test tests/foo.test.ts");
		assert.strictEqual(parsed.context.query, "compaction resume state");
		assert.strictEqual(parsed.context.path, "src/router.ts");
		assert.strictEqual(parsed.context.reason, "assertion failed: expected 2 got 1");
		assert.strictEqual(parsed.context.turn, "2");
		assert.strictEqual(parsed.context.correlationId, "turn_2_abc123");
	});

	await t.step("3. classifyActivityPhase correctly categorizes actions into major lifecycle phases", () => {
		// Research phase during active research
		const researchState: any = { researchRequired: true, researchComplete: false };
		assert.strictEqual(classifyActivityPhase("read", { path: "src/foo.ts" }, researchState), "research");
		assert.strictEqual(classifyActivityPhase("search_graph", { query: "state" }, researchState), "research");
		assert.strictEqual(classifyActivityPhase("web_search", { query: "docs" }, researchState), "research");
		assert.strictEqual(classifyActivityPhase("quest_update_state", {}, researchState), "planning");
		assert.strictEqual(classifyActivityPhase("quest_mark_saved", {}, researchState), "checkpoint");

		// Verification phase for test/build commands
		assert.strictEqual(classifyActivityPhase("bash", { command: "make test" }, researchState), "verification");
		assert.strictEqual(classifyActivityPhase("bash", { command: "deno test tests/bar.test.ts" }, researchState), "verification");
		assert.strictEqual(classifyActivityPhase("bash", { command: "npm run build" }, researchState), "verification");

		// Implementation phase when research is complete and implementation allowed
		const implState: any = { researchRequired: false, researchComplete: true, implementationAllowed: true };
		assert.strictEqual(classifyActivityPhase("edit", { path: "src/foo.ts" }, implState), "implementation");
		assert.strictEqual(classifyActivityPhase("write", { path: "src/new.ts" }, implState), "implementation");
		assert.strictEqual(classifyActivityPhase("subagent", { task: "fix bug" }, implState), "implementation");
		// Reads during implementation mode do not guess 'implementation'; phase is omitted (undefined)
		assert.strictEqual(classifyActivityPhase("read", { path: "src/foo.ts" }, implState), undefined);

		// Reassessment phase when reassessment is active
		const reassessState: any = { reassessmentRequired: true };
		assert.strictEqual(classifyActivityPhase("read", { path: "src/foo.ts" }, reassessState), "reassessment");
		assert.strictEqual(classifyActivityPhase("search_graph", { query: "trace" }, reassessState), "reassessment");
		assert.strictEqual(classifyActivityPhase("bash", { command: "make test" }, reassessState), "verification");
	});
});

Deno.test("observability_logging: tool lifecycle events and causal chain verification", async (t) => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	const testQid = "test_obs_qid";
	state.questId = testQid;
	state.active = "obs-quest";
	state.stack = ["obs-quest"];
	state.researchRequired = true;
	state.researchComplete = false;
	state.implementationAllowed = false;
	state.currentTurn = 1;
	state.currentTurnCorrelationId = "turn_1_xyz";

	const mockCtx: any = {
		cwd: "/test/workspace",
		getContextUsage: () => ({ tokens: 10000, contextWindow: 1000000, percent: 1 }),
		sessionManager: { getBranch: () => [] },
		ui: { notify: () => {}, setStatus: () => {}, input: async () => "", select: async () => null },
		hasUI: true,
		mode: "headless",
	};

	const sInitial = getState(mockCtx);
	sInitial.questId = testQid;
	sInitial.active = "obs-quest";
	sInitial.stack = ["obs-quest"];
	sInitial.researchRequired = true;
	sInitial.researchComplete = false;
	sInitial.implementationAllowed = false;

	const tempLogPath = getQuestLogPath(testQid);
	clearQuestLog(tempLogPath);

	const handlers: Record<string, EventCallback[]> = {};
	const tools: Record<string, any> = {};
	const commands: Record<string, any> = {};
	const userMessages: Array<{ msg: any; options?: any }> = [];

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
		sendMessage(msg: any, options?: any) {
			userMessages.push({ msg: msg?.content || msg, options });
		},
	};

	questJournalExtension(mockPi);

	await t.step("1. read result logs tool, normalized path, and phase", async () => {
		for (const cb of handlers["tool_result"] || []) {
			await cb(
				{
					toolName: "read",
					input: { path: "./src/router.ts" },
					content: "router code content...",
					isError: false,
				},
				mockCtx,
			);
		}

		const log = readQuestLog(tempLogPath);
		assert.ok(log.includes("TOOL_ACTIVITY"));
		assert.ok(log.includes("tool=read"));
		assert.ok(log.includes("operation=success"));
		assert.ok(log.includes("phase=research"));
		assert.ok(log.includes("path=src/router.ts"));
		assert.ok(!log.includes("router code content"), "Tool output content must NOT be dumped in the log");
	});

	await t.step("2. search_graph logs tool, search target/query, and phase", async () => {
		for (const cb of handlers["tool_result"] || []) {
			await cb(
				{
					toolName: "search_graph",
					input: { name_pattern: ".*OrderHandler.*" },
					content: "graph match snippet...",
					isError: false,
				},
				mockCtx,
			);
		}

		const log = readQuestLog(tempLogPath);
		assert.ok(log.includes("tool=search_graph"));
		assert.ok(log.includes("operation=success"));
		assert.ok(log.includes("phase=research"));
		assert.ok(log.includes("query=.*OrderHandler.*") || log.includes('query=".*OrderHandler.*"'));
	});

	await t.step("3. Blocked implementation attempt logs GATE_BLOCKED, IMPLEMENTATION_BLOCKED, and blocked TOOL_ACTIVITY", async () => {
		for (const cb of handlers["tool_call"] || []) {
			await cb(
				{
					toolName: "edit",
					input: { path: "src/router.ts", edits: [] },
				},
				mockCtx,
			);
		}

		const log = readQuestLog(tempLogPath);
		assert.ok(log.includes("GATE_BLOCKED"));
		assert.ok(log.includes("IMPLEMENTATION_BLOCKED"));
		assert.ok(log.includes("tool=edit"));
		assert.ok(log.includes("operation=blocked"));
		assert.ok(log.includes("path=src/router.ts"));
	});

	await t.step("4. After research completion and confirmation, edit/write logs successful implementation activity", async () => {
		state.researchRequired = false;
		state.researchComplete = true;
		state.implementationAllowed = true;
		state.confirmedQuests = ["obs-quest"];

		for (const cb of handlers["tool_result"] || []) {
			await cb(
				{
					toolName: "edit",
					input: { path: "src/router.ts" },
					content: "edit applied",
					isError: false,
				},
				mockCtx,
			);
		}

		const log = readQuestLog(tempLogPath);
		assert.ok(log.includes("tool=edit"));
		assert.ok(log.includes("operation=success"));
		assert.ok(log.includes("phase=implementation"));
		assert.ok(log.includes("path=src/router.ts"));
	});

	await t.step("5. Failed verification command produces full causal sequence (TOOL_ACTIVITY -> TEST_FAILED -> REASSESSMENT_REQUIRED)", async () => {
		const mockFailToolResult = {
			toolName: "bash",
			input: { command: "make test" },
			args: { command: "make test" },
			content: "FAIL: test_orders assertion failed: expected 200 got 500\n" + "x".repeat(4000),
			isError: true,
		};

		// 1. Tool result fires
		for (const cb of handlers["tool_result"] || []) {
			await cb(mockFailToolResult, mockCtx);
		}

		// 2. Turn end fires
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [mockFailToolResult] }, mockCtx);
		}

		const log = readQuestLog(tempLogPath);
		assert.ok(log.includes("tool=bash"));
		assert.ok(log.includes("operation=failure"));
		assert.ok(log.includes("phase=verification"));
		assert.ok(log.includes('command="make test"'));
		assert.ok(log.includes("TEST_FAILED"));
		assert.ok(log.includes("REASSESSMENT_REQUIRED"));

		// Verify causal ordering in lines
		const lines = log.split("\n").filter((l) => l.trim().length > 0);
		const toolActivityIdx = lines.findIndex((l) => l.includes("TOOL_ACTIVITY") && l.includes("command=\"make test\"") && l.includes("operation=failure"));
		const testFailedIdx = lines.findIndex((l) => l.includes("TEST_FAILED"));
		const reassessIdx = lines.findIndex((l) => l.includes("REASSESSMENT_REQUIRED"));

		assert.ok(toolActivityIdx >= 0, "TOOL_ACTIVITY for failed bash command must exist");
		assert.ok(testFailedIdx > toolActivityIdx, "TEST_FAILED must follow failed TOOL_ACTIVITY");
		assert.ok(reassessIdx > testFailedIdx, "REASSESSMENT_REQUIRED must follow TEST_FAILED");
	});

	await t.step("6. TURN_END logs accurate breakdown counts (reads, searches, writes, commands, mutations, failures)", async () => {
		const toolResults = [
			{ toolName: "read", input: { path: "src/a.ts" } },
			{ toolName: "read", input: { path: "src/b.ts" } },
			{ toolName: "search_graph", input: { query: "User" } },
			{ toolName: "edit", input: { path: "src/a.ts" } },
			{ toolName: "write", input: { path: "src/c.ts" } },
			{ toolName: "bash", input: { command: "make test" }, isError: true, content: "FAIL" },
		];

		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults }, mockCtx);
		}

		const log = readQuestLog(tempLogPath);
		const lines = log.split("\n").filter((l) => l.includes("TURN_END"));
		const lastTurnEnd = lines[lines.length - 1];

		assert.ok(lastTurnEnd.includes("reads=2"), `TURN_END must include reads=2: ${lastTurnEnd}`);
		assert.ok(lastTurnEnd.includes("searches=1"), `TURN_END must include searches=1: ${lastTurnEnd}`);
		assert.ok(lastTurnEnd.includes("writes=2"), `TURN_END must include writes=2: ${lastTurnEnd}`);
		assert.ok(lastTurnEnd.includes("commands=1"), `TURN_END must include commands=1: ${lastTurnEnd}`);
		assert.ok(lastTurnEnd.includes("failures=1"), `TURN_END must include failures=1: ${lastTurnEnd}`);
	});

	await t.step("7. Integration scenario: read -> read -> edit -> test failure is completely reconstructable", async () => {
		clearQuestLog(tempLogPath);

		const s = getState(mockCtx);
		s.questId = testQid;
		s.active = "obs-quest";
		s.stack = ["obs-quest"];
		s.currentTurn = 10;
		s.currentTurnCorrelationId = "turn_10_corr";
		s.reassessmentRequired = false;
		s.researchComplete = true;
		s.researchRequired = false;
		s.implementationAllowed = true;
		s.confirmedQuests = ["obs-quest"];
		s.prompts = ["Refactor order processing logic"];
		s.refinements = [];
		s.pendingNotifications = [];
		s.pendingResume = null;
		s.activeTransaction = null;
		s.consecutiveFailures = 0;

		state.questId = testQid;
		state.active = "obs-quest";
		state.stack = ["obs-quest"];
		state.currentTurn = 10;
		state.currentTurnCorrelationId = "turn_10_corr";
		state.reassessmentRequired = false;
		state.researchComplete = true;
		state.researchRequired = false;
		state.implementationAllowed = true;
		state.confirmedQuests = ["obs-quest"];
		state.prompts = ["Refactor order processing logic"];
		state.refinements = [];
		state.pendingNotifications = [];
		state.pendingResume = null;
		state.activeTransaction = null;
		state.consecutiveFailures = 0;

		// Agent starts turn
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Refactor order processing logic" }, mockCtx);
		}

		// Tool 1: read order.ts
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "./src/order.ts" }, content: "order code", isError: false }, mockCtx);
		}

		// Tool 2: read payment.ts
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "./src/payment.ts" }, content: "payment code", isError: false }, mockCtx);
		}

		// Tool 3: edit order.ts
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: "./src/order.ts" }, content: "updated order", isError: false }, mockCtx);
		}

		// Tool 4: bash make test (fails)
		const testFail = {
			toolName: "bash",
			input: { command: "make test" },
			args: { command: "make test" },
			content: "FAIL: order_test.c:45 assertion failed: balance != 0",
			isError: true,
		};
		for (const cb of handlers["tool_result"] || []) {
			await cb(testFail, mockCtx);
		}

		// Turn end
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "read" }, { toolName: "read" }, { toolName: "edit" }, testFail] }, mockCtx);
		}

		const fullLog = readQuestLog(tempLogPath);
		const summary = summarizeQuestJournalLog(fullLog);

		assert.ok(summary.failureCount >= 1, `failureCount should be at least 1, got ${summary.failureCount}`);
		assert.strictEqual(summary.reassessmentCycles >= 1, true);

		// Check parsed sequence
		const entries = fullLog
			.split("\n")
			.map((l) => parseLogEntry(l))
			.filter((e): e is NonNullable<typeof e> => e !== null);

		const toolActivities = entries.filter((e) => e.type === "TOOL_ACTIVITY");
		assert.strictEqual(toolActivities.length, 4, "Must have exactly 4 TOOL_ACTIVITY entries in order");

		assert.strictEqual(toolActivities[0].context.tool, "read");
		assert.strictEqual(toolActivities[0].context.path, "src/order.ts");
		assert.strictEqual(toolActivities[0].context.operation, "success");

		assert.strictEqual(toolActivities[1].context.tool, "read");
		assert.strictEqual(toolActivities[1].context.path, "src/payment.ts");
		assert.strictEqual(toolActivities[1].context.operation, "success");

		assert.strictEqual(toolActivities[2].context.tool, "edit");
		assert.strictEqual(toolActivities[2].context.path, "src/order.ts");
		assert.strictEqual(toolActivities[2].context.operation, "success");

		assert.strictEqual(toolActivities[3].context.tool, "bash");
		assert.strictEqual(toolActivities[3].context.command, "make test");
		assert.strictEqual(toolActivities[3].context.operation, "failure");

		// Exactly one REASSESSMENT_REQUIRED event emitted for this trigger (no duplicate)
		const reassessEvents = entries.filter((e) => e.type === "REASSESSMENT_REQUIRED");
		assert.strictEqual(reassessEvents.length, 1, "Must have exactly 1 REASSESSMENT_REQUIRED entry (no duplicates)");
	});

	await t.step("8. Full end-to-end causal trace: investigation -> changes -> verification -> failure -> reassessment -> renewed investigation -> subsequent fix -> passing test", async () => {
		clearQuestLog(tempLogPath);

		// Initialize state in research phase
		const s = getState(mockCtx);
		s.questId = testQid;
		s.active = "obs-quest";
		s.stack = ["obs-quest"];
		s.currentTurn = 0;
		s.currentTurnCorrelationId = "";
		s.reassessmentRequired = false;
		s.researchRequired = true;
		s.researchComplete = false;
		s.implementationAllowed = false;
		s.confirmedQuests = [];
		s.prompts = ["Build order feature"];
		s.refinements = [];
		s.pendingNotifications = [];
		s.pendingResume = null;
		s.activeTransaction = null;
		s.consecutiveFailures = 0;

		state.questId = testQid;
		state.active = "obs-quest";
		state.stack = ["obs-quest"];
		state.currentTurn = 0;
		state.currentTurnCorrelationId = "";
		state.reassessmentRequired = false;
		state.researchRequired = true;
		state.researchComplete = false;
		state.implementationAllowed = false;
		state.confirmedQuests = [];
		state.prompts = ["Build order feature"];

		// --- Turn 1: Initial research ---
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Build order feature" }, mockCtx);
		}
		for (const cb of handlers["turn_start"] || []) {
			await cb({ turnIndex: 1, timestamp: Date.now() }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "src/order.ts" }, content: "order code", isError: false }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "search_graph", input: { query: "OrderService" }, content: "symbol", isError: false }, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "read" }, { toolName: "search_graph" }] }, mockCtx);
		}

		// Unlock implementation (research completed + confirmed)
		state.researchRequired = false;
		state.researchComplete = true;
		state.implementationAllowed = true;
		state.confirmedQuests = ["obs-quest"];
		s.researchRequired = false;
		s.researchComplete = true;
		s.implementationAllowed = true;
		s.confirmedQuests = ["obs-quest"];

		// --- Turn 2: Changes & Verification (Fails) ---
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Apply changes and test" }, mockCtx);
		}
		for (const cb of handlers["turn_start"] || []) {
			await cb({ turnIndex: 2, timestamp: Date.now() }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: "src/order.ts" }, content: "changes", isError: false }, mockCtx);
		}
		const failTest = {
			toolName: "bash",
			input: { command: "make test" },
			args: { command: "make test" },
			content: "FAIL: test_order assertion failed",
			isError: true,
		};
		for (const cb of handlers["tool_result"] || []) {
			await cb(failTest, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit" }, failTest] }, mockCtx);
		}

		// Now state is in reassessment
		assert.strictEqual(state.reassessmentRequired, true);

		// --- Turn 3: Renewed investigation during reassessment ---
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Investigate test failure" }, mockCtx);
		}
		for (const cb of handlers["turn_start"] || []) {
			await cb({ turnIndex: 3, timestamp: Date.now() }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "tests/order_test.c" }, content: "test code", isError: false }, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "read" }] }, mockCtx);
		}

		// Resolve reassessment & allow fix
		state.reassessmentRequired = false;
		state.implementationAllowed = true;
		s.reassessmentRequired = false;
		s.implementationAllowed = true;

		// --- Turn 4: Subsequent fix & passing test ---
		for (const cb of handlers["before_agent_start"] || []) {
			await cb({ prompt: "Fix bug and verify" }, mockCtx);
		}
		for (const cb of handlers["turn_start"] || []) {
			await cb({ turnIndex: 4, timestamp: Date.now() }, mockCtx);
		}
		for (const cb of handlers["tool_result"] || []) {
			await cb({ toolName: "edit", input: { path: "src/order.ts" }, content: "fixed order", isError: false }, mockCtx);
		}
		const passTest = {
			toolName: "bash",
			input: { command: "make test" },
			args: { command: "make test" },
			content: "PASS: all tests passed",
			isError: false,
		};
		for (const cb of handlers["tool_result"] || []) {
			await cb(passTest, mockCtx);
		}
		for (const cb of handlers["turn_end"] || []) {
			await cb({ toolResults: [{ toolName: "edit" }, passTest] }, mockCtx);
		}

		const fullLog = readQuestLog(tempLogPath);
		const entries = fullLog
			.split("\n")
			.map((l) => parseLogEntry(l))
			.filter((e): e is NonNullable<typeof e> => e !== null);

		// Verify that the full causal sequence can be cleanly reconstructed from execution.log
		const types = entries.map((e) => e.type);

		assert.ok(types.includes("TURN_START"), "Must include TURN_START");
		assert.ok(types.includes("TOOL_ACTIVITY"), "Must include TOOL_ACTIVITY");
		assert.ok(types.includes("TEST_FAILED"), "Must include TEST_FAILED");
		assert.ok(types.includes("REASSESSMENT_REQUIRED"), "Must include REASSESSMENT_REQUIRED");
		assert.ok(types.includes("TEST_PASSED"), "Must include TEST_PASSED");
		assert.ok(types.includes("TURN_END"), "Must include TURN_END");

		// Verify tool activities across turns
		const activities = entries.filter((e) => e.type === "TOOL_ACTIVITY");
		assert.strictEqual(activities.length, 7, "Must have exactly 7 tool activities across 4 turns");

		// Check turn correlation and phase labels
		const t1Read = activities.find((a) => a.context.path === "src/order.ts" && a.context.turn === "1");
		assert.ok(t1Read, "Turn 1 read must exist");
		assert.strictEqual(t1Read.context.phase, "research");

		const t2Edit = activities.find((a) => a.context.tool === "edit" && a.context.turn === "2");
		assert.ok(t2Edit, "Turn 2 edit must exist");
		assert.strictEqual(t2Edit.context.phase, "implementation");

		const t2Fail = activities.find((a) => a.context.tool === "bash" && a.context.turn === "2");
		assert.ok(t2Fail, "Turn 2 bash fail must exist");
		assert.strictEqual(t2Fail.context.operation, "failure");
		assert.strictEqual(t2Fail.context.phase, "verification");

		const t3Read = activities.find((a) => a.context.path === "tests/order_test.c" && a.context.turn === "3");
		assert.ok(t3Read, "Turn 3 read must exist");
		assert.strictEqual(t3Read.context.phase, "reassessment");

		const t4Edit = activities.find((a) => a.context.tool === "edit" && a.context.turn === "4");
		assert.ok(t4Edit, "Turn 4 edit must exist");
		assert.strictEqual(t4Edit.context.phase, "implementation");

		const t4Pass = activities.find((a) => a.context.tool === "bash" && a.context.turn === "4");
		assert.ok(t4Pass, "Turn 4 bash pass must exist");
		assert.strictEqual(t4Pass.context.operation, "success");
		assert.strictEqual(t4Pass.context.phase, "verification");
	});

	// Cleanup
	try {
		rmSync(tempLogPath, { force: true });
	} catch {}
});
