import { existsSync } from "node:fs";
import { readFile, rm, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { QUEST_CURRENT_DIR } from "../src/constants.ts";
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
	setupCompactionTestHarness,
} from "./compaction_test_helpers.ts";
import { findProjectRoot, inspectZipEntries } from "../src/diagnostic.ts";
import { questArchivePath, questDirPath, questLogPath, questPath, resolveQuestRecordBySlug } from "../src/paths.ts";
import { setLifecycleStageObserver } from "../src/lifecycle.ts";
import { scheduleArchiveCompaction, scheduleEconomyCompaction, scheduleSubquestLaunchCompaction } from "../src/compaction/execution.ts";
import { createOrGetCompactionTransaction } from "../src/compaction/transaction.ts";
import { getSessionId } from "../src/state.ts";

Deno.test("lifecycle_consistency: authoritative completion, diagnostic archive ordering, and compaction transactional consistency", async (t) => {
	const currentDir = ".pi/quest/current";
	const archiveDir = ".pi/quest/archive";
	await mkdir(currentDir, { recursive: true });
	await mkdir(archiveDir, { recursive: true });

	// -----------------------------------------------------------------------
	// Scenario 1: Authoritative Completion -> Final State Committed -> Post-Completion Archive -> Consistent Changelog
	// -----------------------------------------------------------------------
	await t.step("1. Completion: final state committed -> archive generated afterward -> changelog records authoritative outcome", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_completion_ordering");
		const questName = "auth-completion-ordering-quest";

		// 1. Initialize quest
		await commands["quest"].handler(questName, ctx);
		const state = getState(ctx as any);
		const qid = state.questId!;
		assert.ok(qid, "Quest ID must be established");

		const qPath = questPath(qid);
		const qDir = questDirPath(qid);

		// 2. Perform research and complete prerequisites
		recordObservedInvestigation(state, "read", { path: "src/main.c" }, "code", false);
		await tools["quest_update_state"].execute(
			"call_res",
			{
				name: questName,
				goal: "Verify authoritative completion and archive ordering",
				status: "Research complete",
				understanding: "Completion must authoritatively commit state before diagnostic packaging.",
				assumptions: ["All completion checks pass"],
				openQuestions: ["None"],
				findings: ["Order is deterministic"],
				plan: ["1. Complete work", "2. Verify"],
				planConfidence: "high",
				exactNextAction: "Archive quest and verify diagnostic post-completion artifacts",
				researchComplete: true,
			},
			null,
			null,
			ctx,
		);

		// 3. Mark completed in markdown (no unfinished tasks in Remaining Work)
		const completeMarkdown = `# Quest: ${questName}

## Quest ID
${qid}

## Goal
Verify authoritative completion and archive ordering

## Current Status
COMPLETED - verified clean

## Current Understanding
Completion must authoritatively commit state before diagnostic packaging.

## Key Assumptions
- All completion checks pass

## Decisions Made
- Established single authoritative completion transition

## Remaining Work
- [x] All tasks completed and verified

## Exact Next Action
Archive quest and verify diagnostic post-completion artifacts
`;
		await writeFile(qPath, completeMarkdown, "utf8");
		await tools["quest_mark_saved"].execute("call_save", { name: questName }, {}, () => {}, ctx);

		// 4. Execute archive tool
		const archiveRes = await tools["quest_archive"].execute("call_archive", { questName, compact: false }, {}, () => {}, ctx);

		assert.strictEqual(archiveRes.details?.error, undefined, `Archive must succeed: ${JSON.stringify(archiveRes)}`);
		assert.strictEqual(state.active, null, "Active quest must be cleared after root completion");

		// 5. Verify post-completion artifacts on disk
		const projectRoot = findProjectRoot(ctx?.cwd);
		const zipPath = questArchivePath(qid, projectRoot);
		assert.ok(existsSync(zipPath), `Run archive zip must exist at ${zipPath}`);

		// Active directory should be removed
		const qDirResolved = resolve(projectRoot, QUEST_CURRENT_DIR, qid);
		assert.strictEqual(existsSync(qDirResolved), false, `Active quest directory ${qDirResolved} must be removed after archive packaging`);

		// Inspect zip entries: must contain initial prompt, summary, execution log, manifest
		const zipEntries = await inspectZipEntries(zipPath);
		assert.ok(zipEntries.includes("manifest.txt"), "Zip must contain manifest.txt");
		assert.ok(zipEntries.includes("initial-prompt.txt"), "Zip must contain initial-prompt.txt");
		assert.ok(zipEntries.includes("summary.md"), "Zip must contain summary.md");
		assert.ok(zipEntries.includes("execution.log"), "Zip must contain execution.log");

		// 6. Verify CHANGELOG.md contains authoritative outcome
		const changelogPath = `${projectRoot}/CHANGELOG.md`;
		if (existsSync(changelogPath)) {
			const changelogContent = await readFile(changelogPath, "utf8");
			assert.ok(
				changelogContent.includes(`Completed \`${questName}\` [${qid}]`),
				`CHANGELOG.md must record completed status for ${questName} [${qid}]`,
			);
		}

		await rm(zipPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// Scenario 2: Pre-compaction Checkpoint -> Compaction -> Resume -> Reconstructed State Matches Exactly
	// -----------------------------------------------------------------------
	await t.step("2. Pre-compaction checkpoint -> compaction -> resume -> state reconstruction matches checkpoint", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_compaction_exact_match");
		const questName = "checkpoint-exact-match-quest";

		// 1. Initialize quest
		await commands["quest"].handler(questName, ctx);
		const state = getState(ctx as any);
		const qid = state.questId!;
		const qPath = questPath(qid);

		recordObservedInvestigation(state, "read", { path: "src/core.c" }, "code", false);
		await tools["quest_update_state"].execute(
			"call_update",
			{
				name: questName,
				goal: "Verify exact checkpoint matching across compaction boundary",
				status: "Implementation in progress",
				understanding: "State checkpointing must be bitwise reproducible.",
				assumptions: ["Hash matches disk"],
				openQuestions: ["None"],
				findings: ["Checkpoint identity is immutable"],
				plan: ["1. Save state", "2. Compact", "3. Resume"],
				planConfidence: "high",
				exactNextAction: "Perform step 2",
				researchComplete: true,
			},
			null,
			null,
			ctx,
		);

		await tools["quest_mark_saved"].execute("call_save", { name: questName }, {}, () => {}, ctx);
		const initialSaveCount = state.saveCount;
		const initialHash = state.saveGeneration?.hash;
		assert.ok(initialHash, "Save hash must be established");

		// 2. Prepare and trigger compaction
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}

		assert.ok(state.activeTransaction, "Transaction must be prepared");
		assert.strictEqual(state.activeTransaction.phase, "in-flight");
		assert.strictEqual(state.activeTransaction.checkpointSaveCount, initialSaveCount);
		assert.strictEqual(state.activeTransaction.checkpointHash, initialHash);

		// 3. Complete compaction
		api.agentMessages.length = 0;
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		// 4. Assert resume delivered cleanly
		assert.strictEqual(state.activeTransaction?.phase, "resume-delivered");
		assert.strictEqual(state.pendingResume, null);
		assert.strictEqual(state.active, questName);

		const msgs = getAllMessages(api);
		assert.ok(
			msgs.some((m) => m.includes("Post-Compaction Autonomous Resumption Directive")),
			"Post-compaction resume directive must be delivered",
		);

		await rm(questDirPath(qid), { recursive: true, force: true });
	});

	// -----------------------------------------------------------------------
	// Scenario 3: Completion/Archive Racing with Pending Compaction -> No Resurrection, No Stale Resume
	// -----------------------------------------------------------------------
	await t.step("3. Completion/archive racing with pending compaction: no resurrection, no stale resume", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_racing_compaction");
		const questName = "racing-archive-compaction-quest";

		// 1. Initialize quest
		await commands["quest"].handler(questName, ctx);
		const state = getState(ctx as any);
		const qid = state.questId!;
		const qPath = questPath(qid);

		recordObservedInvestigation(state, "read", { path: "src/lib.c" }, "code", false);
		await tools["quest_update_state"].execute(
			"call_update",
			{
				name: questName,
				goal: "Verify race safety between completion and compaction",
				status: "Completed",
				understanding: "Archival must invalidate pending transactions and prevent resurrection.",
				assumptions: ["No ghost resumes"],
				openQuestions: ["None"],
				findings: ["Archive cleans pending obligations"],
				plan: ["1. Complete", "2. Archive"],
				planConfidence: "high",
				exactNextAction: "Archive quest",
				researchComplete: true,
			},
			null,
			null,
			ctx,
		);

		const completeMarkdown = `# Quest: ${questName}

## Quest ID
${qid}

## Goal
Verify race safety between completion and compaction

## Current Status
COMPLETED

## Current Understanding
Archival must invalidate pending transactions and prevent resurrection.

## Key Assumptions
- No ghost resumes

## Decisions Made
- Transaction invalidated on archive

## Remaining Work
- [x] All work verified clean

## Exact Next Action
Archive quest
`;
		await writeFile(qPath, completeMarkdown, "utf8");
		await tools["quest_mark_saved"].execute("call_save", { name: questName }, {}, () => {}, ctx);

		// 2. Start compaction before-compact hook (puts transaction in-flight)
		for (const cb of api.handlers["session_before_compact"] || []) {
			await cb({}, ctx);
		}
		assert.ok(state.activeTransaction, "Transaction must be in-flight prior to race");
		assert.strictEqual(state.activeTransaction.phase, "in-flight");

		// 3. User / agent completes and archives the quest before compaction finishes
		await tools["quest_archive"].execute("call_archive", { questName, compact: false }, {}, () => {}, ctx);

		assert.strictEqual(state.active, null, "state.active must be null after archive");
		assert.strictEqual(state.activeTransaction, null, "activeTransaction must be null after archive");
		assert.strictEqual(state.pendingResume, null, "pendingResume must be null after archive");

		// 4. Now the delayed host compaction callback fires
		api.agentMessages.length = 0;
		api.userMessages.length = 0;
		for (const cb of api.handlers["session_compact"] || []) {
			await cb({}, ctx);
		}

		// ASSERTIONS:
		// 1. Completed quest is NOT resurrected
		assert.strictEqual(state.active, null, "state.active MUST remain null after compaction callback");
		assert.strictEqual(state.pendingResume, null, "No pending resume obligation may be created for completed quest");

		// 2. No resume directive delivered for completed quest
		const msgs = getAllMessages(api);
		assert.strictEqual(
			msgs.filter((m) => m.includes("Post-Compaction Autonomous Resumption Directive")).length,
			0,
			"Must NOT deliver resume directive for completed quest",
		);

		// 3. No spurious error reported
		assert.strictEqual(
			msgs.filter((m) => m.includes("RESUME_STATE_INCONSISTENT") || m.includes("RESUME_DELIVERY_FAILURE")).length,
			0,
			"Must NOT report inconsistent state error for clean idle post-completion compaction",
		);

		// 4. Turn-end retry does NOT resurrect or fail
		for (const cb of api.handlers["turn_end"] || []) {
			await cb({ toolResults: [] }, ctx);
		}
		assert.strictEqual(state.active, null, "state.active must still remain null after turn_end");

		const projectRoot = findProjectRoot(ctx?.cwd);
		const zipPath = questArchivePath(qid, projectRoot);
		await rm(zipPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// Scenario 4: Strict Terminal Ordering & Post-Completion Diagnostic Artifact
	// -----------------------------------------------------------------------
	await t.step("4. Terminal ordering: commit/verify state -> remove active quest -> diagnostic zip post-completion -> changelog -> completion", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_terminal_ordering_proof");
		const questName = "terminal-ordering-proof-quest";

		await commands["quest"].handler(questName, ctx);
		const state = getState(ctx as any);
		const qid = state.questId!;
		const qPath = questPath(qid);
		const qDir = questDirPath(qid);
		const projectRoot = findProjectRoot(ctx?.cwd);

		recordObservedInvestigation(state, "read", { path: "src/engine.c" }, "code", false);
		await tools["quest_update_state"].execute(
			"call_update",
			{
				name: questName,
				goal: "Prove strict ordering of terminal completion and diagnostic zip generation",
				status: "Completed",
				understanding: "Terminal state must be committed before removal; zip generated post-removal as diagnostic artifact.",
				assumptions: ["Ordering is strictly verified"],
				openQuestions: ["None"],
				findings: ["Diagnostic zip does not gate completion"],
				plan: ["1. Commit", "2. Remove", "3. Zip"],
				planConfidence: "high",
				exactNextAction: "Archive",
				researchComplete: true,
			},
			null,
			null,
			ctx,
		);

		const completeMarkdown = `# Quest: ${questName}

## Quest ID
${qid}

## Goal
Prove strict ordering of terminal completion and diagnostic zip generation

## Current Status
COMPLETED

## Current Understanding
Terminal state must be committed before removal; zip generated post-removal as diagnostic artifact.

## Key Assumptions
- Ordering is strictly verified

## Decisions Made
- Diagnostic zip is a post-completion artifact

## Remaining Work
- [x] All work verified clean

## Exact Next Action
Archive
`;
		await writeFile(qPath, completeMarkdown, "utf8");
		await tools["quest_mark_saved"].execute("call_save", { name: questName }, {}, () => {}, ctx);

		// Observe lifecycle stage transitions to assert strict ordering
		const observedStages: Array<{ stage: string; details: any }> = [];
		setLifecycleStageObserver((stage, details) => {
			observedStages.push({ stage, details });
		});

		let archiveRes: any;
		try {
			archiveRes = await tools["quest_archive"].execute("call_archive", { questName, compact: false }, {}, () => {}, ctx);
		} finally {
			setLifecycleStageObserver(null);
		}

		assert.strictEqual(archiveRes.details?.error, undefined, "Archive must succeed");
		assert.strictEqual(state.active, null, "state.active must be null post-archival");

		// ASSERT STRICT ORDERING:
		// 1. All 4 lifecycle stages must occur in exact chronological order
		const stageNames = observedStages.map((s) => s.stage);
		assert.deepStrictEqual(
			stageNames,
			["terminal_commit", "active_removal", "zip_creation", "changelog_appended"],
			"Lifecycle stages must execute in exact strict order",
		);

		// 2. At stage 1 (terminal_commit): active quest dir MUST exist, zip MUST NOT exist, terminal state verified
		const stage1 = observedStages[0];
		assert.strictEqual(stage1.details.activeDirExists, true, "Active quest dir must exist during terminal state commit");
		assert.strictEqual(stage1.details.zipExists, false, "Diagnostic zip must not exist prior to removal");
		assert.strictEqual(stage1.details.authoritativeTerminalStatus, "COMPLETED", "Authoritative status must be COMPLETED");

		// 3. At stage 2 (active_removal): active quest dir MUST BE GONE, zip MUST NOT exist yet
		const stage2 = observedStages[1];
		assert.strictEqual(stage2.details.activeDirExists, false, "Active quest dir must be removed at active_removal stage");
		assert.strictEqual(stage2.details.zipExists, false, "Diagnostic zip must not exist at active_removal stage");

		// 4. At stage 3 (zip_creation): active quest dir MUST ALREADY BE ABSENT, zip MUST now exist
		const stage3 = observedStages[2];
		assert.strictEqual(stage3.details.activeDirExists, false, "Active quest dir must already be absent when zip is created");
		assert.strictEqual(stage3.details.zipExists, true, "Diagnostic zip must exist after zip_creation stage");

		// 5. At stage 4 (changelog_appended): zip MUST already exist, changelog recorded
		const stage4 = observedStages[3];
		assert.strictEqual(stage4.details.zipExists, true, "Diagnostic zip must exist when changelog is appended");
		assert.strictEqual(stage4.details.activeDirExists, false, "Active quest dir must remain absent");

		// Verify active quest directory is removed on disk
		const qDirResolved = resolve(projectRoot, QUEST_CURRENT_DIR, qid);
		assert.strictEqual(existsSync(qDirResolved), false, `Active quest directory ${qDirResolved} must be removed`);

		// Verify zip was generated post-removal containing all required files
		const zipPath = questArchivePath(qid, projectRoot);
		assert.ok(existsSync(zipPath), `Run archive zip must exist at ${zipPath}`);
		const zipEntries = await inspectZipEntries(zipPath);
		assert.ok(zipEntries.includes("manifest.txt"), "Zip must contain manifest.txt");
		assert.ok(zipEntries.includes("initial-prompt.txt"), "Zip must contain initial-prompt.txt");
		assert.ok(zipEntries.includes("summary.md"), "Zip must contain summary.md");
		assert.ok(zipEntries.includes("execution.log"), "Zip must contain execution.log");

		// Verify changelog entry derived from authoritative outcome
		const changelogPath = `${projectRoot}/CHANGELOG.md`;
		if (existsSync(changelogPath)) {
			const changelogContent = await readFile(changelogPath, "utf8");
			assert.ok(changelogContent.includes(`Completed \`${questName}\` [${qid}]`));
		}

		await rm(zipPath, { force: true });
	});

	// -----------------------------------------------------------------------
	// Scenario 5: Timing-Sensitive Asynchronous Stale-Callback Isolation (A -> B Race)
	// -----------------------------------------------------------------------
	await t.step("5. Timing-sensitive race: scheduled callback for Tx A cannot mutate Transaction B", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_async_timing_race");
		const questA = "async-race-quest-a";
		const questB = "async-race-quest-b";

		// 1. Initialize Quest A and prepare transaction A
		await commands["quest"].handler(questA, ctx);
		const stateA = getState(ctx as any);
		const qidA = stateA.questId!;
		await tools["quest_mark_saved"].execute("call_save_a", { name: questA }, {}, () => {}, ctx);

		const txA = createOrGetCompactionTransaction(stateA, "normal-compaction", questA);
		txA.phase = "in-flight";
		stateA.compactionPending = true;
		const idA = txA.id;

		// 2. Schedule economy compaction for Quest A (this schedules a 50ms setTimeout)
		let compactErrorCallbackA: any = null;
		(ctx as any).compact = (options: any) => {
			compactErrorCallbackA = options.onError;
		};
		const sessionId = getSessionId(ctx as any);
		scheduleEconomyCompaction(api.mockPi as any, ctx as any, sessionId, "instructions for A");

		// 3. Before the timeout or error callback fires, state changes to Quest B with Transaction B
		await commands["quest"].handler(questB, ctx);
		const stateB = getState(ctx as any);
		const qidB = stateB.questId!;
		assert.notStrictEqual(qidA, qidB, "Quest B must have different QID");
		await tools["quest_mark_saved"].execute("call_save_b", { name: questB }, {}, () => {}, ctx);

		const txB = createOrGetCompactionTransaction(stateB, "normal-compaction", questB);
		txB.phase = "in-flight";
		stateB.compactionPending = true;
		const idB = txB.id;
		assert.notStrictEqual(idA, idB, "Transaction B must have distinct ID from A");

		// 4. Now simulate the delayed timer / error callback for Quest A firing while B is current
		api.agentMessages.length = 0;
		api.userMessages.length = 0;
		await new Promise((r) => setTimeout(r, 60)); // let scheduled timer fire

		if (compactErrorCallbackA) {
			// Fire error callback for A
			compactErrorCallbackA(new Error("Compaction failed for A in background"));
		}

		// ASSERTIONS:
		// Transaction B MUST NOT be mutated by A's callback!
		assert.strictEqual(stateB.active, questB, "Active quest must remain B");
		assert.strictEqual(stateB.activeTransaction?.id, idB, "Active transaction must remain B's ID");
		assert.strictEqual(stateB.activeTransaction?.phase, "in-flight", "Transaction B must remain in-flight (not failed by A)");
		assert.strictEqual(stateB.compactionPending, true, "compactionPending for B must not be cleared by A");
		assert.strictEqual(stateB.activeTransaction?.error, undefined, "Transaction B must not have error from A");

		// No fallback resume for A was dispatched to B's active session
		const msgs = getAllMessages(api);
		assert.strictEqual(
			msgs.filter((m) => m.includes(questA)).length,
			0,
			"No message for quest A should be dispatched after B became active",
		);

		await rm(questDirPath(qidA), { recursive: true, force: true });
		await rm(questDirPath(qidB), { recursive: true, force: true });
	});

	// -----------------------------------------------------------------------
	// Scenario 6: Stale callback with cleared activeTransaction performs no mutation
	// -----------------------------------------------------------------------
	await t.step("6. Timing-sensitive race: scheduled callback for Tx A with cleared activeTransaction performs no state mutation", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_async_cleared_tx");
		const questA = "async-cleared-quest-a";

		// 1. Initialize Quest A and prepare transaction A
		await commands["quest"].handler(questA, ctx);
		const stateA = getState(ctx as any);
		const qidA = stateA.questId!;
		await tools["quest_mark_saved"].execute("call_save_a", { name: questA }, {}, () => {}, ctx);

		const txA = createOrGetCompactionTransaction(stateA, "normal-compaction", questA);
		txA.phase = "in-flight";
		stateA.compactionPending = true;
		const idA = txA.id;

		// 2. Schedule economy compaction for Quest A
		let compactErrorCallbackA: any = null;
		(ctx as any).compact = (options: any) => {
			compactErrorCallbackA = options.onError;
		};
		const sessionId = getSessionId(ctx as any);
		scheduleEconomyCompaction(api.mockPi as any, ctx as any, sessionId, "instructions for A");

		// 3. Clear active transaction (e.g. quest completed, reset, or transaction cleared)
		stateA.activeTransaction = null;
		stateA.activeCompactionId = null;
		stateA.compactionPending = false;

		// 4. Now simulate the delayed timer / error callback for Quest A firing
		api.agentMessages.length = 0;
		api.userMessages.length = 0;
		await new Promise((r) => setTimeout(r, 60)); // let scheduled timer fire

		if (compactErrorCallbackA) {
			// Fire error callback for A
			compactErrorCallbackA(new Error("Compaction failed for A in background"));
		}

		// ASSERTIONS:
		// Active transaction must remain null (not resurrected or mutated to failed)
		assert.strictEqual(stateA.activeTransaction, null, "activeTransaction must remain null");
		assert.strictEqual(stateA.compactionPending, false, "compactionPending must remain false");
		assert.strictEqual(stateA.activeCompactionId, null, "activeCompactionId must remain null");

		// No error notification or agent messages were dispatched
		const msgs = getAllMessages(api);
		assert.strictEqual(
			msgs.filter((m) => m.includes("Economy auto-compaction failed")).length,
			0,
			"No error message should be dispatched when active transaction is cleared",
		);

		await rm(questDirPath(qidA), { recursive: true, force: true });
	});

	// -----------------------------------------------------------------------
	// Scenario 7: Late log events after active removal go to finalized log without resurrecting current/<qid>
	// -----------------------------------------------------------------------
	await t.step("7. Late log events after active removal go to finalized log, not a recreated current/<qid>/execution.log", async () => {
		const { api, ctx, commands, tools } = setupCompactionTestHarness(10000, "session_late_logging");
		const questName = "late-logging-quest";

		// 1. Initialize Quest and write completion
		await commands["quest"].handler(questName, ctx);
		const state = getState(ctx as any);
		const qid = state.questId!;
		const qPath = questPath(qid);

		recordObservedInvestigation(state, "read", { path: "src/finish.c" }, "code", false);
		await tools["quest_update_state"].execute(
			"call_update",
			{
				name: questName,
				goal: "Verify late log events do not resurrect current directory",
				status: "COMPLETED",
				understanding: "Logging must pin to finalized sink upon archival.",
				assumptions: ["No directory resurrection"],
				openQuestions: ["None"],
				findings: ["Log pinned"],
				plan: ["1. Finish", "2. Archive"],
				planConfidence: "high",
				exactNextAction: "Archive",
				researchComplete: true,
			},
			null,
			null,
			ctx,
		);

		const completeMarkdown = `# Quest: ${questName}

## Quest ID
${qid}

## Goal
Verify late log events do not resurrect current directory

## Current Status
COMPLETED

## Remaining Work
- [x] All tasks completed

## Exact Next Action
Archive
`;
		await writeFile(qPath, completeMarkdown, "utf8");
		await tools["quest_mark_saved"].execute("call_save", { name: questName }, {}, () => {}, ctx);

		// 2. Archive quest
		const archiveRes = await tools["quest_archive"].execute("call_archive", { questName, compact: false }, {}, () => {}, ctx);
		assert.strictEqual(archiveRes.details?.error, undefined, "Archive must succeed");

		// 3. Verify active directory is removed
		const currentDirPath = questDirPath(qid);
		assert.strictEqual(existsSync(currentDirPath), false, "Active quest directory must be removed after archival");

		// 4. Simulate late asynchronous lifecycle events firing for this archived quest
		const { logEvent } = await import("../src/logging.ts");
		logEvent("TOOL_ACTIVITY", "Late callback execution event after active removal", {
			questId: qid,
			quest: questName,
			operation: "success",
			phase: "completion",
		});
		logEvent("TURN_END", "Late turn boundary event after active removal", {
			questId: qid,
			quest: questName,
			turn: 99,
		});

		// 5. ASSERTIONS:
		// Active directory must NEVER be recreated!
		assert.strictEqual(existsSync(currentDirPath), false, "Active quest directory must NOT be resurrected by late log events");
		assert.strictEqual(existsSync(`${currentDirPath}/execution.log`), false, "current/<qid>/execution.log must NOT exist");

		// Pinned finalized log must exist and contain the late events
		const projectRoot = findProjectRoot(ctx?.cwd);
		const finalizedLogPath = resolve(projectRoot, ".pi/quest/finalized_logs", `${qid}.log`);
		assert.ok(existsSync(finalizedLogPath), `Pinned finalized log must exist at ${finalizedLogPath}`);

		const finalizedContent = await readFile(finalizedLogPath, "utf8");
		assert.ok(finalizedContent.includes("Late callback execution event after active removal"), "Finalized log must contain late events");
		assert.ok(finalizedContent.includes("Late turn boundary event after active removal"), "Finalized log must contain late turn events");

		const zipPath = questArchivePath(qid, projectRoot);
		await rm(zipPath, { force: true });
		await rm(finalizedLogPath, { force: true });
	});
});
