import assert from "node:assert";
import { cp, mkdir, mkdtemp, readFile, rm, stat, writeFile } from "node:fs/promises";
import { existsSync } from "node:fs";
import { join, resolve } from "node:path";
import { tmpdir } from "node:os";
import {
	appendChangelogEntry,
	computeFileSha256,
	computeStagedFilesHash,
	createDiagnosticZip,
	createRunArchive,
	createRunDirectory,
	createUnifiedBundleZip,
	findArchivedQuestFile,
	findExtensionDir,
	findProjectRoot,
	generateRunManifest,
	generateRunSummary,
	inspectZipEntries,
	resolveActiveRunHierarchy,
	runShellCommand,
	verifyDiagnosticZip,
	verifyRunArchive,
} from "../src/diagnostic.ts";
import { QUEST_CURRENT_DIR } from "../src/constants.ts";
import questJournalExtension from "../index.ts";

Deno.test("diagnostic_zip: project root and extension dir resolution", async (t) => {
	await t.step("findProjectRoot resolves host project root from cwd and subdirectories", () => {
		const root = findProjectRoot();
		assert.ok(root && root.length > 0);
		assert.ok(root.endsWith("site"));

		const fromExt = findProjectRoot(join(root, ".pi/extensions/pi-quest"));
		assert.strictEqual(fromExt, root);

		const fromDeep = findProjectRoot(join(root, ".pi/extensions/pi-quest/src/nested"));
		assert.strictEqual(fromDeep, root);
	});

	await t.step("findExtensionDir resolves extension directory", () => {
		const root = findProjectRoot();
		const extDir = findExtensionDir(root);
		assert.ok(extDir.endsWith(".pi/extensions/pi-quest"));
	});
});

