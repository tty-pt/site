import {
	assert,
	canImplement,
	createMockContext,
	createMockExtensionAPI,
	createOrGetCompactionTransaction,
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

Deno.test("quest_journal_compaction_transaction: lifecycle transactions, checkpoints, immutability, and state consistency", async (t) => {
	const QUEST_DIR = ".pi/quest/current";
	await mkdir(QUEST_DIR, { recursive: true });

	// -----------------------------------------------------------------------
	// 1. Checkpoint immutability: checkpoint fields remain immutable when state changes
	// -----------------------------------------------------------------------
	await t.step("1. Checkpoint immutability: checkpoint fields remain immutable when state changes, mismatch is detected", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_immutability");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const questA = "quest-immutability-a";
		const questB = "quest-immutability-b";
		await commands["quest"].handler(questA, ctx);
		const pathA = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save_a", { name: questA }, {}, () => {}, ctx);

		const state = getState(ctx as any);
		const initialSaveCount = state.saveCount;
		const initialHash = state.saveGeneration?.hash || state.lastSavedHash || "";

		// Prepare transaction for Quest A
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		assert.ok(state.activeTransaction, "Transaction must be prepared");
		const preparedTxId = state.activeTransaction.id;
		assert.strictEqual(state.activeTransaction.activeQuest, questA);
		assert.strictEqual(state.activeTransaction.checkpointSaveCount, initialSaveCount);
		assert.strictEqual(state.activeTransaction.checkpointHash, initialHash);

		// Mutate authoritative state before session_compact completes: switch to quest B
		await commands["quest"].handler(questB, ctx);
		const pathB = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save_b", { name: questB }, {}, () => {}, ctx);

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// Fire session_compact
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		// ASSERTIONS:
		// 1. Transaction checkpoint fields MUST remain immutable (quest A, initial save count, initial hash)
		assert.strictEqual(state.activeTransaction?.id, preparedTxId);
		assert.strictEqual(state.activeTransaction?.activeQuest, questA, "Transaction activeQuest must remain immutable A");
		assert.strictEqual(state.activeTransaction?.checkpointSaveCount, initialSaveCount, "Transaction checkpointSaveCount must remain immutable");
		assert.strictEqual(state.activeTransaction?.checkpointHash, initialHash, "Transaction checkpointHash must remain immutable");

		// 2. Transaction is marked inconsistent
		assert.strictEqual(state.activeTransaction?.phase, "inconsistent");

		// 3. Normal resume is NOT emitted
		const msgs = getAllMessages(api);
		assert.strictEqual(
			msgs.filter((m) => m.includes("Post-Compaction Autonomous Resumption Directive")).length,
			0,
			"Normal resume must NOT be emitted on checkpoint mismatch",
		);

		// 4. RESUME_STATE_INCONSISTENT error is reported to model
		assert.ok(
			msgs.some((m) => m.includes("RESUME_STATE_INCONSISTENT")),
			"RESUME_STATE_INCONSISTENT error must be reported",
		);

		// 5. Inconsistent transaction hard-blocks implementation
		assert.strictEqual(canImplement(state, ctx as any), false, "canImplement must return false for inconsistent transaction");
		const blockReason = getImplementationBlockReason(state, ctx as any);
		assert.strictEqual(blockReason.blocked, true);
		assert.strictEqual(blockReason.code, QuestErrorCode.RESUME_STATE_INCONSISTENT);

		// 6. Tool call gate blocks mutating tools
		let blockedEvent: any = null;
		for (const cb of api.handlers["tool_call"] || []) {
			const res = await cb({ toolName: "edit", input: { path: "some/file.ts" } }, ctx);
			if (res?.block) blockedEvent = res;
		}
		assert.ok(blockedEvent?.block, "Tool call gate must block edit while transaction is inconsistent");

		// 7. Calling quest_mark_saved does NOT clear the inconsistent transaction (inconsistent remains blocked until explicit reconciliation)
		await tools["quest_mark_saved"].execute("call_save_reconcile", { name: questB }, {}, () => {}, ctx);
		assert.ok(state.activeTransaction, "activeTransaction must NOT be cleared by quest_mark_saved");
		assert.strictEqual(state.activeTransaction.phase, "inconsistent");
		assert.strictEqual(canImplement(state, ctx as any), false, "Implementation must stay blocked while transaction is inconsistent");

		// Explicit, validated reconciliation operation resolves the transaction
		recordObservedInvestigation(state, "read", { path: pathB }, "content", false);
		await tools["quest_update_state"].execute(
			"call_resolve_step_1",
			{
				name: questB,
				goal: "Quest B",
				understanding: "Reconciled after investigation",
				assumptions: ["Assumptions verified"],
				openQuestions: ["None"],
				findings: ["Resolved"],
				plan: ["Proceed"],
				planConfidence: "high",
				exactNextAction: "Continue",
				reassessmentComplete: true,
				reassessmentConclusion: "Investigated discrepancy and reconciled durable journal.",
			},
			{},
			() => {},
			ctx,
		);
		assert.strictEqual(state.activeTransaction, null, "activeTransaction cleared after explicit reassessment");
		assert.strictEqual(canImplement(state, ctx as any), true, "canImplement must return true once reconciled");

		await rm(pathA, { force: true });
		await rm(pathB, { force: true });
	});

	// -----------------------------------------------------------------------
	// 5. Interrupted transaction reconstruction: in-flight phase preserved and compactionPending synced
	// -----------------------------------------------------------------------
	await t.step("5. Interrupted transaction reconstruction: in-flight phase preserved and compactionPending synced", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_interrupted");
		const state = getState(ctx as any);
		const slug = "interrupted-recon-quest";
		state.active = slug;

		state.activeTransaction = {
			id: "cmp_in_flight_recon_123",
			phase: "in-flight",
			activeQuest: slug,
			questPath: `.pi/quest/current/${slug}.md`,
			reason: "normal-compaction",
			checkpointSaveCount: 4,
			checkpointHash: "hash_recon_test_abc",
			stack: [slug],
			researchRound: 1,
			reassessmentVersion: 0,
			planVersion: 1,
			createdAt: Date.now() - 5000,
		};

		const branchEntry = {
			type: "custom",
			customType: "quest_journal",
			data: state,
		};
		(ctx.sessionManager as any).getBranch = () => [branchEntry];

		// Fire session_start / reconstruction
		for (const cb of api.handlers["session_start"] || []) {
			await cb({}, ctx);
		}

		const recovered = getState(ctx as any);
		assert.ok(recovered.activeTransaction, "activeTransaction must be reconstructed");
		assert.strictEqual(recovered.activeTransaction.phase, "in-flight", "Phase must remain in-flight");
		assert.strictEqual(recovered.activeTransaction.id, "cmp_in_flight_recon_123");
		assert.strictEqual(recovered.compactionPending, true, "compactionPending must be synced to true for in-flight transaction");
	});

	// -----------------------------------------------------------------------
	// 6. Checkpoint mismatch: hash change between preparation and completion triggers RESUME_STATE_INCONSISTENT
	// -----------------------------------------------------------------------
	await t.step("6. Checkpoint mismatch: hash change between preparation and completion triggers RESUME_STATE_INCONSISTENT", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_hash_mismatch");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-hash-mismatch-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);

		// Prepare transaction
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		assert.ok(state.activeTransaction);
		const originalHash = state.activeTransaction.checkpointHash;

		// Mutate hash in live state to simulate unverified edit during compaction
		state.saveGeneration = { count: state.saveCount, path: p, hash: "altered_external_hash_Y", savedAt: Date.now() };
		state.lastSavedHash = "altered_external_hash_Y";

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// Fire session_compact
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		// ASSERTIONS:
		// 1. Transaction checkpoint hash remained X
		assert.strictEqual(state.activeTransaction?.checkpointHash, originalHash);
		assert.strictEqual(state.activeTransaction?.phase, "inconsistent");

		// 2. No ordinary post-compaction resume directive
		const msgs = getAllMessages(api);
		assert.strictEqual(
			msgs.filter((m) => m.includes("Post-Compaction Autonomous Resumption Directive")).length,
			0,
			"No ordinary resume directive may be emitted on checkpoint mismatch",
		);

		// 3. Model receives RESUME_STATE_INCONSISTENT error
		assert.ok(
			msgs.some((m) => m.includes("RESUME_STATE_INCONSISTENT")),
			"RESUME_STATE_INCONSISTENT error must be reported",
		);

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 8. Successful normal lifecycle: prepared -> in-flight -> completed -> resume-pending -> resume-delivered
	// -----------------------------------------------------------------------
	await t.step("8. Successful normal lifecycle: prepared -> in-flight -> completed -> resume-pending -> resume-delivered", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_full_lifecycle");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-full-lifecycle-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);

		// 1. Prepare
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		assert.ok(state.activeTransaction);
		assert.strictEqual(state.activeTransaction.phase, "in-flight");
		const txId = state.activeTransaction.id;

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// 2. session_compact -> completed -> resume delivered -> resume-delivered
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		assert.strictEqual(state.activeTransaction?.phase, "resume-delivered");
		assert.strictEqual(state.lastDeliveredCompactionId, txId);
		assert.strictEqual(state.pendingResume, null);

		const msgs = getAllMessages(api);
		const resumes = msgs.filter((m) => m.includes("Post-Compaction Autonomous Resumption Directive"));
		assert.strictEqual(resumes.length, 1, "Exactly one successful resume directive delivered");

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 9. Duplicate host events: duplicate session_compact calls produce exactly one resume
	// -----------------------------------------------------------------------
	await t.step("9. Duplicate host events: duplicate session_compact calls produce exactly one resume", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_duplicate_events");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-duplicate-events-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save", { name: slug }, {}, () => {}, ctx);

		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// 3 rapid duplicate session_compact events
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
			await cb({}, ctx);
			await cb({}, ctx);
		}

		const resumes = getAllMessages(api).filter((m) => m.includes("Post-Compaction Autonomous Resumption Directive"));
		assert.strictEqual(resumes.length, 1, "Exactly one resume directive delivered across duplicate callbacks");

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 23. Pre-compaction checkpoint verification before successful compaction bookkeeping
	// -----------------------------------------------------------------------
	await t.step("23. Regression Test F & G: Pre-compaction checkpoint verification before successful compaction bookkeeping", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_compaction_bookkeeping_verify");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-bookkeeping-verify-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save_1", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);
		const saveCount1 = state.saveCount;

		// Prepare transaction
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		const tx = state.activeTransaction;
		assert.ok(tx);

		// Case F: Checkpoint mismatch -> dirty flag not cleared, compactCount not advanced
		state.dirty = true;
		state.saveCount = 99; // mutate saveCount so mismatch occurs

		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		assert.strictEqual(tx.phase, "inconsistent", "Transaction must be marked inconsistent");
		assert.strictEqual(state.dirty, true, "dirty flag must NOT be cleared on mismatch");
		assert.notStrictEqual(state.compactCount, 99, "compactCount must NOT advance on mismatch");

		// Reconcile and save properly via explicit reassessment resolution
		recordObservedInvestigation(state, "read", { path: p }, "content", false);
		await tools["quest_update_state"].execute(
			"call_update_reconciled",
			{
				name: slug,
				goal: "Bookkeeping verify",
				understanding: "Reconciled state after mismatch investigation",
				assumptions: ["Durable state matches disk"],
				openQuestions: ["None"],
				findings: ["State verified"],
				plan: ["Continue with verified step"],
				planConfidence: "high",
				exactNextAction: "Verify compaction lifecycle",
				reassessmentComplete: true,
				reassessmentConclusion: "Investigated save count discrepancy and reconciled durable journal.",
			},
			{},
			() => {},
			ctx,
		);
		await tools["quest_mark_saved"].execute("call_save_reconciled", { name: slug }, {}, () => {}, ctx);

		// Case G: Valid match -> successful compaction bookkeeping occurs
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		const validTx = state.activeTransaction;
		assert.ok(validTx);
		const validCount = state.saveCount;

		// Force transport failure to inspect retained pendingResume
		api.mockPi.setThrowOnSend(true);

		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		assert.strictEqual(validTx.phase, "resume-pending", "Transaction marked resume-pending on valid match with transport failure");
		assert.strictEqual(state.dirty, false, "dirty flag cleared on valid match");
		assert.strictEqual(state.compactCount, validCount, "compactCount advanced on valid match");
		assert.ok(state.pendingResume, "Pending resume created on valid match with transport retry");
		assert.strictEqual(state.pendingResume.checkpointSaveCount, validTx.checkpointSaveCount);
		assert.strictEqual(state.pendingResume.checkpointHash, validTx.checkpointHash);
		assert.strictEqual(state.pendingResume.checkpointQuestPath, validTx.questPath);

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 24. quest_mark_saved does not resolve failed/inconsistent transactions
	// -----------------------------------------------------------------------
	await t.step("24. Regression Test: quest_mark_saved does not resolve failed/inconsistent transactions; gate remains blocked", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_mark_saved_no_resolve");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "test-mark-saved-no-resolve-quest";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("call_save_init", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);

		// Prepare a transaction and mark it failed
		const tx = createOrGetCompactionTransaction(state, "normal-compaction", slug);
		tx.phase = "failed";
		tx.error = "Simulated compaction failure";

		// Implementation gate must be blocked
		assert.strictEqual(canImplement(state, ctx as any), false, "Gate must be blocked when transaction is failed");
		const blockReason = getImplementationBlockReason(state, ctx as any);
		assert.strictEqual(blockReason.blocked, true);
		assert.strictEqual(blockReason.code, QuestErrorCode.COMPACTION_FAILURE);

		// Execute quest_mark_saved
		const saveRes = await tools["quest_mark_saved"].execute("call_save_while_failed", { name: slug }, {}, () => {}, ctx);
		assert.ok(saveRes.details?.hash, "Save should succeed");

		// INVARIANT: quest_mark_saved MUST NOT clear or resolve the failed transaction!
		assert.ok(state.activeTransaction, "activeTransaction must still exist after quest_mark_saved");
		assert.strictEqual(state.activeTransaction.phase, "failed", "activeTransaction must remain in failed phase");
		assert.strictEqual(canImplement(state, ctx as any), false, "Implementation gate must stay blocked after save");

		// Test inconsistent phase as well
		state.activeTransaction.phase = "inconsistent";
		assert.strictEqual(canImplement(state, ctx as any), false, "Gate must be blocked when transaction is inconsistent");

		await tools["quest_mark_saved"].execute("call_save_while_inconsistent", { name: slug }, {}, () => {}, ctx);
		assert.ok(state.activeTransaction);
		assert.strictEqual(state.activeTransaction.phase, "inconsistent", "activeTransaction must remain inconsistent after save");
		assert.strictEqual(canImplement(state, ctx as any), false, "Gate must stay blocked");

		// Explicit, validated reconciliation operation resolves the transaction
		recordObservedInvestigation(state, "read", { path: p }, "content", false);
		await tools["quest_update_state"].execute(
			"call_explicit_reconcile",
			{
				name: slug,
				goal: "Test mark saved",
				understanding: "Explicitly resolved after investigation",
				assumptions: ["Assumptions verified"],
				openQuestions: ["None"],
				findings: ["Transaction reconciled"],
				plan: ["Proceed with implementation"],
				planConfidence: "high",
				exactNextAction: "Continue execution",
				reassessmentComplete: true,
				reassessmentConclusion: "Investigated failure and explicitly reconciled state.",
			},
			{},
			() => {},
			ctx,
		);

		assert.strictEqual(state.activeTransaction, null, "activeTransaction cleared after explicit reassessmentComplete");
		assert.strictEqual(canImplement(state, ctx as any), true, "Implementation gate opens after explicit reconciliation");

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 9. Prepared transaction invalidation on new save: gen 15 -> gen 16 before compaction starts
	// -----------------------------------------------------------------------
	await t.step("9. Invalidation of prepared compaction transaction upon new save (gen 15 -> gen 16)", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_prepared_invalidation");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "quest-prepared-invalidation";
		await commands["quest"].handler(slug, ctx);
		const p = questPath(getState(ctx as any).questId);
		await tools["quest_mark_saved"].execute("save_gen1", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);

		// Fast-forward saveCount to simulate reaching gen 15
		state.saveCount = 15;
		state.saveGeneration = {
			count: 15,
			path: p,
			hash: "hash_gen_15",
			savedAt: Date.now(),
		};
		state.lastSavedHash = "hash_gen_15";

		// 1. Prepare compaction at generation 15
		const preparedTx = createOrGetCompactionTransaction(state, "normal-compaction", slug);
		assert.strictEqual(preparedTx.phase, "prepared");
		assert.strictEqual(preparedTx.checkpointSaveCount, 15);
		assert.strictEqual(preparedTx.checkpointHash, "hash_gen_15");
		assert.strictEqual(state.activeTransaction?.id, preparedTx.id);

		// 2. Save quest again at generation 16 before compaction starts
		await writeFile(p, `# Quest: ${slug}\n\n## Goal\nTest prepared invalidation\n\n## Current Status\nv2 (gen 16)\n`, "utf8");
		await tools["quest_mark_saved"].execute("save_gen16", { name: slug }, {}, () => {}, ctx);

		assert.strictEqual(state.saveCount, 16);
		const gen16Hash = state.saveGeneration?.hash;
		assert.ok(gen16Hash && gen16Hash !== "hash_gen_15");

		// Stale gen-15 transaction MUST be invalidated and cleared
		assert.strictEqual(state.activeTransaction, null, "Prepared transaction must be cleared on new save");

		// 3. Attempt compaction: fire session_before_compact
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		// 4. Verify the new transaction uses generation 16 and is in-flight
		const freshTx = (state as any).activeTransaction;
		assert.ok(freshTx, "Fresh transaction must be created for compaction");
		assert.notStrictEqual(freshTx.id, preparedTx.id, "Stale gen-15 transaction must NOT be reused");
		assert.strictEqual(freshTx.checkpointSaveCount, 16, "Fresh transaction must point to gen 16");
		assert.strictEqual(freshTx.checkpointHash, gen16Hash, "Fresh transaction must point to gen 16 hash");
		assert.strictEqual(freshTx.phase, "in-flight");

		api.agentMessages.length = 0;
		api.userMessages.length = 0;

		// 5. Fire session_compact completion
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		// 6. Verify NO false RESUME_STATE_INCONSISTENT occurs and resume succeeds
		const msgs = getAllMessages(api);
		assert.strictEqual(
			msgs.some((m) => m.includes("RESUME_STATE_INCONSISTENT")),
			false,
			"RESUME_STATE_INCONSISTENT must NOT occur when prepared checkpoint was updated",
		);

		assert.ok(
			msgs.some((m) => m.includes("Post-Compaction Autonomous Resumption Directive")),
			"Autonomous resume directive must be emitted successfully",
		);

		await rm(p, { force: true });
	});

	// -----------------------------------------------------------------------
	// 10. In-flight transaction preservation: save after compaction has started does not clear in-flight
	// -----------------------------------------------------------------------
	await t.step("10. In-flight compaction transaction is not cleared by save", async () => {
		const api = createMockExtensionAPI();
		plugin(api.mockPi as any);

		const ctx = createMockContext(10000, "session_inflight_preservation");
		const commands: Record<string, any> = {};
		for (const cmd of api.registeredCommands) commands[cmd.name] = cmd;
		const tools: Record<string, any> = {};
		for (const tool of api.registeredTools) tools[tool.name] = tool;

		const slug = "quest-inflight-preservation";
		const p = `${QUEST_DIR}/${slug}.md`;
		await writeFile(p, `# Quest: ${slug}\n\n## Goal\nTest in-flight preservation\n\n## Current Status\nv1\n`, "utf8");

		await commands["quest"].handler(slug, ctx);
		await tools["quest_mark_saved"].execute("save_gen1", { name: slug }, {}, () => {}, ctx);

		const state = getState(ctx as any);

		// Prepare transaction and transition to in-flight
		const tx = createOrGetCompactionTransaction(state, "normal-compaction", slug);
		tx.phase = "in-flight";

		// A save while in-flight MUST NOT clear the in-flight activeTransaction
		await writeFile(p, `# Quest: ${slug}\n\n## Goal\nTest in-flight preservation\n\n## Current Status\nv2 modified during compact\n`, "utf8");
		await tools["quest_mark_saved"].execute("save_during_compact", { name: slug }, {}, () => {}, ctx);

		assert.ok(state.activeTransaction, "in-flight transaction must survive save");
		assert.strictEqual(state.activeTransaction.id, tx.id);
		assert.strictEqual(state.activeTransaction.phase, "in-flight");

		await rm(p, { force: true });
	});
});
