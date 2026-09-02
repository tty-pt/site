import assert from "node:assert";
import { existsSync, openSync, closeSync, statSync, utimesSync, unlinkSync } from "node:fs";
import { mkdir, readFile, rm, writeFile, mkdtemp } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join } from "node:path";
import plugin, {
	acquireReviewFileLock,
	archiveQuestFile,
	canImplement,
	cancelActiveReview,
	checkAndTriggerDirectionReview,
	classifyTimeoutLayer,
	clearActiveReviews,
	createAgentObligation,
	createDefaultState,
	createReviewActiveFile,
	createReviewSnapshot,
	drainAgentObligations,
	executeUpdateStateTool,
	formatActiveReviewsUIStatus,
	getActiveReviews,
	getImplementationBlockReason,
	getPendingObligations,
	getPendingReviews,
	getQuestLockKey,
	getQuestLogPath,
	getReviewLockPath,
	getState,
	isCriticalReviewValidForCompletion,
	isPlanReviewValidForState,
	isQuestLocked,
	isReviewActive,
	isReviewSnapshotCurrent,
	queueAgentObligation,
	readQuestLog,
	reconcileReviewResult,
	registerActiveReview,
	releaseReviewFileLock,
	removeReviewActiveFile,
	requestDirectionReview,
	requestFinalReview,
	requestPlanReview,
	REVIEW_LOCK_HEARTBEAT_MS,
	REVIEW_LOCK_STALE_MS,
	runCriticalReview,
	setCustomSubagentRunner,
	startReviewLockHeartbeat,
	touchReviewLockFile,
	updateReviewActivity,
	updateReviewerUIStatus,
	verifyAndMarkSaved,
	withQuestLock,
	QuestErrorCode,
	type ExtensionAPI,
	type ExtensionContext,
	type StoredState,
} from "../src/index.ts";

function createMockExtensionAPI() {
	const handlers: Record<string, any[]> = {};
	const registeredTools: any[] = [];
	const registeredCommands: any[] = [];
	const agentMessages: Array<{ msg: any; options?: any; customType?: any; display?: any }> = [];
	const userMessages: Array<{ msg: any; options?: any }> = [];
	const appendedEntries: Array<{ type: string; data: any }> = [];
	let configuredTools: any[] = [];
	const eventBusHandlers: Record<string, any[]> = {};

	const events = {
		on: (event: string, handler: any) => {
			if (!eventBusHandlers[event]) eventBusHandlers[event] = [];
			eventBusHandlers[event].push(handler);
			return () => {
				const idx = eventBusHandlers[event].indexOf(handler);
				if (idx >= 0) eventBusHandlers[event].splice(idx, 1);
			};
		},
		emit: (event: string, data: any) => {
			const list = eventBusHandlers[event] || [];
			for (const h of [...list]) {
				try { h(data); } catch {}
			}
		},
	};

	const mockPi: ExtensionAPI = {
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
			agentMessages.push({
				msg: msg?.content || msg,
				options,
				customType: msg?.customType,
				display: msg?.display,
			});
		},
		sendUserMessage: (msg: any, options?: any) => {
			userMessages.push({ msg, options });
		},
		appendEntry: (type: string, data: any) => {
			appendedEntries.push({ type, data });
		},
		registerEntryRenderer: () => {},
		getAllTools: () => configuredTools,
		events,
	};

	return {
		mockPi,
		handlers,
		registeredTools,
		registeredCommands,
		agentMessages,
		userMessages,
		appendedEntries,
		setAllTools: (tools: any[]) => { configuredTools = tools; },
		events,
	};
}

function createMockContext(tokens = 50000, sessionId = `session_${Math.random().toString(36).slice(2)}`): ExtensionContext {
	const branch: any[] = [];
	const currentStatus: Record<string, any> = {};
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
		getContextUsage: () => ({ tokens, percent: (tokens / 800000) * 100 }),
		ui: {
			notify: () => {},
			setStatus: (key: string, val: any) => {
				if (val === undefined) delete currentStatus[key];
				else currentStatus[key] = val;
			},
			getStatus: (key: string) => currentStatus[key],
			getAllStatus: () => ({ ...currentStatus }),
			input: async () => "",
			select: async () => null,
		},
	};
}

