import { execFile } from "node:child_process";
import { createHash } from "node:crypto";
import { existsSync } from "node:fs";
import { copyFile, mkdir, mkdtemp, readFile, readdir, rm, stat, writeFile } from "node:fs/promises";
import { tmpdir } from "node:os";
import { join, relative, resolve } from "node:path";
import { promisify } from "node:util";
import { FUTURE_DIR, QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR } from "../constants.ts";
import { summarizeQuestJournalLog } from "../logging.ts";
import { fileExists } from "../paths.ts";
import { state } from "../state.ts";
import { findExtensionDir, findProjectRoot, resolveActiveRunHierarchy } from "./hierarchy.ts";
import { generateRunManifest, generateRunSummary } from "./summary.ts";
import {
	ActiveRunHierarchy,
	DiagnosticExpectedState,
	DiagnosticZipOptions,
	DiagnosticZipResult,
	RunArchiveResult,
	RunDirectoryResult,
	VerificationResult,
} from "./types.ts";

const execFileAsync = promisify(execFile);

export async function computeFileSha256(filePath: string): Promise<string> {
	const data = await readFile(filePath);
	return createHash("sha256").update(data).digest("hex");
}

export async function computeStagedFilesHash(stagingDir: string): Promise<string> {
	const hash = createHash("sha256");
	async function walk(dir: string) {
		const entries = await readdir(dir, { withFileTypes: true });
		entries.sort((a, b) => a.name.localeCompare(b.name));
		for (const entry of entries) {
			const full = join(dir, entry.name);
			if (entry.isDirectory()) {
				await walk(full);
			} else if (entry.isFile() && entry.name !== "manifest.txt") {
				const content = await readFile(full);
				hash.update(relative(stagingDir, full));
				hash.update(content);
			}
		}
	}
	await walk(stagingDir);
	return hash.digest("hex");
}

export async function runShellCommand(
	cmd: string,
	args: string[],
	cwd?: string,
): Promise<{ stdout: string; stderr: string; exitCode: number }> {
	try {
		const { stdout, stderr } = await execFileAsync(cmd, args, { cwd });
		return { stdout: stdout.toString(), stderr: stderr.toString(), exitCode: 0 };
	} catch (err: any) {
		return {
			stdout: err.stdout ? err.stdout.toString() : "",
			stderr: err.stderr ? err.stderr.toString() : (err.message || ""),
			exitCode: typeof err.code === "number" ? err.code : 1,
		};
	}
}

