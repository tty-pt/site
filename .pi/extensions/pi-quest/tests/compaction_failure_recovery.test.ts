import {
	assert,
	canImplement,
	createMockContext,
	createMockExtensionAPI,
	getAllMessages,
	getImplementationBlockReason,
	getState,
	mkdir,
	plugin,
	QuestErrorCode,
	recordObservedInvestigation,
	rm,
	writeFile,
} from "./compaction_test_helpers.ts";
import { questPath } from "../src/paths.ts";

Deno.test("quest_journal_compaction_failure_recovery: compaction failures, fallback directives, error gating, and explicit reconciliation", async (t) => {
	const QUEST_DIR = ".pi/quest/current";
	await mkdir(QUEST_DIR, { recursive: true });

	// -----------------------------------------------------------------------
	// 7. Compaction failure: reports COMPACTION_FAILURE and delivers fallback message, not success
	// -----------------------------------------------------------------------
	await t.step("7. Compaction failure: reports COMPACTION_FAILURE and delivers fallback message, not success", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_failure_handling");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-compaction-fail-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);
		// Prepare transaction
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		assert.ok(state.activeTransaction);
		assert.strictEqual(state.activeTransaction.phase, "in-flight");
		const compactCountBefore = state.compactCount;

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// Fire session_compact_failed
		for (const cb of api.handlers["session_compact_failed"] || []) {
			await cb({ error: new Error("Simulated LLM token budget compaction abort") }, ctx);
		}

		// ASSERTIONS:
		// 1. Transaction resolved to resume-delivered on successful fallback delivery
		assert.strictEqual(state.activeTransaction?.phase, "resume-delivered", "Transaction must resolve to resume-delivered after successful fallback delivery");
		assert.strictEqual(state.compactionPending, false);
		assert.strictEqual(state.compactCount, compactCountBefore, "compactCount must not advance on failure");

		// 2. Model-visible COMPACTION_FAILURE error is reported
		const msgs = getAllMessages(api);
		assert.ok(msgs.some((m) => m.includes("COMPACTION_FAILURE")), "COMPACTION_FAILURE must be reported");

		// 3. Fallback message (not post-compaction success message) is delivered
		assert.strictEqual(
			msgs.filter((m) => m.includes("Post-Compaction Autonomous Resumption Directive")).length,
			0,
			"Must NOT emit Post-Compaction success directive",
		);
		assert.ok(
			msgs.some((m) => m.includes("Compaction Skipped / Fallback")),
			"Must emit fallback continuation directive",
		);

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 11. Subquest launch compaction failure fallback preserves pendingSubquestResume on transport error
	// -----------------------------------------------------------------------
	await t.step("11. Subquest launch compaction failure fallback preserves pendingSubquestResume on transport error", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_subquest_fallback_error");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const parentSlug = "test-fallback-parent";
		const childSlug = "test-fallback-child";

		await commands["quest"].handler(parentSlug, ctx);
		const parentPath = questPath(getState(ctx as any).questId);
		await tools["quest_subquest"].execute("call_sub", { name: childSlug, switchNow: true }, {}, () => {}, ctx);
		const childPath = questPath(getState(ctx as any).questId);

		const state = getState(ctx as any);
		state.pendingSubquestResume = childSlug;

		// Disable transport to simulate delivery error on fallback
		api.mockPi.setThrowOnSend(true);
		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// Fire session_compact_failed
		for (const cb of api.handlers["session_compact_failed"] || []) {
			await cb({ error: new Error("Simulated compaction failure") }, ctx);
		}

		// ASSERTIONS:
		// 1. pendingSubquestResume is preserved on delivery failure
		assert.strictEqual(state.pendingSubquestResume, childSlug, "pendingSubquestResume must be preserved when delivery fails");
		assert.ok(state.pendingResume, "pendingResume must be recorded for retry");
		assert.strictEqual(state.pendingResume.reason, "compaction-failure-fallback");

		// 2. Error notification accurately states "Compaction failed", not "Compaction completed successfully"
		assert.ok(
			state.pendingNotifications?.some((n) => n.message.includes("Compaction failed, and the autonomous fallback resume directive could not be delivered.")),
			"Buffered pending notification must accurately say 'Compaction failed'",
		);
		assert.ok(
			!state.pendingNotifications?.some((n) => n.message.includes("Compaction completed successfully")),
			"Must NOT claim compaction completed successfully on failure",
		);

		// Restore transport
		api.mockPi.setThrowOnSend(false);
		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["turn_end"] || []) {
			await cb({ toolResults: [] }, ctx);
		}

		// ASSERTIONS:
		// 3. Fallback directive is delivered and pendingSubquestResume is cleared
		assert.strictEqual(state.pendingSubquestResume, null, "pendingSubquestResume must be consumed after successful delivery");
		assert.strictEqual(state.pendingResume, null, "pendingResume must be cleared after successful delivery");
		const msgs = getAllMessages(api);
		assert.ok(
			msgs.some((m) => m.includes("Compaction Skipped / Fallback")),
			"Fallback directive must be delivered on retry",
		);
		assert.ok(
			msgs.some((m) => m.includes("Compaction failed, and the autonomous fallback resume directive could not be delivered.")),
			"Buffered error notification must be drained and delivered",
		);

		await rm(parentPath, { force: true });
		await rm(childPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// 12. Failed compaction transaction blocks implementation until explicit recovery transition
	// -----------------------------------------------------------------------
	await t.step("12. Failed compaction transaction blocks implementation until explicit recovery transition", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_failed_tx_blocks_implementation");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-failed-tx-gate-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);
		state.researchComplete = true;
		state.researchRequired = false;
		state.reassessmentRequired = false;
		state.awaitingUserConfirmation = false;

		// Verify implementation is allowed when clean
		assert.strictEqual(canImplement(state, ctx as any), true, "Implementation must be allowed when clean");

		// Prepare transaction and fail it (with transport outage so fallback is not immediately delivered)
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		assert.ok(state.activeTransaction);

		api.mockPi.setThrowOnSend(true);
		for (const cb of api.handlers["session_compact_failed"] || []) {
			await cb({ error: new Error("Compaction worker timeout") }, ctx);
		}

		assert.strictEqual(state.activeTransaction?.phase, "failed", "Transaction must be failed");

		// ASSERTION 1: canImplement() is false on failed transaction
		assert.strictEqual(canImplement(state, ctx as any), false, "canImplement() must return false when activeTransaction is failed");

		const reason = getImplementationBlockReason(state, ctx as any);
		assert.strictEqual(reason.blocked, true);
		assert.strictEqual(reason.code, QuestErrorCode.COMPACTION_FAILURE);
		assert.strictEqual(reason.stateName, "COMPACTION_TRANSACTION_FAILED");

		// Tool gating blocks mutating tool
		const editTool = tools["edit"] || api.registeredTools.find((t: any) => t.name === "edit");
		if (editTool) {
			const res = await editTool.execute("call_edit_blocked", { path: "src/file.ts", edits: [] }, {}, () => {}, ctx);
			assert.ok(res.error, "Mutating tool must be blocked when compaction transaction failed");
			assert.ok(res.error.includes("COMPACTION_TRANSACTION_FAILED") || res.error.includes("Compaction worker timeout"));
		}

		// ASSERTION 2: quest_mark_saved does NOT clear failed transaction; explicit reassessment resolves it
		await tools["quest_mark_saved"].execute("call_save_recovery", { name: slug }, {}, () => {}, ctx);
		assert.ok(state.activeTransaction, "activeTransaction must remain after quest_mark_saved");
		assert.strictEqual(state.activeTransaction.phase, "failed");
		assert.strictEqual(canImplement(state, ctx as any), false, "Gate must remain blocked after save");

		recordObservedInvestigation(state, "read", { path: p }, "content", false);
		await tools["quest_update_state"].execute(
			"call_resolve_step_12",
			{
				name: slug,
				goal: "Test failed tx gate",
				understanding: "State recovered after failure",
				assumptions: ["Assumptions verified"],
				openQuestions: ["None"],
				findings: ["Recovered"],
				plan: ["Proceed"],
				planConfidence: "high",
				exactNextAction: "Continue",
				reassessmentComplete: true,
				reassessmentConclusion: "Investigated failure and reconciled durable state.",
			},
			{},
			() => {},
			ctx,
		);
		assert.strictEqual(state.activeTransaction, null, "activeTransaction cleared after explicit reassessment");
		assert.strictEqual(canImplement(state, ctx as any), true, "Gate must open after explicit reconciliation");

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 13. Failed compaction transaction persists across unrelated successful tool results
	// -----------------------------------------------------------------------
	await t.step("13. Failed compaction transaction persists across unrelated successful tool results", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_failed_tx_tool_result_persistence");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-failed-tx-persistence-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		api.mockPi.setThrowOnSend(true);
		for (const cb of api.handlers["session_compact_failed"] || []) {
			await cb({ error: new Error("Out of memory during compaction") }, ctx);
		}

		assert.strictEqual(state.activeTransaction?.phase, "failed");

		// Simulate unrelated successful tool result (e.g. read or bash)
		for (const cb of api.handlers["tool_result"] || []) {
			await cb({ toolName: "read", input: { path: "README.md" }, content: "file content", isError: false }, ctx);
		}

		// ASSERTION: Transaction must NOT be cleared by successful tool_result
		assert.ok(state.activeTransaction, "activeTransaction must not be null after successful tool_result");
		assert.strictEqual(state.activeTransaction?.phase, "failed", "activeTransaction must remain in phase 'failed'");
		assert.strictEqual(canImplement(state, ctx as any), false, "Implementation must remain blocked");

		// Simulate another tool result (e.g. bash pwd)
		for (const cb of api.handlers["tool_result"] || []) {
			await cb({ toolName: "bash", input: { command: "pwd" }, content: "/home/user", isError: false }, ctx);
		}

		assert.ok(state.activeTransaction);
		assert.strictEqual(state.activeTransaction?.phase, "failed");

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 15. Fallback resume success explicitly resolves failed transaction and reopens implementation gate
	// -----------------------------------------------------------------------
	await t.step("15. Fallback resume success explicitly resolves failed transaction and reopens implementation gate", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_fallback_reopens_gate");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-fallback-reopen-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);
		state.researchComplete = true;
		state.researchRequired = false;
		state.reassessmentRequired = false;
		state.awaitingUserConfirmation = false;

		// 1. Enter compaction and fail it with transport outage
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		assert.ok(state.activeTransaction);

		api.mockPi.setThrowOnSend(true);
		for (const cb of api.handlers["session_compact_failed"] || []) {
			await cb({ error: new Error("Compaction worker crashed") }, ctx);
		}

		// ASSERTION A: When fallback delivery fails, transaction remains "failed" and implementation remains blocked
		assert.strictEqual(state.activeTransaction?.phase, "failed", "Transaction must remain failed when delivery fails");
		assert.strictEqual(canImplement(state, ctx as any), false, "Implementation must be blocked while fallback delivery has not succeeded");
		assert.ok(state.pendingResume, "pendingResume must be recorded for retry");
		assert.strictEqual(state.pendingResume.reason, "compaction-failure-fallback");

		// 2. Restore transport and let fallback resume succeed on turn_end
		api.mockPi.setThrowOnSend(false);
		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		for (const cb of api.handlers["turn_end"] || []) {
			await cb({ toolResults: [] }, ctx);
		}

		// ASSERTION B: When fallback continuation is successfully delivered and state is valid:
		// transaction.phase !== "failed" (transitions to "resume-delivered") and implementation gate reopens
		assert.strictEqual(state.pendingResume, null, "pendingResume must be consumed");
		assert.strictEqual(state.activeTransaction?.phase, "resume-delivered", "activeTransaction must transition to resume-delivered");
		assert.notStrictEqual(state.activeTransaction?.phase, "failed", "activeTransaction.phase must not be 'failed'");
		assert.strictEqual(canImplement(state, ctx as any), true, "Implementation gate must reopen after successful fallback delivery");

		// 3. Verify immediate delivery case on another quest (compaction fails with working transport)
		const slugDirect = "test-fallback-immediate-reopen";
		await commands["quest"].handler(slugDirect, ctx);
		const pDirect = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slugDirect }, {}, () => {}, ctx);

		state.researchComplete = true;
		state.researchRequired = false;
		state.reassessmentRequired = false;
		state.awaitingUserConfirmation = false;

		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		assert.ok(state.activeTransaction);

		// Transport is working; session_compact_failed delivers fallback continuation immediately
		api.agentMessages.length = 0;
		api.userMessages.length = 0;
		for (const cb of api.handlers["session_compact_failed"] || []) {
			await cb({ error: new Error("Immediate failure simulated") }, ctx);
		}

		assert.strictEqual(state.pendingResume, null, "pendingResume must be consumed on immediate delivery");
		assert.strictEqual(state.activeTransaction?.phase, "resume-delivered", "activeTransaction must be resolved to resume-delivered");
		assert.notStrictEqual(state.activeTransaction?.phase, "failed", "activeTransaction.phase must not be 'failed'");
		assert.strictEqual(canImplement(state, ctx as any), true, "Implementation gate must be open immediately after successful delivery");

		await rm(p, { force: true });
		await rm(pDirect, { force: true });
	});
});