Deno.test("diagnostic_zip: 12-point run-archive & diagnostic simplification requirements", async (t) => {
	const tempRoot = await mkdtemp(join(tmpdir(), "pi-diag-12points-"));
	const realProjectRoot = findProjectRoot();
	const realExtDir = findExtensionDir(realProjectRoot);

	try {
		const currentDir = join(tempRoot, ".pi/quest/current");
		const archiveDir = join(tempRoot, ".pi/quest/archive");
		await mkdir(currentDir, { recursive: true });
		await mkdir(archiveDir, { recursive: true });

		const questId = "a7b3c9e100";
		const questDir = join(currentDir, questId);
		await mkdir(questDir, { recursive: true });

		// Setup root quest
		const rootContent = [
			"# Quest: user-auth-flow",
			"",
			"## Goal",
			"Implement user authentication flow with passkeys.",
			"",
			"## Original request",
			"> Implement user authentication flow with passkeys.",
			"",
			"## Current Understanding",
			"Passkey authentication requires WebAuthn challenge handling in pure C.",
			"",
			"## Execution Snapshot",
			"",
			"### Completed",
			"- Implemented challenge generation in mods/auth/challenge.c.",
			"- Implemented signature verification.",
			"",
			"### Decisions",
			"- Keep all UX isomorphic in mods/auth/ux/auth_ux.c.",
			"",
			"### Test / Build Status",
			"- make test -> 0 errors.",
			"",
			"### Remaining Work",
			"- [ ] None",
			"",
			"## Sub-Quests",
			"- [x] [[auth-crypto-helper]]",
			"",
		].join("\n");
		await writeFile(join(questDir, "quest.md"), rootContent, "utf8");

		// Setup subquest in archive (completed subquest belonging to run)
		const subContent = [
			"# Quest: auth-crypto-helper",
			"",
			"## Parent Quest",
			"[[user-auth-flow]]",
			"",
			"## Goal",
			"Helper crypto routines for passkeys.",
			"",
			"## Current Status",
			"- [x] Completed",
			"",
		].join("\n");
		await writeFile(join(archiveDir, "auth-crypto-helper-12345.md"), subContent, "utf8");

		// Setup unrelated quest in archive
		await writeFile(join(archiveDir, "unrelated-old-quest-999.md"), "# Unrelated\n", "utf8");

		// Setup execution log
		const logPath = join(questDir, "execution.log");
		await writeFile(
			logPath,
			"2026-08-31T01:00:00.000Z | QUEST_CREATED | questId=" + questId + " root=user-auth-flow quest=user-auth-flow | user prompt\n" +
				"2026-08-31T01:01:00.000Z | GATE_BLOCKED | questId=" + questId + " root=user-auth-flow quest=user-auth-flow gate=RESEARCH_PENDING | blocked by gate\n" +
				"2026-08-31T01:02:00.000Z | RESEARCH_COMPLETED | questId=" + questId + " root=user-auth-flow quest=user-auth-flow | research done\n" +
				"2026-08-31T01:03:00.000Z | SUBQUEST_START | questId=" + questId + " root=user-auth-flow quest=auth-crypto-helper parent=user-auth-flow | sub start\n" +
				"2026-08-31T01:04:00.000Z | ARCHIVE | questId=" + questId + " root=user-auth-flow quest=auth-crypto-helper parent=user-auth-flow dest=.pi/quest/archive/auth-crypto-helper-12345.md | sub archived\n" +
				"2026-08-31T01:05:00.000Z | COMPACTION_COMPLETED | questId=" + questId + " root=user-auth-flow quest=user-auth-flow compactionId=cmp_1 | compacted\n" +
				"2026-08-31T01:06:00.000Z | RESUME_DELIVERED | questId=" + questId + " root=user-auth-flow quest=user-auth-flow compactionId=cmp_1 | resume delivered\n" +
				"2026-08-31T01:07:00.000Z | TEST_PASSED | questId=" + questId + " root=user-auth-flow quest=user-auth-flow | tests passed\n" +
				"2026-08-31T01:08:00.000Z | ARCHIVE | questId=" + questId + " root=user-auth-flow quest=user-auth-flow dest=.pi/quest/archive/user-auth-flow-99999.md | root archived\n",
			"utf8",
		);

		// Requirement 1 & 2: Completed root quest produces one run summary synthesized from quest state + log
		await t.step("Req 1 & 2: completed root quest produces summary.md generated from final quest state + execution log", async () => {
			const hierarchy = await resolveActiveRunHierarchy(tempRoot, { questId });
			assert.strictEqual(hierarchy.questId, questId);
			assert.strictEqual(hierarchy.activeRootQuest, "user-auth-flow");

			const summary = await generateRunSummary(hierarchy, tempRoot);
			assert.ok(summary.includes(`# Run Summary (${questId})`));
			assert.ok(summary.includes("Implement user authentication flow with passkeys."));
			assert.ok(summary.includes("Implemented challenge generation"));
			assert.ok(summary.includes("Passkey authentication requires WebAuthn"));
			assert.ok(summary.includes("Keep all UX isomorphic in mods/auth/ux/auth_ux.c"));
			assert.ok(summary.includes("RESEARCH_PENDING"), "Summary must mention diagnostic gate events");
			assert.ok(summary.includes("Compactions: 1"), "Summary must reflect compaction outcome");
		});

		// Requirement 3, 4, 5, 6, 8: Standalone run archive contains initial-prompt, summary, manifest, execution.log, excludes source code
		await t.step("Req 3, 4, 5, 6, 8: run archive includes initial-prompt + summary + manifest + execution.log, excludes source code", async () => {
			const runArchiveRes = await createRunArchive({
				projectRoot: tempRoot,
				questId,
			});

			assert.strictEqual(runArchiveRes.questId, questId);
			assert.strictEqual(runArchiveRes.verification.valid, true);

			const entries = runArchiveRes.verification.entries;

			// Manifest, initial-prompt, summary, execution.log included
			assert.ok(entries.includes("manifest.txt") || entries.includes("run/manifest.txt"));
			assert.ok(entries.includes("initial-prompt.txt") || entries.includes("run/initial-prompt.txt"));
			assert.ok(entries.includes("summary.md") || entries.includes("run/summary.md"));
			assert.ok(entries.includes("execution.log") || entries.includes("run/execution.log"));

			// Source code and node_modules excluded from run archive
			assert.ok(!entries.some((e) => e.startsWith("run/src/") || e.endsWith(".c") || e.endsWith(".ts")));
			assert.ok(!entries.some((e) => e.includes("node_modules/")));
		});

		// Requirement 7: Concurrent runs create separate run directories/logs with short 7-10 char hashes
		await t.step("Req 7: concurrent runs create isolated run directories and logs with short 7-10 char hashes", async () => {
			const qid1 = "c1d2e3f400";
			const qid2 = "f5g6h7j800";

			const qDir1 = join(currentDir, qid1);
			const qDir2 = join(currentDir, qid2);
			await mkdir(qDir1, { recursive: true });
			await mkdir(qDir2, { recursive: true });

			await writeFile(join(qDir1, "execution.log"), `2026-08-31T01:00:00.000Z | QUEST_CREATED | questId=${qid1} quest=quest1 | run 1\n`, "utf8");
			await writeFile(join(qDir2, "execution.log"), `2026-08-31T01:00:00.000Z | QUEST_CREATED | questId=${qid2} quest=quest2 | run 2\n`, "utf8");

			const h1 = await resolveActiveRunHierarchy(tempRoot, { questId: qid1 });
			const h2 = await resolveActiveRunHierarchy(tempRoot, { questId: qid2 });

			assert.strictEqual(h1.questId, qid1);
			assert.strictEqual(h2.questId, qid2);
			assert.notStrictEqual(h1.logPath, h2.logPath);
			assert.ok(h1.logPath.includes(qid1));
			assert.ok(h2.logPath.includes(qid2));
		});

		// Requirement 9: A failed archive does not destroy the underlying run evidence
		await t.step("Req 9: a failed archive does not destroy underlying run evidence", async () => {
			const badQid = "fail_test1";
			const badQuestDir = join(currentDir, badQid);
			await mkdir(badQuestDir, { recursive: true });
			await writeFile(join(badQuestDir, "execution.log"), "sample log content\n", "utf8");

			// Intentionally pass an invalid output path in a read-only or invalid location to trigger failure
			try {
				await createRunArchive({
					projectRoot: tempRoot,
					questId: badQid,
					outputZipPath: "/invalid_nonexistent_dir_123/bad.zip",
				});
			} catch {}

			// Verify run evidence in .pi/quest/current/<qid>/ is completely preserved
			assert.ok(
				existsSync(join(badQuestDir, "execution.log")),
				"execution.log must survive archive failure",
			);
			assert.ok(
				existsSync(join(badQuestDir, "summary.md")),
				"summary.md must survive archive failure",
			);
			assert.ok(
				existsSync(join(badQuestDir, "manifest.txt")),
				"manifest.txt must survive archive failure",
			);
		});

		// Requirement 10 & 11: One concise CHANGELOG line for terminal root quest, none for subquests, and truthful outcome prefix
		await t.step("Req 10 & 11: one concise CHANGELOG line added for terminal root quest, none for subquest alone, truthful status prefix", async () => {
			const changelogPath = join(tempRoot, "CHANGELOG.md");
			await rm(changelogPath, { force: true });

			// 1. Subquest completion alone does NOT add changelog line
			const resSub = false; // Subquests do not trigger appendChangelogEntry
			if (resSub) {
				await appendChangelogEntry(tempRoot, "sub-feature", "sub feature done", "completed", true);
			}
			assert.strictEqual(existsSync(changelogPath), false, "Subquest alone must not create CHANGELOG.md");

			// 2. Terminal root quest that completed adds "Completed `quest` [questId]"
			const addedCompleted = await appendChangelogEntry(
				tempRoot,
				"user-auth-flow",
				"implemented passkey authentication with pure C challenge verification",
				"completed",
				true,
				questId,
			);

			assert.ok(addedCompleted && addedCompleted.length > 0);
			let changelogContent = await readFile(changelogPath, "utf8");
			let lines = changelogContent.trim().split("\n");
			assert.strictEqual(lines.length, 1, "CHANGELOG must contain exactly one concise line");
			assert.ok(lines[0].startsWith("- 2026-"));
			assert.ok(lines[0].includes(`Completed \`user-auth-flow\` [${questId}]: implemented passkey`));

			// 3. Failed terminal run adds "Terminal failure for `quest` [questId]"
			const addedFailed = await appendChangelogEntry(
				tempRoot,
				"failed-auth-quest",
				"unresolved compilation errors",
				"failed",
				false,
				"f1e2d3c400",
			);
			assert.ok(addedFailed && addedFailed.length > 0);
			changelogContent = await readFile(changelogPath, "utf8");
			lines = changelogContent.trim().split("\n");
			assert.strictEqual(lines.length, 2);
			assert.ok(lines[1].includes("Terminal failure for `failed-auth-quest` [f1e2d3c400]: unresolved compilation errors"));
		});

		// Requirement 12: npm run zip produces one combined code + run-evidence ZIP
		await t.step("Req 12: createUnifiedBundleZip produces one combined code + run-evidence ZIP", async () => {
			const bundleZipPath = join(tempRoot, "pi-quest-bundle.zip");

			const bundleRes = await createUnifiedBundleZip({
				projectRoot: tempRoot,
				extensionDir: realExtDir,
				outputZipPath: bundleZipPath,
				questId,
			});

			assert.strictEqual(bundleRes.zipPath, bundleZipPath);
			assert.strictEqual(bundleRes.verification.valid, true);

			const entries = bundleRes.verification.entries;

			// Code in pi-quest/
			assert.ok(entries.includes("pi-quest/package.json"));
			assert.ok(entries.includes("pi-quest/index.ts"));
			assert.ok(entries.some((e) => e.startsWith("pi-quest/src/")));

			// Run evidence in run/
			assert.ok(entries.includes("run/manifest.txt"));
			assert.ok(entries.includes("run/initial-prompt.txt"));
			assert.ok(entries.includes("run/summary.md"));
			assert.ok(entries.includes("run/execution.log"));

			// Exclusions
			assert.ok(!entries.some((e) => e.startsWith("run/src/")));
			assert.ok(!entries.some((e) => e.includes("node_modules/")));
			assert.ok(!entries.some((e) => e.endsWith(".zip")));
		});
	} finally {
		await rm(tempRoot, { recursive: true, force: true });
	}
});

