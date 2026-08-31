import { existsSync } from "node:fs";
import { readFile, writeFile } from "node:fs/promises";
import { resolve } from "node:path";
import { summarizeQuestJournalLog } from "../logging.ts";
import { ActiveRunHierarchy } from "./types.ts";

export function calculateAuthoritativeTerminalStatus(
	hierarchy: ActiveRunHierarchy,
	logSummaryInfo?: ReturnType<typeof summarizeQuestJournalLog> | null,
	explicitStatus?: string,
): "COMPLETED" | "FAILED" | "IDLE" {
	const normExplicit = (explicitStatus || "").toLowerCase();
	if (normExplicit === "failed" || normExplicit === "failure" || normExplicit === "terminal_failure") {
		return "FAILED";
	}
	if (normExplicit === "completed" || normExplicit === "complete" || normExplicit === "success") {
		return "COMPLETED";
	}

	if (logSummaryInfo) {
		if (logSummaryInfo.hasUnresolvedError || logSummaryInfo.hasCriticalReviewFailure) {
			return "FAILED";
		}
		if (logSummaryInfo.testVerification && logSummaryInfo.testVerification.status === "FAILED") {
			return "FAILED";
		}
		if (logSummaryInfo.unrecoveredFailures && logSummaryInfo.unrecoveredFailures.length > 0) {
			return "FAILED";
		}
		if (logSummaryInfo.implementationAttempts > 0 && logSummaryInfo.implementationAllowedCount === 0 && logSummaryInfo.implementationBlockedCount > 0) {
			return "FAILED";
		}
		if (
			logSummaryInfo.failures &&
			logSummaryInfo.failures.some(
				(f) =>
					!f.recovered &&
					(f.type === "ERROR" ||
						f.type === "RESUME_FAILED" ||
						f.type === "RECOVERY_FAILED" ||
						f.type === "COMPACTION_FAILED" ||
						f.type === "COMPACTION_INCONSISTENT" ||
						f.type === "SAVE_FAILED" ||
						f.type === "TEST_FAILED" ||
						f.type === "BUILD_FAILED" ||
						f.type === "TEST_FAILURE" ||
						f.type === "CRITICAL_REVIEW_FAILED" ||
						f.type === "CRITICAL_REVIEW_UNCERTAIN" ||
						f.type === "CRITICAL_REVIEW_ERROR"),
			)
		) {
			return "FAILED";
		}
	}

	if (hierarchy.activeRootQuest) {
		return "COMPLETED";
	}

	return "IDLE";
}

export async function appendChangelogEntry(
	projectRoot: string,
	rootQuest: string,
	summaryOrGoal: string,
	status = "completed",
	isCompleted?: boolean,
	questId?: string,
): Promise<string | null> {
	const changelogPath = resolve(projectRoot, "CHANGELOG.md");
	const dateStr = new Date().toISOString().slice(0, 10);

	const cleanSummary = summaryOrGoal
		.replace(/^#+.*$/gm, "")
		.replace(/[\r\n]+/g, " ")
		.replace(/\s+/g, " ")
		.trim()
		.slice(0, 140);

	const normStatus = (status || "").toLowerCase();
	const completed = typeof isCompleted === "boolean" ? isCompleted : normStatus === "completed";
	const idTag = questId ? ` [${questId}]` : "";
	const prefix = completed ? `Completed \`${rootQuest}\`${idTag}` : `Terminal failure for \`${rootQuest}\`${idTag}`;
	const entry = `- ${dateStr} — ${prefix}${cleanSummary ? `: ${cleanSummary}` : ""}.\n`;

	try {
		let currentContent = "";
		if (existsSync(changelogPath)) {
			currentContent = await readFile(changelogPath, "utf8");
		}
		if (!currentContent.includes(entry.trim())) {
			await writeFile(changelogPath, currentContent ? `${currentContent.trimEnd()}\n${entry}` : entry, "utf8");
		}
		return entry.trim();
	} catch {
		return null;
	}
}
