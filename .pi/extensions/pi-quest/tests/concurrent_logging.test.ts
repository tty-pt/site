import assert from "node:assert";
import { existsSync, readFileSync, writeFileSync } from "node:fs";
import { mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";
import {
	formatLogEntry,
	getQuestLogPath,
	getRunLogPath,
	logEvent,
	parseLogEntry,
} from "../src/logging.ts";
import {
	asyncContext,
	createDefaultState,
	generateQuestId,
	getQuestId,
	getState,
	sessionStates,
	setSessionState,
	snapshotState,
	state,
} from "../src/state.ts";
import {
	createDiagnosticZip,
	findExtensionDir,
	findProjectRoot,
	parseRunLogFile,
	resolveActiveRunHierarchy,
	verifyDiagnosticZip,
} from "../src/diagnostic.ts";
import { QUEST_CURRENT_DIR } from "../src/constants.ts";
import { ExtensionContext, StoredState } from "../src/types.ts";
import { createOrGetCompactionTransaction } from "../src/compaction/transaction.ts";

function createMockContext(sessionId: string, cwd: string): ExtensionContext {
	return {
		cwd,
		sessionManager: { id: sessionId, getSessionId: () => sessionId },
		ui: { notify: () => {} },
		hasUI: false,
	};
}

Deno.test("concurrent_logging: per-run log isolation and concurrency guarantees", async (t) => {
	const tempRoot = await mkdtemp(join(tmpdir(), "pi-concurrent-logging-"));
	const currentDir = join(tempRoot, QUEST_CURRENT_DIR);

	await mkdir(currentDir, { recursive: true });

	const realProjectRoot = findProjectRoot();
	const realExtDir = findExtensionDir(realProjectRoot);

	try {
		await t.step("1. Start run A -> log event -> run B -> log event -> A log contains only A, B log contains only B", async () => {
			const ctxA = createMockContext("session-A", tempRoot);
			const ctxB = createMockContext("session-B", tempRoot);

			const questIdA = "q_test_A_111";
			const questIdB = "q_test_B_222";

			// Initialize session A state
			const stateA = createDefaultState();
			stateA.questId = questIdA;
			stateA.active = "quest-alpha";
			stateA.stack = ["quest-alpha"];
			setSessionState(ctxA, stateA);

			// Initialize session B state
			const stateB = createDefaultState();
			stateB.questId = questIdB;
			stateB.active = "quest-beta";
			stateB.stack = ["quest-beta"];
			setSessionState(ctxB, stateB);

			const logPathA = getQuestLogPath(questIdA, currentDir);
			const logPathB = getQuestLogPath(questIdB, currentDir);

			// Log events from context A
			await asyncContext.run(ctxA, async () => {
				logEvent("QUEST_CREATED", "created alpha quest", { logPath: logPathA, questId: questIdA });
				logEvent("TURN_START", "turn 1 start in A", { logPath: logPathA, questId: questIdA });
			});

			// Log events from context B
			await asyncContext.run(ctxB, async () => {
				logEvent("QUEST_CREATED", "created beta quest", { logPath: logPathB, questId: questIdB });
				logEvent("TURN_START", "turn 1 start in B", { logPath: logPathB, questId: questIdB });
				logEvent("TURN_END", "turn 1 end in B", { logPath: logPathB, questId: questIdB });
			});

			// Verify log A contents
			const contentA = await readFile(logPathA, "utf8");
			assert.ok(contentA.includes("quest-alpha"));
			assert.ok(contentA.includes("turn 1 start in A"));
			assert.ok(!contentA.includes("quest-beta"), "Log A must not contain run B events");
			assert.ok(!contentA.includes("turn 1 start in B"), "Log A must not contain run B events");

			// Verify log B contents
			const contentB = await readFile(logPathB, "utf8");
			assert.ok(contentB.includes("quest-beta"));
			assert.ok(contentB.includes("turn 1 start in B"));
			assert.ok(!contentB.includes("quest-alpha"), "Log B must not contain run A events");
			assert.ok(!contentB.includes("turn 1 start in A"), "Log B must not contain run A events");
		});

		await t.step("2. Same root quest in two independent runs -> separate run IDs and separate logs", async () => {
			const questId1 = "q_same_root_1";
			const questId2 = "q_same_root_2";

			const ctx1 = createMockContext("session-same-1", tempRoot);
			const ctx2 = createMockContext("session-same-2", tempRoot);

			const state1 = createDefaultState();
			state1.questId = questId1;
			state1.active = "common-quest";
			state1.stack = ["common-quest"];
			setSessionState(ctx1, state1);

			const state2 = createDefaultState();
			state2.questId = questId2;
			state2.active = "common-quest";
			state2.stack = ["common-quest"];
			setSessionState(ctx2, state2);

			const logPath1 = getQuestLogPath(questId1, currentDir);
			const logPath2 = getQuestLogPath(questId2, currentDir);

			await asyncContext.run(ctx1, async () => {
				logEvent("QUEST_CREATED", "run 1 started common quest", { logPath: logPath1, questId: questId1 });
			});

			await asyncContext.run(ctx2, async () => {
				logEvent("QUEST_CREATED", "run 2 started common quest independently", { logPath: logPath2, questId: questId2 });
			});

			assert.notStrictEqual(logPath1, logPath2);
			const content1 = await readFile(logPath1, "utf8");
			const content2 = await readFile(logPath2, "utf8");

			assert.ok(content1.includes("run 1 started"));
			assert.ok(!content1.includes("run 2 started"));

			assert.ok(content2.includes("run 2 started"));
			assert.ok(!content2.includes("run 1 started"));
		});

		await t.step("3. One run with multiple subquests -> exactly one run log, events identify active subquest", async () => {
			const questId = "q_subquests_demo";
			const ctx = createMockContext("session-subs", tempRoot);

			const s = createDefaultState();
			s.questId = questId;
			s.active = "root-engine";
			s.stack = ["root-engine"];
			setSessionState(ctx, s);

			const logPath = getQuestLogPath(questId, currentDir);

			await asyncContext.run(ctx, async () => {
				// Root event
				logEvent("QUEST_CREATED", "root created", { logPath, questId });

				// Subquest 1 started
				s.active = "sub-parser";
				s.stack = ["root-engine", "sub-parser"];
				logEvent("SUBQUEST_START", "sub-parser started", { quest: "sub-parser", parent: "root-engine", logPath, questId });

				// Subquest 2 started
				s.active = "sub-compiler";
				s.stack = ["root-engine", "sub-compiler"];
				logEvent("SUBQUEST_START", "sub-compiler started", { quest: "sub-compiler", parent: "root-engine", logPath, questId });

				// Subquest 2 returned
				s.active = "root-engine";
				s.stack = ["root-engine"];
				logEvent("SUBQUEST_RETURN", "sub-compiler returned", { quest: "root-engine", child: "sub-compiler", logPath, questId });
			});

			const parsed = await parseRunLogFile(logPath);
			assert.ok(parsed);
			assert.strictEqual(parsed.questId, questId);
			assert.strictEqual(parsed.rootQuest, "root-engine");
			assert.ok(parsed.subquests.includes("sub-parser"));
			assert.ok(parsed.subquests.includes("sub-compiler"));

			const content = await readFile(logPath, "utf8");
			assert.ok(content.includes("quest=sub-parser"));
			assert.ok(content.includes("quest=sub-compiler"));
			assert.ok(content.includes("root=root-engine"));
		});

		await t.step("4. Compaction/resume within a run -> continues in the same run log", async () => {
			const questId = "q_compaction_flow";
			const ctx = createMockContext("session-compact", tempRoot);

			const s = createDefaultState();
			s.questId = questId;
			s.active = "persistent-quest";
			s.stack = ["persistent-quest"];
			setSessionState(ctx, s);

			const logPath = getQuestLogPath(questId, currentDir);

			await asyncContext.run(ctx, async () => {
				logEvent("TURN_START", "before compaction turn", { logPath, questId });

				// Prepare compaction transaction
				const tx = createOrGetCompactionTransaction(s, "normal-compaction");
				logEvent("COMPACTION_STARTED", "compaction started", { compactionId: tx.id, logPath, questId });

				// Snapshot state across compaction
				const preserved = snapshotState(ctx);
				assert.strictEqual(preserved.questId, questId, "Compaction state must preserve questId");

				// Post-compaction resume
				logEvent("COMPACTION_COMPLETED", "compaction completed", { compactionId: tx.id, logPath, questId });
				logEvent("RESUME_DELIVERED", "resumed in same run", { compactionId: tx.id, logPath, questId });
				logEvent("TURN_START", "post compaction turn", { logPath, questId });
			});

			const content = await readFile(logPath, "utf8");
			assert.ok(content.includes("before compaction turn"));
			assert.ok(content.includes("COMPACTION_STARTED"));
			assert.ok(content.includes("COMPACTION_COMPLETED"));
			assert.ok(content.includes("post compaction turn"));
		});

		await t.step("5. Diagnostic packaging for run A -> includes A's log and quest state, does not include B's", async () => {
			const questIdA = "q_pkg_A";
			const questIdB = "q_pkg_B";

			const logPathA = getQuestLogPath(questIdA, currentDir);
			const logPathB = getQuestLogPath(questIdB, currentDir);

			// Write quest A files
			await mkdir(join(currentDir, questIdA), { recursive: true });
			await writeFile(join(currentDir, questIdA, "quest.md"), "# Quest: quest-A\n\n## Sub-Quests\n- [ ] [[sub-A]]\n", "utf8");

			// Write quest B files
			await mkdir(join(currentDir, questIdB), { recursive: true });
			await writeFile(join(currentDir, questIdB, "quest.md"), "# Quest: quest-B\n\n## Sub-Quests\n- [ ] [[sub-B]]\n", "utf8");

			// Write log A
			await writeFile(
				logPathA,
				"2026-08-31T03:00:00.000Z | QUEST_CREATED | questId=" + questIdA + " root=quest-A quest=quest-A | init A\n" +
					"2026-08-31T03:01:00.000Z | SUBQUEST_START | questId=" + questIdA + " root=quest-A quest=sub-A parent=quest-A child=sub-A | sub A\n",
				"utf8",
			);

			// Write log B
			await writeFile(
				logPathB,
				"2026-08-31T03:10:00.000Z | QUEST_CREATED | questId=" + questIdB + " root=quest-B quest=quest-B | init B\n" +
					"2026-08-31T03:11:00.000Z | SUBQUEST_START | questId=" + questIdB + " root=quest-B quest=sub-B parent=quest-B child=sub-B | sub B\n",
				"utf8",
			);

			// Package run A explicitly
			const zipPathA = join(tempRoot, "pi-quest-bundle-A.zip");
			const resultA = await createDiagnosticZip({
				projectRoot: tempRoot,
				extensionDir: realExtDir,
				outputZipPath: zipPathA,
				questId: questIdA,
			});

			assert.strictEqual(resultA.verification.valid, true);
			const entriesA = resultA.verification.entries;

			// Verify run A items are present
			assert.ok(entriesA.includes("run/manifest.txt") || entriesA.includes("diagnostic/current-run/manifest.txt"));
			assert.ok(entriesA.includes("run/initial-prompt.txt"));
			assert.ok(entriesA.includes("run/summary.md"));
			assert.ok(entriesA.includes("run/execution.log") || entriesA.includes("diagnostic/current-run/run.log"));

			// Check manifest content for run A
			assert.ok(resultA.manifest.includes(`questId: ${questIdA}`));
			assert.ok(resultA.manifest.includes("quest-A"));
			assert.ok(resultA.manifest.includes("sub-A"));
			assert.ok(!resultA.manifest.includes("quest-B"));
		});

		await t.step("6. Concurrent writes to separate run logs -> no interleaving or state cross-contamination", async () => {
			const questId1 = "q_concurrent_writer_1";
			const questId2 = "q_concurrent_writer_2";

			const log1 = getQuestLogPath(questId1, currentDir);
			const log2 = getQuestLogPath(questId2, currentDir);

			const ctx1 = createMockContext("session-worker-1", tempRoot);
			const ctx2 = createMockContext("session-worker-2", tempRoot);

			const s1 = createDefaultState();
			s1.questId = questId1;
			s1.active = "worker-quest-1";
			setSessionState(ctx1, s1);

			const s2 = createDefaultState();
			s2.questId = questId2;
			s2.active = "worker-quest-2";
			setSessionState(ctx2, s2);

			const count = 50;
			const task1 = async () => {
				for (let i = 0; i < count; i++) {
					await asyncContext.run(ctx1, async () => {
						logEvent("TURN_START", `worker 1 iteration ${i}`, { logPath: log1, questId: questId1 });
					});
				}
			};

			const task2 = async () => {
				for (let i = 0; i < count; i++) {
					await asyncContext.run(ctx2, async () => {
						logEvent("TURN_START", `worker 2 iteration ${i}`, { logPath: log2, questId: questId2 });
					});
				}
			};

			// Run both writers simultaneously
			await Promise.all([task1(), task2()]);

			const content1 = await readFile(log1, "utf8");
			const content2 = await readFile(log2, "utf8");

			const lines1 = content1.trim().split("\n");
			const lines2 = content2.trim().split("\n");

			assert.strictEqual(lines1.length, count, "Worker 1 should have exact expected entry count");
			assert.strictEqual(lines2.length, count, "Worker 2 should have exact expected entry count");

			assert.ok(lines1.every((l) => l.includes("worker 1") && l.includes(questId1) && !l.includes("worker 2")));
			assert.ok(lines2.every((l) => l.includes("worker 2") && l.includes(questId2) && !l.includes("worker 1")));
		});
	} finally {
		await rm(tempRoot, { recursive: true, force: true });
	}
});