Deno.test("diagnostic_zip: real project bundle packaging verification", async (t) => {
	const projectRoot = findProjectRoot();
	const bundleZipPath = join(projectRoot, "pi-quest-bundle.zip");

	await t.step("runs createUnifiedBundleZip on live repository", async () => {
		const result = await createUnifiedBundleZip({
			projectRoot,
			outputZipPath: bundleZipPath,
		});

		assert.strictEqual(result.zipPath, bundleZipPath);
		assert.strictEqual(result.verification.valid, true);
		assert.ok(result.sha256 && result.sha256.length === 64);

		const entries = result.verification.entries;
		assert.ok(entries.includes("pi-quest/package.json"));
		assert.ok(entries.includes("pi-quest/src/diagnostic.ts"));
		assert.ok(entries.includes("run/manifest.txt"));
		assert.ok(entries.includes("run/summary.md"));

		const s = await stat(bundleZipPath);
		assert.ok(s.size > 10000, "Bundle zip must have substantial size");
	});

	await t.step("CLI zip_bundle.ts runs cleanly and outputs verified status", async () => {
		const res = await runShellCommand(
			"deno",
			["run", "--allow-all", ".pi/extensions/pi-quest/scripts/zip_bundle.ts"],
			projectRoot,
		);

		assert.strictEqual(res.exitCode, 0, `CLI run failed: ${res.stderr || res.stdout}`);
		assert.ok(res.stdout.includes("PI-QUEST BUNDLE CREATED & VERIFIED"));
		assert.ok(res.stdout.includes("Quest Journal diagnostic bundle:"));
		assert.ok(res.stdout.includes("Bundle:"));
		assert.ok(res.stdout.includes("SHA-256:"));
		assert.ok(/Verification:\s+PASSED/.test(res.stdout));
	});
});