export async function inspectZipEntries(zipPath: string): Promise<string[]> {
	const res = await runShellCommand("unzip", ["-l", zipPath]);
	if (res.exitCode !== 0) {
		throw new Error(`Failed to inspect zip entries: ${res.stderr || res.stdout}`);
	}

	const lines = res.stdout.split(/\r?\n/);
	const entries: string[] = [];
	let inTable = false;

	for (const line of lines) {
		if (/^\s*-+\s+-+\s+-+\s+-+/.test(line)) {
			inTable = !inTable;
			continue;
		}
		if (!inTable) continue;

		const match = line.match(/^\s*\d+\s+[\d-]+\s+[\d:]+\s+(.+)$/);
		if (match && match[1]) {
			const rawEntry = match[1].trim();
			const entryName = rawEntry.replace(/^\.\//, "");
			if (entryName && entryName !== ".") {
				entries.push(entryName);
			}
		}
	}

	return entries;
}

export async function createRunDirectory(
	projectRoot: string,
	hierarchy: ActiveRunHierarchy,
	options?: {
		status?: string;
		summaryContent?: string;
		customTimestamp?: string;
		targetDir?: string;
		finalizedLogContent?: string;
		finalizedQuestContent?: string;
	},
): Promise<RunDirectoryResult> {
	const qId = hierarchy.questId || "quest";
	const currentDir = resolve(projectRoot, QUEST_CURRENT_DIR);
	const runDir = options?.targetDir || resolve(currentDir, qId);
	const questDir = runDir;

	await mkdir(runDir, { recursive: true });

	const targetLogPath = resolve(runDir, "execution.log");
	if (typeof options?.finalizedLogContent === "string") {
		await writeFile(targetLogPath, options.finalizedLogContent, "utf8");
	} else if (hierarchy.logExists && (await fileExists(hierarchy.logPath))) {
		if (resolve(hierarchy.logPath) !== targetLogPath) {
			await copyFile(hierarchy.logPath, targetLogPath);
		}
	} else if (!existsSync(targetLogPath)) {
		await writeFile(targetLogPath, "", "utf8");
	}

	const questFiles: Array<{ name: string; path: string }> = [];

	const targetQuestPath = resolve(runDir, "quest.md");
	if (typeof options?.finalizedQuestContent === "string") {
		await writeFile(targetQuestPath, options.finalizedQuestContent, "utf8");
		questFiles.push({ name: "quest.md", path: targetQuestPath });
	} else if (hierarchy.activeRootQuestPath && (await fileExists(hierarchy.activeRootQuestPath))) {
		if (resolve(hierarchy.activeRootQuestPath) !== targetQuestPath) {
			await copyFile(hierarchy.activeRootQuestPath, targetQuestPath);
		}
		questFiles.push({ name: "quest.md", path: targetQuestPath });
	}

	let logSummaryInfo: ReturnType<typeof summarizeQuestJournalLog> | null = null;
	if (await fileExists(targetLogPath)) {
		try {
			logSummaryInfo = summarizeQuestJournalLog(targetLogPath);
		} catch {}
	} else if (hierarchy.logExists && (await fileExists(hierarchy.logPath))) {
		try {
			logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
		} catch {}
	}

	const initialPromptPath = resolve(runDir, "initial-prompt.txt");
	const promptContent = hierarchy.initialPrompt || (state?.prompts && state.prompts.length > 0 ? state.prompts[0] : "") || "(none)";
	await writeFile(initialPromptPath, promptContent, "utf8");

	// L0: copy future drafts dir clearest run/future/<slug>.md
	try {
		const futSrc = resolve(projectRoot, FUTURE_DIR);
		if (existsSync(futSrc)) {
			const futEntries = await readdir(futSrc, { withFileTypes: true }).catch(() => [] as any);
			if (futEntries.length > 0) {
				const futDest = resolve(runDir, "future");
				await mkdir(futDest, { recursive: true });
				for (const e of futEntries) if (e.isFile && e.name.endsWith(".md")) {
					try { await copyFile(resolve(futSrc, e.name), resolve(futDest, e.name)); } catch {}
				}
			}
		}
		// future-archive dir from current quest run
		const archSrc = resolve(projectRoot, QUEST_CURRENT_DIR, qId, "future-archive");
		if (existsSync(archSrc)) {
			const archEntries = await readdir(archSrc, { withFileTypes: true }).catch(() => [] as any);
			if (archEntries.length > 0) {
				const archDest = resolve(runDir, "future-archive");
				await mkdir(archDest, { recursive: true });
				for (const e of archEntries) if (e.isFile && e.name.endsWith(".md")) {
					try { await copyFile(resolve(archSrc, e.name), resolve(archDest, e.name)); } catch {}
				}
			}
		}
		// compaction-resume.txt file-only
		const compSrc = resolve(projectRoot, QUEST_CURRENT_DIR, qId, "compaction-resume.txt");
		if (existsSync(compSrc)) {
			try { await copyFile(compSrc, resolve(runDir, "compaction-resume.txt")); } catch {}
		} else {
			const altComp = resolve(runDir, "compaction-resume.txt");
			if (!existsSync(altComp) && (state as any)?.pendingResume) {
				// placeholder will be written by resume.ts when needed
			}
		}
	} catch {}

	const summaryContent = options?.summaryContent || (await generateRunSummary(hierarchy, projectRoot, { status: options?.status, logSummaryInfo, questContent: options?.finalizedQuestContent }));
	const summaryPath = resolve(runDir, "summary.md");
	await writeFile(summaryPath, summaryContent, "utf8");

	const manifestPath = resolve(runDir, "manifest.txt");
	const manifestContent = generateRunManifest(hierarchy, options?.customTimestamp, projectRoot, {
		zipPath: resolve(projectRoot, QUEST_ARCHIVE_DIR, `${qId}.zip`),
		status: options?.status,
		logSummaryInfo,
	});
	await writeFile(manifestPath, manifestContent, "utf8");

	return {
		questId: qId,
		runDir,
		summaryPath,
		manifestPath,
		initialPromptPath,
		logPath: targetLogPath,
		questDir,
		questFiles,
	};
}

async function copyExtensionDirectory(sourceDir: string, targetDir: string): Promise<void> {
	await mkdir(targetDir, { recursive: true });
	const entries = await readdir(sourceDir, { withFileTypes: true });

	for (const entry of entries) {
		const name = entry.name;

		if (
			name.endsWith(".zip") ||
			name === "node_modules" ||
			name === ".git" ||
			name === ".DS_Store" ||
			name === "docs" ||
			name === ".pi" ||
			name === ".cache" ||
			name === "debug" ||
			name === "build"
		) {
			continue;
		}

		const srcPath = join(sourceDir, name);
		const destPath = join(targetDir, name);

		if (entry.isDirectory()) {
			await copyExtensionDirectory(srcPath, destPath);
		} else if (entry.isFile()) {
			await copyFile(srcPath, destPath);
		}
	}
}

export async function verifyRunArchive(
	zipPath: string,
	expected: DiagnosticExpectedState,
): Promise<VerificationResult> {
	const entries = await inspectZipEntries(zipPath);
	const errors: string[] = [];

	const hasManifest = entries.includes("run/manifest.txt") || entries.includes("manifest.txt");
	if (!hasManifest) {
		errors.push("Manifest file is missing from run archive (run/manifest.txt)");
	}

	const hasInitialPrompt = entries.includes("run/initial-prompt.txt") || entries.includes("initial-prompt.txt");
	if (!hasInitialPrompt) {
		errors.push("Initial prompt file is missing from run archive (run/initial-prompt.txt)");
	}

	const hasSummary = entries.includes("run/summary.md") || entries.includes("summary.md");
	if (!hasSummary) {
		errors.push("Summary markdown file is missing from run archive (run/summary.md)");
	}

	if (expected.logExists) {
		const logEntry = "run/execution.log";
		const altEntry1 = "run/run.log";
		const altEntry2 = "execution.log";
		if (!entries.includes(logEntry) && !entries.includes(altEntry1) && !entries.includes(altEntry2)) {
			errors.push(`Execution log exists on disk but is missing from archive (${logEntry})`);
		}
	}

	const nestedZips = entries.filter((e) => e.endsWith(".zip"));
	if (nestedZips.length > 0) {
		errors.push(`Archive contains recursive zip file(s): ${nestedZips.join(", ")}`);
	}

	const nodeModules = entries.filter((e) => e.includes("node_modules/"));
	if (nodeModules.length > 0) {
		errors.push(`Archive contains node_modules: ${nodeModules.slice(0, 3).join(", ")}`);
	}

	return {
		valid: errors.length === 0,
		errors,
		entries,
	};
}

export async function verifyDiagnosticZip(
	zipPath: string,
	expected: DiagnosticExpectedState,
): Promise<VerificationResult> {
	const entries = await inspectZipEntries(zipPath);
	const errors: string[] = [];

	if (!entries.includes("pi-quest/package.json") && !entries.includes("pi-quest/index.ts")) {
		errors.push("pi-quest extension source files are missing from bundle (pi-quest/package.json)");
	}

	const hasManifest =
		entries.includes("run/manifest.txt") ||
		entries.includes("diagnostic/current-run/manifest.txt") ||
		entries.includes("manifest.txt");
	if (!hasManifest) {
		errors.push("Manifest file is missing from archive (run/manifest.txt)");
	}

	const hasInitialPrompt = entries.includes("run/initial-prompt.txt") || entries.includes("initial-prompt.txt");
	if (!hasInitialPrompt) {
		errors.push("Initial prompt file is missing from archive (run/initial-prompt.txt)");
	}

	const hasSummary = entries.includes("run/summary.md") || entries.includes("summary.md");
	if (!hasSummary) {
		errors.push("Summary markdown file is missing from archive (run/summary.md)");
	}

	if (expected.logExists) {
		const logEntry = "run/execution.log";
		const legacyLogEntry = "diagnostic/current-run/run.log";
		if (!entries.includes(logEntry) && !entries.includes(legacyLogEntry)) {
			errors.push(`Execution log exists on disk but is missing from archive (${logEntry})`);
		}
	}

	const nestedZips = entries.filter((e) => e.endsWith(".zip"));
	if (nestedZips.length > 0) {
		errors.push(`Archive contains recursive zip file(s): ${nestedZips.join(", ")}`);
	}

	const staleArchives = entries.filter(
		(e) =>
			(!e.startsWith("pi-quest/") && e.includes("diagnostic/archive")) ||
			e.startsWith("archive/") ||
			(e.includes("/archive/") && !e.startsWith("pi-quest/")),
	);
	if (staleArchives.length > 0) {
		errors.push(`Archive contains historical/stale archive files: ${staleArchives.join(", ")}`);
	}

	const nodeModules = entries.filter((e) => e.includes("node_modules/"));
	if (nodeModules.length > 0) {
		errors.push(`Archive contains node_modules: ${nodeModules.slice(0, 3).join(", ")}`);
	}

	return {
		valid: errors.length === 0,
		errors,
		entries,
	};
}

export const verifyZipContents = verifyDiagnosticZip;

export async function createRunArchive(
	options: {
		projectRoot?: string;
		questId?: string;
		outputZipPath?: string;
		skipVerification?: boolean;
		status?: string;
		hierarchy?: ActiveRunHierarchy | null;
		finalizedLogContent?: string;
		finalizedQuestContent?: string;
	} = {},
): Promise<RunArchiveResult> {
	const projectRoot = findProjectRoot(options.projectRoot);
	const targetId = options.questId;
	const hierarchy = options.hierarchy || (await resolveActiveRunHierarchy(projectRoot, { questId: targetId }));
	const qId = targetId || hierarchy.questId || "quest";
	const archiveDir = resolve(projectRoot, QUEST_ARCHIVE_DIR);
	await mkdir(archiveDir, { recursive: true });
	const outputZipPath = resolve(options.outputZipPath || join(archiveDir, `${qId}.zip`));

	const stagingDir = await mkdtemp(join(tmpdir(), `pi-quest-run-archive-${qId}-`));

	try {
		const currentRunDir = resolve(projectRoot, QUEST_CURRENT_DIR, qId);
		const targetDir = (options.finalizedLogContent || options.finalizedQuestContent || !existsSync(currentRunDir))
			? stagingDir
			: currentRunDir;

		const runDirRes = await createRunDirectory(projectRoot, hierarchy, {
			status: options.status,
			targetDir,
			finalizedLogContent: options.finalizedLogContent,
			finalizedQuestContent: options.finalizedQuestContent,
		});

		const summaryContent = await readFile(runDirRes.summaryPath, "utf8");
		const manifestContent = await readFile(runDirRes.manifestPath, "utf8");

		if (targetDir !== stagingDir) {
			await copyFile(runDirRes.manifestPath, join(stagingDir, "manifest.txt"));
			await copyFile(runDirRes.initialPromptPath, join(stagingDir, "initial-prompt.txt"));
			await copyFile(runDirRes.summaryPath, join(stagingDir, "summary.md"));
			if (await fileExists(runDirRes.logPath)) {
				await copyFile(runDirRes.logPath, join(stagingDir, "execution.log"));
			}
		}

		if (await fileExists(outputZipPath)) {
			await rm(outputZipPath, { force: true });
		}

		const zipRes = await runShellCommand("zip", ["-r", outputZipPath, "."], stagingDir);
		if (zipRes.exitCode !== 0) {
			throw new Error(`Failed to create run archive zip: ${zipRes.stderr || zipRes.stdout}`);
		}

		const sha256 = await computeFileSha256(outputZipPath);
		const verification = await verifyRunArchive(outputZipPath, {
			questId: qId,
			activeRootQuest: hierarchy.activeRootQuest,
			capturedSubQuests: hierarchy.capturedSubQuests.map((s) => s.name),
			logExists: hierarchy.logExists || Boolean(options.finalizedLogContent),
		});

		if (!options.skipVerification && !verification.valid) {
			throw new Error(`Run archive verification failed: ${verification.errors.join("; ")}`);
		}

		return {
			zipPath: outputZipPath,
			questId: qId,
			hierarchy,
			summary: summaryContent,
			manifest: manifestContent,
			sha256,
			runDir: runDirRes.runDir,
			verification,
		};
	} finally {
		await rm(stagingDir, { recursive: true, force: true });
	}
}

export async function createUnifiedBundleZip(
	options: DiagnosticZipOptions = {},
): Promise<DiagnosticZipResult> {
	const projectRoot = findProjectRoot(options.projectRoot);
	const extensionDir = findExtensionDir(projectRoot, options.extensionDir);
	const outputZipPath = resolve(options.outputZipPath || join(projectRoot, "pi-quest-bundle.zip"));

	const targetId = options.questId;
	const hierarchy = await resolveActiveRunHierarchy(projectRoot, { questId: targetId });
	const qId = targetId || hierarchy.questId || "quest";

	const stagingDir = await mkdtemp(join(tmpdir(), `pi-quest-bundle-${qId}-`));

	try {
		const extStagingDir = join(stagingDir, "pi-quest");
		const runStagingDir = join(stagingDir, "run");

		await mkdir(extStagingDir, { recursive: true });
		await mkdir(runStagingDir, { recursive: true });

		const runDirRes = await createRunDirectory(projectRoot, hierarchy, {
			customTimestamp: options.timestamp,
			targetDir: runStagingDir,
		});

		// 1. Copy extension source files into pi-quest/
		await copyExtensionDirectory(extensionDir, extStagingDir);

		// 2. Ensure run files exist in run/
		if (runDirRes.manifestPath !== join(runStagingDir, "manifest.txt")) {
			await copyFile(runDirRes.manifestPath, join(runStagingDir, "manifest.txt"));
		}
		if (runDirRes.initialPromptPath !== join(runStagingDir, "initial-prompt.txt")) {
			await copyFile(runDirRes.initialPromptPath, join(runStagingDir, "initial-prompt.txt"));
		}
		if (runDirRes.summaryPath !== join(runStagingDir, "summary.md")) {
			await copyFile(runDirRes.summaryPath, join(runStagingDir, "summary.md"));
		}
		if (await fileExists(runDirRes.logPath) && runDirRes.logPath !== join(runStagingDir, "execution.log")) {
			await copyFile(runDirRes.logPath, join(runStagingDir, "execution.log"));
		}

		const bundleContentHash = await computeStagedFilesHash(stagingDir);

		if (await fileExists(outputZipPath)) {
			await rm(outputZipPath, { force: true });
		}

		const zipRes = await runShellCommand("zip", ["-r", outputZipPath, "."], stagingDir);
		if (zipRes.exitCode !== 0) {
			throw new Error(`Failed to create bundle zip: ${zipRes.stderr || zipRes.stdout}`);
		}

		const zipSha256 = await computeFileSha256(outputZipPath);
		const verification = await verifyDiagnosticZip(outputZipPath, {
			questId: qId,
			activeRootQuest: hierarchy.activeRootQuest,
			capturedSubQuests: hierarchy.capturedSubQuests.map((s) => s.name),
			logExists: hierarchy.logExists,
		});

		if (!options.skipVerification && !verification.valid) {
			await rm(outputZipPath, { force: true });
			throw new Error(
				`Bundle zip verification failed:\n${verification.errors.map((e) => `  - ${e}`).join("\n")}`,
			);
		}

		const manifestContent = await readFile(runDirRes.manifestPath, "utf8");

		return {
			zipPath: outputZipPath,
			hierarchy,
			manifest: manifestContent,
			verification,
			sha256: zipSha256,
			bundleHash: bundleContentHash,
		};
	} finally {
		await rm(stagingDir, { recursive: true, force: true });
	}
}

export const createDiagnosticZip = createUnifiedBundleZip;