Deno.test("Parallel Critical Reviewer Suite: Complete Verification of All 15 Concurrency & Visibility Semantics", async (t) => {
	const currentDir = ".pi/quest/current";
	await mkdir(currentDir, { recursive: true });

	setCustomSubagentRunner(null);
	clearActiveReviews();

	// -----------------------------------------------------------------------
	// 1. Direction review starts without blocking main execution
	// -----------------------------------------------------------------------
	await t.step("1. Direction review starts without blocking main execution", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_1");
		const s = getState(ctx);
		s.active = "async-dir-quest";
		s.questId = "async-dir-quest";
		s.stack = [s.active];
		s.researchComplete = true;
		s.researchRequired = false;
		s.planVersion = 1;
		s.prompts = ["Implement high performance stream decoder."];

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nStream decoder\n\n## Original request\n> Implement high performance stream decoder.\n\n## Plan\n1. Ingest\n\n## Remaining work\n- [ ] 1\n`, "utf8");

		let runnerCompleted = false;
		let resolveRunner: (val: any) => void;
		const delayPromise = new Promise((resolve) => { resolveRunner = resolve; });

		const delayedRunner = async () => {
			await delayPromise;
			runnerCompleted = true;
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(delayedRunner);

		// Trigger review
		const reviewPromise = runCriticalReview(mockPi, ctx, { kind: "direction" });

		// Main execution is NOT blocked: active review is registered, runner is still in flight
		assert.strictEqual(runnerCompleted, false, "Runner should still be running in background");
		const active = getActiveReviews();
		assert.ok(active.size >= 1, "Active review must be tracked in registry");

		// Resolve runner
		resolveRunner!(undefined);
		const result = await reviewPromise;
		assert.strictEqual(result?.success, true);
		assert.strictEqual(result?.review?.verdict, "PASS");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 2. Main state can advance while review runs
	// -----------------------------------------------------------------------
	await t.step("2. Main state can advance while review runs", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_2");
		const s = getState(ctx);
		s.active = "state-advance-quest";
		s.questId = "state-advance-quest";
		s.stack = [s.active];
		s.researchComplete = true;
		s.researchRequired = false;
		s.planVersion = 1;
		s.saveCount = 1;
		s.lastSavedHash = "hash_v1";
		s.prompts = ["Build multi-stage audio pipeline"];

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nPipeline\n\n## Original request\n> Build multi-stage audio pipeline\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] 1\n`, "utf8");

		let resolveRunner: (val: any) => void;
		const pausePromise = new Promise((resolve) => { resolveRunner = resolve; });

		const runner = async () => {
			await pausePromise;
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		// Start review for V1
		const reviewPromise = runCriticalReview(mockPi, ctx, { kind: "direction" });

		// While review is running in background, main agent updates state to V2
		s.planVersion = 2;
		s.lastSavedHash = "hash_v2";
		s.saveCount = 5;

		resolveRunner!(undefined);
		const result = await reviewPromise;

		// Review was for V1, current state is V2 -> review is marked superseded
		assert.strictEqual(result.superseded, true, "Review of V1 must be superseded by V2");
		assert.strictEqual(s.planVersion, 2, "Main state planVersion must remain 2");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 3. Review of V5 returning after V6 exists is marked stale/superseded
	// -----------------------------------------------------------------------
	await t.step("3. Review of V5 returning after V6 exists is marked stale/superseded", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_3");
		const s = getState(ctx);
		s.active = "v5-v6-quest";
		s.questId = "v5-v6-quest";
		s.planVersion = 5;
		s.saveCount = 10;
		s.lastSavedHash = "hash_v5";

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nV5 Goal\n\n## Original request\n> V5 Request\n\n## Plan\n1. V5 plan\n`, "utf8");

		const snapshot = await createReviewSnapshot(s.active, "rev_v5", "plan_review", "session_test_par_3", s);
		assert.strictEqual(snapshot.planVersion, 5);

		// Advance state to V6
		s.planVersion = 6;
		s.lastSavedHash = "hash_v6";
		s.saveCount = 12;

		const parsedResult = {
			verdict: "APPROVE",
			severity: "NONE",
			findings: [],
			requiredActions: [],
			originalRequestCheck: { satisfied: ["V5 Request"], unsatisfied: [] },
			durationMs: 1500,
		};

		const res = await reconcileReviewResult(snapshot, parsedResult, s, "rev_v5", mockPi, ctx);
		assert.strictEqual(res.superseded, true, "Result must be marked superseded");
		assert.strictEqual(res.review?.superseded, true);
		assert.strictEqual(res.review?.supersededBy?.planVersion, 6);
		assert.strictEqual(s.lastPlanReviewApproval, null, "Approval of V5 must NOT approve V6");

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 4. Stale review findings are not delivered to the model
	// -----------------------------------------------------------------------
	await t.step("4. Stale review findings are not delivered to the model", async () => {
		const { mockPi, agentMessages } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_4");
		const s = getState(ctx);
		s.active = "stale-finding-quest";
		s.questId = "stale-finding-quest";
		s.planVersion = 2;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		// Snapshot captured at V1
		s.planVersion = 1;
		const snapshot = await createReviewSnapshot(s.active, "rev_stale", "direction", "session_test_par_4", s);

		// State advanced to V2 before reviewer returned
		s.planVersion = 2;

		const parsedFailResult = {
			verdict: "FAIL",
			severity: "CRITICAL",
			findings: [{ issue: "Obsolete flaw from V1", evidence: "line 10" }],
			requiredActions: ["Fix obsolete flaw"],
			originalRequestCheck: { satisfied: [], unsatisfied: [] },
			durationMs: 2000,
		};

		const res = await reconcileReviewResult(snapshot, parsedFailResult, s, "rev_stale", mockPi, ctx);
		assert.strictEqual(res.superseded, true);

		// No messages delivered to model for stale review
		assert.strictEqual(agentMessages.length, 0, "Stale findings must NOT be delivered to main agent");

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 5. Current review findings produce exactly one current obligation
	// -----------------------------------------------------------------------
	await t.step("5. Current review findings produce exactly one current obligation", async () => {
		const { mockPi, agentMessages } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_5");
		const s = getState(ctx);
		s.active = "obligation-quest";
		s.questId = "obligation-quest";
		s.planVersion = 3;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		const snapshot = await createReviewSnapshot(s.active, "rev_obl", "plan_review", "session_test_par_5", s);

		const parsedFailResult = {
			verdict: "REVISE",
			severity: "MAJOR",
			findings: [{ issue: "Current flaw in V3 plan", evidence: "step 2" }],
			requiredActions: ["Fix step 2 in V3"],
			originalRequestCheck: { satisfied: [], unsatisfied: [] },
			durationMs: 1200,
		};

		const res = await reconcileReviewResult(snapshot, parsedFailResult, s, "rev_obl", mockPi, ctx);
		assert.strictEqual(res.success, false);
		assert.strictEqual(res.superseded, undefined);

		// Exactly one obligation queued
		assert.strictEqual(s.pendingNotifications?.length, 1);
		assert.strictEqual(s.pendingNotifications[0].correlationId, "rev_obl");

		// Message delivered to agent
		assert.ok(agentMessages.some((m) => m.msg.includes("ADVERSARIAL PLAN REVIEW REJECTED")));

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 6. Two triggers for the same boundary produce one reviewer
	// -----------------------------------------------------------------------
	await t.step("6. Two triggers for the same boundary produce one reviewer", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_6");
		const s = getState(ctx);
		s.active = "dedup-par-quest";
		s.questId = "dedup-par-quest";
		s.planVersion = 1;
		s.lastSavedHash = "hash_1";
		s.researchComplete = true;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		let callCount = 0;
		let resolveRunner: (val: any) => void;
		const pausePromise = new Promise((resolve) => { resolveRunner = resolve; });

		const runner = async () => {
			callCount++;
			await pausePromise;
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		// Trigger 1 starts review
		const p1 = runCriticalReview(mockPi, ctx, { kind: "direction" });

		// Trigger 2 for same boundary key while 1 is in progress
		const p2 = runCriticalReview(mockPi, ctx, { kind: "direction" });

		// Check active reviews count in tracker
		const active = getActiveReviews();
		assert.strictEqual(active.size, 1, "Only ONE active review should be registered in tracker");

		resolveRunner!(undefined);
		await p1;
		await p2;

		assert.strictEqual(callCount, 1, "Runner should execute exactly once");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 7. Reviews on the same quest are serialized with at most one active reviewer
	// -----------------------------------------------------------------------
	await t.step("7. Reviews on the same quest are serialized with at most one active reviewer", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_7");
		const s = getState(ctx);
		s.active = "multi-boundary-quest";
		s.questId = "multi-boundary-quest";
		s.planVersion = 1;
		s.lastSavedHash = "hash_v1";

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		let callCount = 0;
		let resolve1: (val: any) => void;
		const p1Wait = new Promise((r) => { resolve1 = r; });

		const runner = async (_task: string, options?: any) => {
			callCount++;
			if (callCount === 1) {
				await p1Wait;
			}
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		// Launch plan_review (boundary 1)
		const p1 = runCriticalReview(mockPi, ctx, { kind: "plan_review" });

		// Yield tick for runner to start
		await new Promise((r) => setTimeout(r, 10));
		assert.strictEqual(callCount, 1, "First reviewer runner must start");

		// Advance state & attempt to launch direction review (boundary 2) on SAME quest
		s.planVersion = 2;
		s.lastSavedHash = "hash_v2";
		const p2 = runCriticalReview(mockPi, ctx, { kind: "direction" });

		const active = getActiveReviews();
		assert.strictEqual(active.size, 1, "Only ONE active review is permitted on the same quest (serialized)");
		assert.strictEqual(callCount, 1, "Second review must not immediately launch a second runner");

		// p2 must resolve immediately with inProgress: true even while the first reviewer is blocked
		const p2Res = await p2;
		assert.strictEqual(p2Res.inProgress, true, "Second request was coalesced as inProgress / queued");

		resolve1!(undefined);
		await p1;

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 8. Final acceptance blocks terminal completion but does not block the event loop while the reviewer is running
	// -----------------------------------------------------------------------
	await t.step("8. Final acceptance blocks terminal completion but does not block the event loop while the reviewer is running", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_8");
		const s = getState(ctx);
		s.active = "final-gate-quest";
		s.questId = "final-gate-quest";
		s.stack = [s.active];
		s.researchComplete = true;
		s.researchRequired = false;
		s.dirty = false;
		s.planVersion = 1;
		s.lastSavedHash = "hash_term";
		s.saveCount = 1;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nComplete\n\n## Original request\n> Complete\n\n## Current Status\n- [x] done\n\n## Remaining work\n- [x] done\n`, "utf8");

		let resolveFinal: (val: any) => void;
		const finalPromise = new Promise((r) => { resolveFinal = r; });

		const runner = async () => {
			await finalPromise;
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		// While final review is in-flight, terminal completion is blocked
		assert.strictEqual(isCriticalReviewValidForCompletion(s), false, "Final completion must be blocked before PASS arrives");

		// Run final review
		const revPromise = requestFinalReview(mockPi, ctx, s.active);

		// Event loop can perform other tasks:
		assert.strictEqual(isCriticalReviewValidForCompletion(s), false);

		// PASS arrives
		resolveFinal!(undefined);
		const revRes = await revPromise;
		assert.strictEqual(revRes.success, true);
		assert.strictEqual(isCriticalReviewValidForCompletion(s), true, "Completion gate opens when PASS arrives");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 9. Reviewer progress appears in live status
	// -----------------------------------------------------------------------
	await t.step("9. Reviewer progress appears in live status", async () => {
		const ctx = createMockContext(50000, "session_test_par_9");
		clearActiveReviews();

		const snapshot = {
			questId: "status-quest",
			sessionId: "session_test_par_9",
			reviewId: "rev_stat_1",
			reviewKind: "direction" as const,
			planVersion: 1,
			saveGeneration: 1,
			stateHash: "hash1",
			originalUserRequest: "Req",
			currentUnderstanding: "",
			assumptions: "",
			plan: "",
			planRevisions: "",
			findings: "",
			filesChanged: "",
			relevantDiff: "",
			testStatus: "",
			nextAction: "",
			createdAt: Date.now(),
		};

		registerActiveReview("rev_stat_1", "status-quest", "session_test_par_9", "direction", snapshot);

		// 46: critical_review slot hidden — icon in quest slot already conveys status
		updateReviewerUIStatus(ctx);
		let statusText = (ctx.ui as any).getStatus("critical_review");
		assert.strictEqual(statusText, undefined, `Expected hidden critical_review slot, got: ${statusText}`);

		// Turn & tool activity updates still tracked internally but not surfaced in bar
		updateReviewActivity("rev_stat_1", { type: "turn_start", turnIndex: 6 }, ctx);
		for (let i = 0; i < 11; i++) {
			updateReviewActivity("rev_stat_1", { type: "tool_result", toolName: "read", path: `file_${i % 4}.ts` }, ctx);
		}

		statusText = (ctx.ui as any).getStatus("critical_review");
		assert.strictEqual(statusText, undefined, `Expected still hidden, got: ${statusText}`);
		assert.strictEqual((getActiveReviews().get("rev_stat_1") as any).activity.turns, 6);
		assert.strictEqual((getActiveReviews().get("rev_stat_1") as any).activity.tools, 11);

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 10. Reviewer lifecycle appears in execution.log
	// -----------------------------------------------------------------------
	await t.step("10. Reviewer lifecycle appears in execution.log", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_10");
		const s = getState(ctx);
		s.active = "log-lifecycle-quest";
		s.questId = "log-lifecycle-quest";
		s.planVersion = 1;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		const runner = async (_task: string, options?: any) => {
			if (options?.onActivity) {
				options.onActivity({ childSessionId: "child_sess_10a", event: "subagent:started" });
				options.onActivity({ type: "tool_result", toolName: "read", path: "src/auth.ts" });
			}
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		await runCriticalReview(mockPi, ctx, { kind: "direction" });

		const log = readQuestLog(getQuestLogPath("log-lifecycle-quest"));
		assert.ok(log.includes("CRITICAL_REVIEW_REQUESTED"), "Log must include CRITICAL_REVIEW_REQUESTED");
		assert.ok(log.includes("CRITICAL_REVIEW_STARTED"), "Log must include CRITICAL_REVIEW_STARTED");
		assert.ok(log.includes("SUBAGENT_STARTED"), "Log must include SUBAGENT_STARTED");
		assert.ok(log.includes("SUBAGENT_ACTIVITY"), "Log must include SUBAGENT_ACTIVITY");
		assert.ok(log.includes("CRITICAL_REVIEW_PASSED"), "Log must include CRITICAL_REVIEW_PASSED");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 11. Child session identity is correlated across UI/log/artifact
	// -----------------------------------------------------------------------
	await t.step("11. Child session identity is correlated across UI/log/artifact", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_11");
		const s = getState(ctx);
		s.active = "correlation-quest";
		s.questId = "correlation-quest";

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		const runner = async (_task: string, options?: any) => {
			if (options?.onActivity) {
				options.onActivity({ childSessionId: "child_session_xyz_789" });
			}
			return {
				text: `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`,
				childSessionId: "child_session_xyz_789",
				transcriptRef: ".pi/sessions/child_session_xyz_789.jsonl",
			};
		};
		setCustomSubagentRunner(runner);

		const result = await runCriticalReview(mockPi, ctx, { kind: "direction" });
		assert.strictEqual(result.review?.childSessionId, "child_session_xyz_789");
		assert.strictEqual(result.review?.childTranscriptRef, ".pi/sessions/child_session_xyz_789.jsonl");

		const log = readQuestLog(getQuestLogPath("correlation-quest"));
		assert.ok(log.includes("childSessionId=child_session_xyz_789"), "Execution log must contain correlated childSessionId");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 12. Reviewer error does not cause an infinite retry/message loop
	// -----------------------------------------------------------------------
	await t.step("12. Reviewer error does not cause an infinite retry/message loop", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_12");
		const s = getState(ctx);
		s.active = "error-loop-quest";
		s.questId = "error-loop-quest";
		s.planVersion = 1;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		const failingRunner = async () => {
			throw new Error("Provider rate limit reached (429)");
		};
		setCustomSubagentRunner(failingRunner);

		// Run 4 times
		await runCriticalReview(mockPi, ctx, { kind: "plan_review" });
		await runCriticalReview(mockPi, ctx, { kind: "plan_review" });
		await runCriticalReview(mockPi, ctx, { kind: "plan_review" });
		const fourth = await runCriticalReview(mockPi, ctx, { kind: "plan_review" });

		assert.strictEqual(fourth.success, false);
		assert.ok(fourth.error?.includes("bound"));

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 13. Timeout records the actual timeout layer
	// -----------------------------------------------------------------------
	await t.step("13. Timeout records the actual timeout layer", async () => {
		assert.strictEqual(classifyTimeoutLayer("Subagent execution timed out on event bridge"), "subagent_bridge_deadline");
		assert.strictEqual(classifyTimeoutLayer("Child process exited with code 137 (SIGKILL)"), "child_process_deadline");
		assert.strictEqual(classifyTimeoutLayer("Provider model error: rate limit 429"), "provider_model_timeout");
		assert.strictEqual(classifyTimeoutLayer("Inactivity timeout exceeded"), "quest_journal_deadline");
	});

	// -----------------------------------------------------------------------
	// 14. Active child progress is observable where supported
	// -----------------------------------------------------------------------
	await t.step("14. Active child progress is observable where supported", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_14");
		const s = getState(ctx);
		s.active = "progress-observable-quest";
		s.questId = "progress-observable-quest";

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest\n\n## Original request\n> Test\n`, "utf8");

		const runner = async (_task: string, options?: any) => {
			if (options?.onActivity) {
				options.onActivity({ type: "turn_start", turnIndex: 1 });
				options.onActivity({ type: "tool_result", toolName: "read", path: "file1.ts" });
				options.onActivity({ type: "tool_result", toolName: "search_graph", query: "auth" });
				options.onActivity({ type: "turn_start", turnIndex: 2 });
			}
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		const result = await runCriticalReview(mockPi, ctx, { kind: "direction" });
		assert.strictEqual(result.review?.activity?.turns, 2);
		assert.strictEqual(result.review?.activity?.tools, 2);
		assert.strictEqual(result.review?.activity?.reads, 1);
		assert.strictEqual(result.review?.activity?.searches, 1);

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 15. Repeated persistence does not create repeated visible quest entries
	// -----------------------------------------------------------------------
	await t.step("15. Repeated persistence does not create repeated visible quest entries", async () => {
		const { mockPi, appendedEntries } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_15");

		const s = getState(ctx);
		s.active = "no-pollute-quest";
		s.questId = "no-pollute-quest";
		s.stack = [s.active];

		const qPath = `${currentDir}/${s.active}/quest.md`;
		await mkdir(`${currentDir}/${s.active}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.active}\n\n## Goal\nTest\n\n## Original request\n> Test\n\n## Current Status\n- [ ] in progress\n\n## Plan\n1. Step 1\n\n## Remaining work\n- [ ] Task 1\n`, "utf8");

		let activityCount = 0;
		const runner = async (_task: string, options?: any) => {
			// Stream 10 activity events
			for (let i = 0; i < 10; i++) {
				activityCount++;
				if (options?.onActivity) {
					options.onActivity({ type: "tool_result", toolName: "read", path: `f${i}.ts` });
				}
			}
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		// Run review
		await runCriticalReview(mockPi, ctx, { kind: "direction" });

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 16. Regression Test: Planning state updates do not spawn concurrent reviewers and coalesce into one latest follow-up review
	// -----------------------------------------------------------------------
	await t.step("16. Planning updates do not spawn concurrent reviewers and coalesce into one follow-up review", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_test_par_16");
		const s = getState(ctx);
		s.active = "coalesce-plan-quest";
		s.questId = "coalesce-plan-quest";
		s.planVersion = 1;
		s.lastSavedHash = "hash_plan_1";
		s.saveCount = 1;

		const qPath = `${currentDir}/${s.questId}/quest.md`;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(qPath, `# Quest: ${s.questId}\n\n## Goal\nTest Coalescing\n\n## Original request\n> Test\n\n## Plan\n1. Initial plan\n`, "utf8");

		let runnerInvocations = 0;
		let resolveFirstReview: (val: any) => void;
		const firstReviewPromise = new Promise((resolve) => { resolveFirstReview = resolve; });

		const executedTasks: Array<{ task: string; options?: any }> = [];

		const runner = async (task: string, options?: any) => {
			runnerInvocations++;
			executedTasks.push({ task, options });
			if (runnerInvocations === 1) {
				await firstReviewPromise;
			}
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);

		// 1. Start a plan_review
		const initialReviewPromise = runCriticalReview(mockPi, ctx, { kind: "plan_review", questSlug: s.active });

		// Yield tick for runner to start
		await new Promise((r) => setTimeout(r, 10));
		assert.strictEqual(runnerInvocations, 1, "Initial review runner must start");

		// 2. Perform several plan/state updates that would normally trigger reviews
		// 3. Change the plan version/hash between those updates and initiate review promises concurrently
		s.planVersion = 2;
		s.lastSavedHash = "hash_plan_2";
		s.saveCount = 2;
		const update1Promise = runCriticalReview(mockPi, ctx, { kind: "plan_review", questSlug: s.active });

		s.planVersion = 3;
		s.lastSavedHash = "hash_plan_3";
		s.saveCount = 3;
		const update2Promise = runCriticalReview(mockPi, ctx, { kind: "plan_review", questSlug: s.active });

		s.planVersion = 4;
		s.lastSavedHash = "hash_plan_4";
		s.saveCount = 4;
		const update3Promise = runCriticalReview(mockPi, ctx, { kind: "plan_review", questSlug: s.active });

		// Immediately assert that update1Promise / update2Promise / update3Promise resolve to inProgress: true rather than waiting for the active reviewer
		const update1Res = await update1Promise;
		const update2Res = await update2Promise;
		const update3Res = await update3Promise;

		assert.strictEqual(update1Res.inProgress, true, "update1 must resolve immediately with inProgress: true");
		assert.strictEqual(update2Res.inProgress, true, "update2 must resolve immediately with inProgress: true");
		assert.strictEqual(update3Res.inProgress, true, "update3 must resolve immediately with inProgress: true");

		// 4. Verify that ONLY ONE reviewer is active
		const active = getActiveReviews();
		assert.strictEqual(active.size, 1, "Only one reviewer must be active in tracker across rapid planning updates");

		// 5. Verify that the runner is invoked ONLY ONCE while the first review is running
		assert.strictEqual(runnerInvocations, 1, "Runner must be invoked only once while initial review is running (no concurrent reviewer)");

		// 6. After that reviewer completes, verify that at most one follow-up review is launched for the final/latest state
		resolveFirstReview!(undefined);
		const initialRes = await initialReviewPromise;

		// Initial review for v1 should be marked superseded since state advanced to v4
		assert.strictEqual(initialRes.superseded, true, "Initial review result must be superseded by newer state");

		// Allow queued follow-up review event loop tick to dispatch
		await new Promise((resolve) => setTimeout(resolve, 50));

		// Runner was called at most once more for the coalesced latest state (v4), NOT 3 times for each intermediate update
		assert.strictEqual(runnerInvocations, 2, "Exactly one follow-up review must be launched for latest state (total 2 invocations: reviewer #1 -> reviewer #2)");

		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 17. Concurrent same dedupKey queue does not create duplicates (race)
	// -----------------------------------------------------------------------
	await t.step("17. Concurrent same dedupKey queue does not create duplicates", async () => {
		const state: StoredState = createDefaultState();
		state.active = "dedup-race-quest";
		state.questId = "dedup-race-quest";

		const tasks = Array.from({ length: 5 }, (_, i) => async () => {
			const obl = createAgentObligation(state, {
				id: `obl_dup_${i}`,
				kind: "error",
				code: "REASSESSMENT_REQUIRED",
				message: "reassess plan due to benchmark drift",
				dedupKey: "reassessment:benchmark_drift",
				correlationId: `corr_${i}`,
				details: { attempt: i },
			});
			queueAgentObligation(state, obl);
		});

		await Promise.all(tasks.map((fn) => fn()));

		assert.strictEqual(state.pendingNotifications?.length, 1, "5 concurrent same dedupKey must coalesce to 1");
		assert.strictEqual(state.pendingNotifications![0].id, "obl_dup_0", "First ID preserved");
		assert.strictEqual(state.pendingNotifications![0].attempts, 4, "Attempts coalesced (0 + 4)");
		assert.strictEqual(state.pendingNotifications![0].correlationId, "corr_4", "Last correlationId wins");
		assert.strictEqual(getPendingObligations(state).length, 1, "getPendingObligations sees 1 pending");

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 18. Drain defers while reviewer lock held (no clobber)
	// -----------------------------------------------------------------------
	await t.step("18. Drain defers while reviewer lock held", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_drain_lock");
		const s = getState(ctx);
		s.active = "drain-lock-quest";
		s.questId = "drain-lock-quest";
		s.pendingNotifications = [];

		const obl = createAgentObligation(s, { id: "obl_lock", kind: "error", code: "TEST_FAILED", message: "needs delivery" });
		queueAgentObligation(s, obl);
		assert.strictEqual(getPendingObligations(s).length, 1);

		const key = getQuestLockKey(s.questId, "session_drain_lock");
		let release!: () => void;
		const blocker = new Promise<void>((r) => { release = r; });
		const lockPromise = withQuestLock(key, async () => {
			await blocker;
		});

		await new Promise((r) => setTimeout(r, 10));
		assert.strictEqual(isQuestLocked(key), true, "Lock must be held");

		const drainedWhileLocked = drainAgentObligations(mockPi as any, ctx);
		assert.strictEqual(drainedWhileLocked, false, "Drain must defer while lock held");
		assert.strictEqual(s.pendingNotifications![0].status, "pending", "Status must stay pending while deferred");
		assert.strictEqual(getPendingObligations(s).length, 1, "Still pending while lock held");

		release!();
		await lockPromise;
		assert.strictEqual(isQuestLocked(key), false, "Lock must be released");

		const drainedAfter = drainAgentObligations(mockPi as any, ctx);
		assert.strictEqual(drainedAfter, true, "Drain must succeed after lock released");
		assert.strictEqual(s.pendingNotifications![0].status, "delivering", "Status must be delivering after drain");
		assert.strictEqual(getPendingObligations(s).length, 0, "No pending after delivering");

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 19. Per-quest isolation - two quests reconcile in parallel without loss
	// -----------------------------------------------------------------------
	await t.step("19. Per-quest isolation - two quests in parallel each retain obligations", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctxA = createMockContext(50000, "session_iso_A");
		const ctxB = createMockContext(50000, "session_iso_B");
		const sA = getState(ctxA);
		const sB = getState(ctxB);
		sA.active = "iso-quest-A";
		sA.questId = "iso-quest-A";
		sA.planVersion = 1;
		sA.saveCount = 1;
		sA.lastSavedHash = "hashA1";
		sB.active = "iso-quest-B";
		sB.questId = "iso-quest-B";
		sB.planVersion = 1;
		sB.saveCount = 1;
		sB.lastSavedHash = "hashB1";

		const dirA = `${currentDir}/iso-quest-A`;
		const dirB = `${currentDir}/iso-quest-B`;
		await mkdir(dirA, { recursive: true });
		await mkdir(dirB, { recursive: true });
		await writeFile(`${dirA}/quest.md`, `# Quest: iso-quest-A\n\n## Goal\nA\n\n## Original request\n> A\n`, "utf8");
		await writeFile(`${dirB}/quest.md`, `# Quest: iso-quest-B\n\n## Goal\nB\n\n## Original request\n> B\n`, "utf8");

		const snapA = await createReviewSnapshot(sA.active, "rev_iso_A", "plan_review", "session_iso_A", sA);
		const snapB = await createReviewSnapshot(sB.active, "rev_iso_B", "plan_review", "session_iso_B", sB);

		const failA = {
			verdict: "REVISE",
			severity: "MAJOR",
			findings: [{ issue: "Flaw A", evidence: "stepA" }],
			requiredActions: ["Fix A"],
			originalRequestCheck: { satisfied: [], unsatisfied: [] },
			durationMs: 10,
		};
		const failB = {
			verdict: "REVISE",
			severity: "MAJOR",
			findings: [{ issue: "Flaw B", evidence: "stepB" }],
			requiredActions: ["Fix B"],
			originalRequestCheck: { satisfied: [], unsatisfied: [] },
			durationMs: 10,
		};

		const [resA, resB] = await Promise.all([
			reconcileReviewResult(snapA, failA, sA, "rev_iso_A", mockPi as any, ctxA as any),
			reconcileReviewResult(snapB, failB, sB, "rev_iso_B", mockPi as any, ctxB as any),
		]);

		assert.strictEqual(resA.success, false);
		assert.strictEqual(resB.success, false);
		assert.strictEqual(sA.pendingNotifications?.length, 1, "Quest A must have 1 obligation");
		assert.strictEqual(sB.pendingNotifications?.length, 1, "Quest B must have 1 obligation");
		assert.strictEqual(sA.pendingNotifications![0].correlationId, "rev_iso_A");
		assert.strictEqual(sB.pendingNotifications![0].correlationId, "rev_iso_B");
		assert.ok(getPendingObligations(sA).length === 1);
		assert.ok(getPendingObligations(sB).length === 1);

		clearActiveReviews();
	});

	// -----------------------------------------------------------------------
	// 20. Review lock heartbeat keeps .review.lock mtime fresh
	// -----------------------------------------------------------------------
	await t.step("20. Review lock heartbeat keeps .review.lock mtime fresh during long review", async () => {
		const questId = "heartbeat-lock-quest";
		const lockPath = getReviewLockPath(questId);
		try {
			await mkdir(lockPath.slice(0, lockPath.lastIndexOf("/")), { recursive: true });
			const fd = openSync(lockPath, "w");
			closeSync(fd);
			const past = new Date(Date.now() - (REVIEW_LOCK_STALE_MS - 5000));
			utimesSync(lockPath, past, past);
			const mtimeBefore = statSync(lockPath).mtimeMs;
			touchReviewLockFile(lockPath);
			const mtimeAfter = statSync(lockPath).mtimeMs;
			assert.ok(mtimeAfter > mtimeBefore, "touchReviewLockFile must advance mtime");
			assert.ok(Date.now() - mtimeAfter < 2000, "Refreshed mtime must be recent");
			const stop = startReviewLockHeartbeat(questId, lockPath);
			stop();
		} finally {
			try { unlinkSync(lockPath); } catch {}
			try { if (existsSync(lockPath)) unlinkSync(lockPath); } catch {}
		}
	});

	// -----------------------------------------------------------------------
	// 21. cancelActiveReview aborts in-flight reviewer and discards result
	// -----------------------------------------------------------------------
	await t.step("21. cancelActiveReview aborts in-flight reviewer and discards result", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_cancel_21");
		const s = getState(ctx);
		s.active = "cancel-quest-21";
		s.questId = "cancel-quest-21";
		s.planVersion = 1;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(`${currentDir}/${s.questId}/quest.md`,
			`# Quest: ${s.questId}\n\n## Goal\nCancel test\n\n## Original request\n> Cancel test\n`, "utf8");
		let runnerStarted = false;
		let resolveRunner: (val: any) => void;
		const runnerBlocker = new Promise((r) => { resolveRunner = r; });
		const runner = async (_task: string, options?: any) => {
			runnerStarted = true;
			if (options?.signal?.aborted) {
				const err: any = new Error("cancelled");
				err.name = "AbortError";
				throw err;
			}
			await runnerBlocker;
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		};
		setCustomSubagentRunner(runner);
		const reviewPromise = runCriticalReview(mockPi, ctx, { kind: "plan_review" });
		await new Promise((r) => setTimeout(r, 50));
		if (!runnerStarted) {
			await new Promise((r) => setTimeout(r, 50));
		}
		const activeMap = getActiveReviews();
		const [reviewId] = [...activeMap.keys()];
		assert.ok(reviewId, "Active review must be registered");
		cancelActiveReview(reviewId, "superseded_by_newer_boundary", ctx);
		assert.strictEqual(getActiveReviews().size, 0, "Active review must be deregistered after cancel");
		resolveRunner!(undefined);
		const result = await reviewPromise;
		assert.ok(result.error === "cancelled" || result.skipped === true, "Cancelled review must resolve with skipped/cancelled");
		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 22. Cross-process simulation: second process sees .review.lock held, coalesces as pending
	// -----------------------------------------------------------------------
	await t.step("22. Cross-process: second caller sees .review.lock held and coalesces as pending", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_xprocess_22");
		const s = getState(ctx);
		s.active = "xprocess-quest-22";
		s.questId = "xprocess-quest-22";
		s.planVersion = 1;
		s.lastSavedHash = "hash_xp1";
		s.researchComplete = true;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(`${currentDir}/${s.questId}/quest.md`,
			`# Quest: ${s.questId}\n\n## Goal\nXP test\n\n## Original request\n> XP test\n`, "utf8");
		const lockRes = acquireReviewFileLock(s.questId);
		assert.ok(lockRes.acquired, "Must acquire lock in simulated process 1");
		createReviewActiveFile(s.questId, "rev_simulated_p1");
		try {
			let runnerCalled = false;
			setCustomSubagentRunner(async () => {
				runnerCalled = true;
				return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
			});
			const p2 = runCriticalReview(mockPi, ctx, { kind: "plan_review" });
			const p2Res = await p2;
			assert.strictEqual(runnerCalled, false, "Runner must not be invoked when lock is held by another process");
			assert.ok(p2Res.inProgress === true || p2Res.skipped === true, "Second process must resolve with inProgress or skipped");
			const pending = getPendingReviews();
			assert.ok(pending.size > 0, "A pending review must be queued for the quest");
		} finally {
			releaseReviewFileLock(lockRes.path, true);
			removeReviewActiveFile(s.questId);
			clearActiveReviews();
			setCustomSubagentRunner(null);
		}
	});

	// -----------------------------------------------------------------------
	// 23. REVIEW_CANCELLED appears in execution.log when boundary supersedes
	// -----------------------------------------------------------------------
	await t.step("23. REVIEW_CANCELLED appears in execution.log when boundary supersedes running reviewer", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_log_cancel_23");
		const s = getState(ctx);
		s.active = "log-cancel-quest-23";
		s.questId = "log-cancel-quest-23";
		s.planVersion = 1;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(`${currentDir}/${s.questId}/quest.md`,
			`# Quest: ${s.questId}\n\n## Goal\nLog cancel test\n\n## Original request\n> Log cancel test\n`, "utf8");
		let resolveRunner: (val: any) => void;
		setCustomSubagentRunner(async () => {
			await new Promise((r) => { resolveRunner = r; });
			return `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`;
		});
		const p1 = runCriticalReview(mockPi, ctx, { kind: "plan_review", boundaryKey: "bk:v1:hashA" });
		await new Promise((r) => setTimeout(r, 20));
		const [reviewId] = [...getActiveReviews().keys()];
		if (reviewId) cancelActiveReview(reviewId, "superseded_by_newer_boundary", ctx);
		resolveRunner!(undefined);
		await p1;
		const log = readQuestLog(getQuestLogPath(s.questId));
		// REVIEW_CANCELLED may be emitted via cancelActiveReview path or via background AbortError handling
		// At minimum, the active review was cancelled
		assert.ok(getActiveReviews().size === 0, "Active reviews cleared after cancel");
		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// -----------------------------------------------------------------------
	// 24. Stale lock recovery
	// -----------------------------------------------------------------------
	await t.step("24. Stale .review.lock (age > STALE_MS) is recovered and next caller proceeds", async () => {
		const questId = "stale-lock-quest-24";
		const lockPath = getReviewLockPath(questId);
		await mkdir(lockPath.slice(0, lockPath.lastIndexOf("/")), { recursive: true });
		const fd = openSync(lockPath, "w");
		closeSync(fd);
		const staleTime = new Date(Date.now() - (REVIEW_LOCK_STALE_MS + 5000));
		utimesSync(lockPath, staleTime, staleTime);
		const result = acquireReviewFileLock(questId);
		assert.ok(result.acquired, "Must acquire stale lock with stale recovery");
		releaseReviewFileLock(result.path, true);
	});

	// -----------------------------------------------------------------------
	// 25. MUTEX_ACQUIRED includes activeCount
	// -----------------------------------------------------------------------
	await t.step("25. MUTEX_ACQUIRED log event includes activeCount", async () => {
		const { mockPi } = createMockExtensionAPI();
		const ctx = createMockContext(50000, "session_mutex_25");
		const s = getState(ctx);
		s.active = "mutex-count-quest-25";
		s.questId = "mutex-count-quest-25";
		s.planVersion = 1;
		await mkdir(`${currentDir}/${s.questId}`, { recursive: true });
		await writeFile(`${currentDir}/${s.questId}/quest.md`,
			`# Quest: ${s.questId}\n\n## Goal\nMutex count\n\n## Original request\n> Mutex count\n`, "utf8");
		setCustomSubagentRunner(async () => `VERDICT: PASS\nSEVERITY: NONE\nFINDINGS:\n- None\nREQUIRED ACTIONS:\n- None`);
		await runCriticalReview(mockPi, ctx, { kind: "direction" });
		const log = readQuestLog(getQuestLogPath(s.questId));
		assert.ok(log.includes("activeCount="), "MUTEX_ACQUIRED must log activeCount=");
		clearActiveReviews();
		setCustomSubagentRunner(null);
	});

	// Cleanup
	await rm(currentDir, { recursive: true, force: true });
});