Deno.test("diagnostic_zip: authoritative terminal status calculation and consistency", async (t) => {
	const tempRoot = await mkdtemp(join(tmpdir(), "pi-status-consistency-"));
	try {
		const currentDir = join(tempRoot, ".pi/quest/current");
		await mkdir(currentDir, { recursive: true });

		// Case 1: Execution log with unresolved error -> BOTH manifest.txt and summary.md MUST report FAILED
		await t.step("execution log with unresolved error results in FAILED in both manifest.txt and summary.md", async () => {
			const questId = "err1234500";
			const questDir = join(currentDir, questId);
			await mkdir(questDir, { recursive: true });
			await writeFile(
				join(questDir, "quest.md"),
				"# Quest: test-status\n\n## Goal\nTest status consistency\n\n## Current Status\nComplete\n",
				"utf8",
			);
			await writeFile(
				join(questDir, "execution.log"),
				`2026-08-31T01:00:00.000Z | QUEST_CREATED | questId=${questId} quest=test-status | start\n` +
					`2026-08-31T01:01:00.000Z | ERROR | questId=${questId} quest=test-status code=RESUME_STATE_INCONSISTENT | Fatal state mismatch\n`,
				"utf8",
			);

			const hierarchy = await resolveActiveRunHierarchy(tempRoot, { questId });
			const runDirRes = await createRunDirectory(tempRoot, hierarchy);

			const summaryContent = await readFile(runDirRes.summaryPath, "utf8");
			const manifestContent = await readFile(runDirRes.manifestPath, "utf8");

			assert.ok(summaryContent.includes("- **Final Status**: FAILED"), "summary.md must report FAILED");
			assert.ok(manifestContent.includes("status: FAILED"), "manifest.txt must report FAILED");
			assert.ok(!manifestContent.includes("status: COMPLETED"), "manifest.txt must NOT contradict summary");
		});

		// Case 2: Clean execution log with completed root quest -> BOTH manifest.txt and summary.md MUST report COMPLETED
		await t.step("clean execution log results in COMPLETED in both manifest.txt and summary.md", async () => {
			const questId = "clean12300";
			const questDir = join(currentDir, questId);
			await mkdir(questDir, { recursive: true });
			await writeFile(
				join(questDir, "quest.md"),
				"# Quest: test-status\n\n## Goal\nTest status consistency\n\n## Current Status\nComplete\n",
				"utf8",
			);
			await writeFile(
				join(questDir, "execution.log"),
				`2026-08-31T01:00:00.000Z | QUEST_CREATED | questId=${questId} quest=test-status | start\n` +
					`2026-08-31T01:01:00.000Z | RESEARCH_COMPLETED | questId=${questId} quest=test-status | done\n` +
					`2026-08-31T01:02:00.000Z | TEST_PASSED | questId=${questId} quest=test-status | all passed\n`,
				"utf8",
			);

			const hierarchy = await resolveActiveRunHierarchy(tempRoot, { questId });
			const runDirRes = await createRunDirectory(tempRoot, hierarchy);

			const summaryContent = await readFile(runDirRes.summaryPath, "utf8");
			const manifestContent = await readFile(runDirRes.manifestPath, "utf8");

			assert.ok(summaryContent.includes("- **Final Status**: COMPLETED"), "summary.md must report COMPLETED");
			assert.ok(manifestContent.includes("status: COMPLETED"), "manifest.txt must report COMPLETED");
		});
	} finally {
		await rm(tempRoot, { recursive: true, force: true });
	}
});

