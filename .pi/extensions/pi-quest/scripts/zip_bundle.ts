#!/usr/bin/env -S deno run --allow-all
import { relative } from "node:path";
import { createDiagnosticZip, findProjectRoot } from "../src/diagnostic.ts";

function printLine(msg = ""): void {
	process.stdout.write(`${msg}\n`);
}

function printError(msg = ""): void {
	process.stderr.write(`${msg}\n`);
}

function parseQuestArg(): string | undefined {
	const args = process.argv.slice(2);
	for (let i = 0; i < args.length; i++) {
		const arg = args[i];
		if (arg.startsWith("--quest=")) {
			return arg.slice("--quest=".length);
		}
		if (arg === "--quest" && i + 1 < args.length) {
			return args[i + 1];
		}
		if (arg.startsWith("--id=")) {
			return arg.slice("--id=".length);
		}
		if (arg === "--id" && i + 1 < args.length) {
			return args[i + 1];
		}
	}
	return undefined;
}

async function main() {
	printLine("Packaging unified pi-quest code + diagnostic bundle...");
	try {
		const root = findProjectRoot();
		const questId = parseQuestArg();
		const result = await createDiagnosticZip({ projectRoot: root, questId });

		const rel = relative(root, result.zipPath);
		const displayPath = rel.startsWith(".") ? rel : `./${rel}`;

		printLine("\n================================================================================");
		printLine("                     PI-QUEST BUNDLE CREATED & VERIFIED");
		printLine("================================================================================");
		printLine("Quest Journal diagnostic bundle:");
		printLine(`${displayPath}`);
		printLine(`Quest ID:             ${result.hierarchy.questId || "(none)"}`);
		printLine(`Root quest:           ${result.hierarchy.activeRootQuest || "(none)"}`);
		printLine(`Bundle:               ${displayPath}`);
		printLine(`SHA-256:              ${result.sha256}`);
		printLine(`Quest State Hash:     ${result.hierarchy.questHash || "(none)"}`);
		if (result.bundleHash) {
			printLine(`Bundle Content SHA:   ${result.bundleHash}`);
		}
		printLine(`Current Code:         pi-quest/ (package.json, src/, tests/, scripts/)`);
		printLine(`Active Sub-Quest:     ${result.hierarchy.activeSubQuest || "(none)"}`);
		printLine(
			`Captured Sub-Quests:  ${
				result.hierarchy.capturedSubQuests.length > 0
					? result.hierarchy.capturedSubQuests.map((s) => s.name).join(", ")
					: "(none)"
			}`,
		);
		printLine(`Run Start Time:       ${result.hierarchy.startTime || "(none)"}`);
		printLine(`Run End Time:         ${result.hierarchy.endTime || "(none)"}`);
		printLine(`Resolution Method:    ${result.hierarchy.resolutionMethod}`);
		printLine(
			`Confidence/Ambiguity: ${
				result.hierarchy.confidence === "ambiguous"
					? `AMBIGUOUS (${result.hierarchy.ambiguityDetails || "undetermined"})`
					: result.hierarchy.confidence.toUpperCase()
			}`,
		);
		printLine(
			`Execution Log:        ${
				result.hierarchy.logExists
					? `${result.hierarchy.logPath} (${((result.hierarchy.logSize || 0) / 1024).toFixed(1)} KB)`
					: "Not found (no run recorded)"
			}`,
		);
		printLine(`Verification:         PASSED (all ${result.verification.entries.length} archive entries verified)`);
		printLine("================================================================================\n");
		printLine(`Upload or inspect ${displayPath} for current code + latest run diagnostics.\n`);
	} catch (err: any) {
		printError("\n❌ Bundle zip packaging failed:");
		printError(err?.message || err);
		process.exit(1);
	}
}

if (import.meta.main || !globalThis.Deno) {
	main();
}