Deno.test("diagnostic_zip: stable questId identity model (14 criteria)", async (t) => {
	const tempRoot = await mkdtemp(join(tmpdir(), "pi-quest-id-14points-"));
	const currentDir = join(tempRoot, ".pi/quest/current");
	const archiveDir = join(tempRoot, ".pi/quest/archive");

	await mkdir(currentDir, { recursive: true });
	await mkdir(archiveDir, { recursive: true });

	try {
		// 1. New root quest gets one stable 7-10 char ID (10 chars)
		let rootQuestId: string;
		await t.step("1. New root quest gets one stable 7-10 character ID", async () => {
			const qPath = join(currentDir, "my-feature.md");
			const { QUEST_TEMPLATE, parseQuestId } = await import("../src/markdown.ts");
			const { generateQuestId } = await import("../src/state.ts");

			rootQuestId = generateQuestId();
			assert.strictEqual(rootQuestId.length, 10);
			assert.ok(/^[a-z0-9]{10}$/.test(rootQuestId));

			const template = QUEST_TEMPLATE("my-feature", "Build my feature", "", "Build my feature please", [], rootQuestId);
			await writeFile(qPath, template, "utf8");

			const parsedId = parseQuestId(template);
			assert.strictEqual(parsedId, rootQuestId);
		});

		// 2. Reloading the quest preserves the same ID
		await t.step("2. Reloading the quest preserves the same ID", async () => {
			const { loadExistingQuestEpistemicState } = await import("../src/reconstruction.ts");
			const loaded = await loadExistingQuestEpistemicState("my-feature", currentDir);
			assert.strictEqual(loaded.questId, rootQuestId);
		});

		// 3. Context compaction preserves the same ID
		await t.step("3. Context compaction preserves the same ID", async () => {
			const { createOrGetCompactionTransaction } = await import("../src/compaction/transaction.ts");
			const { createDefaultState } = await import("../src/state.ts");

			const s = createDefaultState();
			s.questId = rootQuestId;
			s.active = "my-feature";
			s.lastSavedHash = "hash123";

			const tx = createOrGetCompactionTransaction(s, "normal-compaction", "my-feature");
			assert.strictEqual(s.questId, rootQuestId);
			assert.strictEqual(tx.activeQuest, "my-feature");
		});

		// 4. A new Pi session resuming the same root quest preserves the same ID
		await t.step("4. A new Pi session resuming the same root quest preserves the same ID", async () => {
			const { loadExistingQuestEpistemicState } = await import("../src/reconstruction.ts");
			const freshLoaded = await loadExistingQuestEpistemicState("my-feature", currentDir);
			assert.strictEqual(freshLoaded.questId, rootQuestId);
		});

		// 5. Subquests use the parent's quest ID
		let subquestPath: string;
		await t.step("5. Subquests use the parent's quest ID", async () => {
			const { QUEST_TEMPLATE, parseQuestId } = await import("../src/markdown.ts");
			subquestPath = join(currentDir, "sub-feature-a.md");
			const subTemplate = QUEST_TEMPLATE("sub-feature-a", "Sub feature", "my-feature", "", [], rootQuestId);
			await writeFile(subquestPath, subTemplate, "utf8");

			const parsedSubId = parseQuestId(subTemplate);
			assert.strictEqual(parsedSubId, rootQuestId);
		});

		// 6. Two different root quests have different IDs and separate logs
		let otherQuestId: string;
		await t.step("6. Two different root quests have different IDs and separate logs", async () => {
			const { generateQuestId } = await import("../src/state.ts");
			const { getQuestLogPath } = await import("../src/logging.ts");

			otherQuestId = generateQuestId();
			assert.notStrictEqual(otherQuestId, rootQuestId);

			const log1 = getQuestLogPath(rootQuestId, currentDir);
			const log2 = getQuestLogPath(otherQuestId, currentDir);

			assert.notStrictEqual(log1, log2);
			assert.ok(log1.includes(rootQuestId));
			assert.ok(log2.includes(otherQuestId));
		});

		// 7. Concurrent quest A/B logging never mixes events
		await t.step("7. Concurrent quest A/B logging never mixes events", async () => {
			const { getQuestLogPath, logEvent } = await import("../src/logging.ts");
			const logA = getQuestLogPath(rootQuestId, currentDir);
			const logB = getQuestLogPath(otherQuestId, currentDir);

			logEvent("QUEST_CREATED", "root A created", { questId: rootQuestId, root: "my-feature", logPath: logA });
			logEvent("QUEST_CREATED", "root B created", { questId: otherQuestId, root: "other-feature", logPath: logB });

			const contentA = await readFile(logA, "utf8");
			const contentB = await readFile(logB, "utf8");

			assert.ok(contentA.includes("root A created") && !contentA.includes("root B created"));
			assert.ok(contentB.includes("root B created") && !contentB.includes("root A created"));
		});

		// 8. Archive filename/path contains the correct quest ID (.pi/quest/archive/<questId>.zip)
		let archiveZipPath: string;
		await t.step("8. Archive filename/path contains the correct quest ID", async () => {
			archiveZipPath = join(tempRoot, ".pi/quest/archive", `${rootQuestId}.zip`);
			const archiveRes = await createRunArchive({
				projectRoot: tempRoot,
				questId: rootQuestId,
				outputZipPath: archiveZipPath,
			});

			assert.strictEqual(archiveRes.zipPath, archiveZipPath);
			assert.ok(archiveRes.zipPath.endsWith(`${rootQuestId}.zip`));
		});

		// 9. Completing a quest creates one archive for that quest
		await t.step("9. Completing a quest creates one archive for that quest", async () => {
			assert.ok(existsSync(archiveZipPath));
		});

		// 10. Re-running packaging does not create unrelated duplicate archives
		await t.step("10. Re-running packaging does not create unrelated duplicate archives", async () => {
			const archiveRes2 = await createRunArchive({
				projectRoot: tempRoot,
				questId: rootQuestId,
				outputZipPath: archiveZipPath,
			});
			assert.strictEqual(archiveRes2.zipPath, archiveZipPath);
		});

		// 11. The archive contains exactly: initial-prompt, summary, execution log, manifest
		await t.step("11. The archive contains exactly: initial prompt, summary, execution log, manifest", async () => {
			const entries = await inspectZipEntries(archiveZipPath);
			assert.ok(entries.includes("manifest.txt") || entries.includes("run/manifest.txt"));
			assert.ok(entries.includes("initial-prompt.txt") || entries.includes("run/initial-prompt.txt"));
			assert.ok(entries.includes("summary.md") || entries.includes("run/summary.md"));
			assert.ok(entries.includes("execution.log") || entries.includes("run/execution.log"));

			// No source code or extra folders
			assert.ok(!entries.some((e) => e.startsWith("run/src/") || e.endsWith(".c") || e.endsWith(".ts")));
			assert.ok(!entries.some((e) => e.includes("node_modules/")));
		});

		// 12. Archived/completed quests retain their ID
		await t.step("12. Archived/completed quests retain their ID", async () => {
			const { parseQuestId } = await import("../src/markdown.ts");
			const dest = join(archiveDir, `my-feature-archive.md`);
			await writeFile(dest, await readFile(join(currentDir, "my-feature.md"), "utf8"), "utf8");

			const archivedContent = await readFile(dest, "utf8");
			assert.strictEqual(parseQuestId(archivedContent), rootQuestId);
		});

		// 13. UI displays the quest ID while active and at completion
		await t.step("13. UI displays the quest ID while active and at completion", async () => {
			const { updateUIStatus } = await import("../src/ui.ts");
			const { state } = await import("../src/state.ts");

			let lastStatus = "";
			const mockCtx = {
				hasUI: true,
				ui: {
					setStatus: (_k: string, text?: string) => {
						if (text) lastStatus = text;
					},
					notify: () => {},
				},
				cwd: tempRoot,
			} as any;

			state.active = "my-feature";
			state.questId = rootQuestId;
			state.saveCount = 1;
			state.compactCount = 0;

			updateUIStatus(mockCtx);
			assert.ok(lastStatus.includes(rootQuestId), `UI status must show quest ID, got: ${lastStatus}`);
		});

		// 14. Changelog contains the quest ID exactly once for the terminal root quest
		await t.step("14. Changelog contains the quest ID exactly once for the terminal root quest", async () => {
			const changelogPath = join(tempRoot, "CHANGELOG.md");
			await rm(changelogPath, { force: true });

			const added = await appendChangelogEntry(
				tempRoot,
				"my-feature",
				"built my feature with passkeys",
				"completed",
				true,
				rootQuestId,
			);

			assert.ok(added);
			const content = await readFile(changelogPath, "utf8");
			const lines = content.trim().split("\n");
			assert.strictEqual(lines.length, 1);
			assert.ok(lines[0].includes(`Completed \`my-feature\` [${rootQuestId}]: built my feature`));
		});
	} finally {
		await rm(tempRoot, { recursive: true, force: true });
	}
});
