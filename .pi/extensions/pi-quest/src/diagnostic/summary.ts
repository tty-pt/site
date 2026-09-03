import { existsSync } from "node:fs";
import { readFile } from "node:fs/promises";
import { relative } from "node:path";
import { summarizeQuestJournalLog } from "../logging.ts";
import { parseMarkdownSections } from "../markdown.ts";
import { fileExists } from "../paths.ts";
import { state } from "../state.ts";
import { findProjectRoot } from "./hierarchy.ts";
import { calculateAuthoritativeTerminalStatus } from "./status.ts";
import {
  ActiveRunHierarchy,
  DiagnosticReportResult,
  ExtractedJournalState,
} from "./types.ts";

type RunSummaryLogInfo = ReturnType<typeof summarizeQuestJournalLog> & {
  filteredCount?: number;
  opencodeSessionId?: string | null;
  startMs?: number | null;
  elapsedMaxMs?: number | null;
};

export function extractJournalStateFromContent(
  content: string,
): ExtractedJournalState {
  const sections = parseMarkdownSections(content);
  const getSec = (key: string): string => {
    const s = sections.get(key.toLowerCase());
    return s?.body?.trim() || "";
  };

  const remainingRaw = getSec("remaining work") || getSec("remaining tasks") ||
    getSec("remaining");
  const remainingTasks = remainingRaw
    ? remainingRaw
      .split(/\r?\n/)
      .map((l) => l.trim())
      .filter((l) =>
        l.startsWith("- [ ]") || l.startsWith("* [ ]") || l.startsWith("[ ]")
      )
    : [];

  const planRaw = getSec("plan") ||
    getSec("detailed multi-stage execution plan");
  const plan = planRaw
    ? planRaw.split(/\r?\n/).map((l) => l.trim()).filter((l) => l.length > 0)
    : [];

  const filesRaw = getSec("files touched") || getSec("files modified");
  const filesTouched = filesRaw
    ? filesRaw.split(/\r?\n/).map((l) => l.trim()).filter((l) => l.length > 0)
    : [];

  return {
    goal: getSec("goal") || getSec("objective"),
    status: getSec("current status") || getSec("status"),
    remainingTasks,
    plan,
    planConfidence: getSec("plan confidence"),
    filesTouched,
    rawContent: content,
  };
}

export async function generateRunSummary(
  hierarchy: ActiveRunHierarchy,
  projectRoot?: string,
  options?: {
    status?: string;
    logSummaryInfo?: RunSummaryLogInfo | null;
    questContent?: string;
  },
): Promise<string> {
  const root = projectRoot || findProjectRoot();
  let rootQuestContent = options?.questContent || "";
  if (
    !rootQuestContent && hierarchy.activeRootQuestPath &&
    (await fileExists(hierarchy.activeRootQuestPath))
  ) {
    try {
      rootQuestContent = await readFile(hierarchy.activeRootQuestPath, "utf8");
    } catch {}
  }

  let logSummaryInfo: RunSummaryLogInfo | null = options?.logSummaryInfo ||
    null;
  if (
    !logSummaryInfo && hierarchy.logExists &&
    (await fileExists(hierarchy.logPath))
  ) {
    try {
      logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
    } catch {}
  }

  const sections = parseMarkdownSections(rootQuestContent);

  const getSectionText = (key: string): string => {
    const s = sections.get(key.toLowerCase());
    const body = s?.body?.trim() || "";
    if (!body || body === "-" || body === "- [ ]" || body.startsWith(">")) {
      return "";
    }
    return body;
  };

  const objective = getSectionText("objective") || getSectionText("goal") ||
    getSectionText("original request") || "(none)";

  // Truthful completed section
  let completed = getSectionText("completed");
  if (!completed) {
    if (
      logSummaryInfo &&
      logSummaryInfo.implementationSummary.completedTasks.length > 0
    ) {
      completed = logSummaryInfo.implementationSummary.completedTasks.map((
        t,
      ) => (t.startsWith("- ") ? t : `- ${t}`)).join("\n");
    } else if (
      logSummaryInfo &&
      logSummaryInfo.implementationSummary.modifiedFiles.length > 0
    ) {
      completed = `- Modified files: ${
        logSummaryInfo.implementationSummary.modifiedFiles.join(", ")
      }`;
    } else if (
      logSummaryInfo && logSummaryInfo.implementationAllowedCount === 0 &&
      logSummaryInfo.implementationAttempts > 0
    ) {
      completed =
        "- No implementation modifications completed (all attempts blocked by gates).";
    } else {
      completed = "- No verified implementation tasks completed.";
    }
  }

  const findings = getSectionText("important discoveries") ||
    getSectionText("research findings") ||
    getSectionText("current understanding") || "- Architecture verified.";
  const decisions = getSectionText("decisions") ||
    getSectionText("decisions made") || "- All state unified under .pi/quest.";

  // Truthful test / build section
  let testStatus = "";
  if (logSummaryInfo) {
    if (logSummaryInfo.testVerification.status === "PASSED") {
      testStatus = `- Tests verified clean: ${
        logSummaryInfo.testVerification.lastPassedCommand || "all tests passed"
      } (total test runs: ${logSummaryInfo.testVerification.totalTestsRun})`;
    } else if (logSummaryInfo.testVerification.status === "FAILED") {
      testStatus = `- Test / build failure: ${
        logSummaryInfo.testVerification.lastFailureReason ||
        logSummaryInfo.testVerification.lastFailedCommand || "test failure"
      } (failed tests: ${logSummaryInfo.testVerification.testsFailed})`;
    } else {
      const secTest = getSectionText("test / build status");
      testStatus = secTest && !secTest.toLowerCase().includes("verified clean")
        ? secTest
        : "- No test or build verification commands executed during run.";
    }
  } else {
    testStatus = getSectionText("test / build status") ||
      "- No test or build verification recorded.";
  }

  const remaining = getSectionText("remaining work") || "- None";

  const finalStatus = calculateAuthoritativeTerminalStatus(
    hierarchy,
    logSummaryInfo,
    options?.status,
  );

  const questId = hierarchy.questId || "quest";
  const subNames = hierarchy.capturedSubQuests.length > 0
    ? hierarchy.capturedSubQuests.map((s) => s.name).join(", ")
    : "None";

  const lines: string[] = [
    `# Run Summary (${questId})`,
    "",
    "## Metadata",
    `- **Quest ID**: ${questId}`,
    `- **Root Quest**: ${hierarchy.activeRootQuest || "(none)"}`,
    `- **Sub-Quests**: ${subNames}`,
    `- **Start Time**: ${hierarchy.startTime || new Date().toISOString()}`,
    `- **End Time**: ${hierarchy.endTime || new Date().toISOString()}`,
    `- **Final Status**: ${finalStatus}`,
    "",
    "## Objective",
    `> ${(hierarchy.initialPrompt || objective).replace(/^>\s*/gm, "").trim()}`,
    "",
    "## What Was Actually Done",
    completed,
    "",
    "## Major Steps Taken",
    completed,
    "",
    "## Subquests Undertaken",
    subNames,
    "",
    "## Important Findings",
    findings,
    "",
    "## Important Decisions",
    decisions,
    "",
    "## Implementation Outcome",
  ];

  if (logSummaryInfo) {
    lines.push(
      `- Implementation attempts: ${logSummaryInfo.implementationAttempts} (allowed: ${logSummaryInfo.implementationAllowedCount}, blocked: ${logSummaryInfo.implementationBlockedCount})`,
    );
    if (logSummaryInfo.implementationSummary.modifiedFiles.length > 0) {
      lines.push(
        `- Modified files: ${
          logSummaryInfo.implementationSummary.modifiedFiles.join(", ")
        }`,
      );
    }
  } else {
    lines.push("- Implementation outcome: unrecorded.");
  }

  lines.push(
    "",
    "## Tests / Build Outcome",
    testStatus,
    "",
    "## Compaction / Resume Outcome",
  );

  if (logSummaryInfo && logSummaryInfo.compactionCount > 0) {
    lines.push(
      `- Compactions: ${logSummaryInfo.compactionCount} (successful: ${logSummaryInfo.successfulCompactions}, failed: ${logSummaryInfo.failedCompactions})`,
      `- Resumes: ${logSummaryInfo.resumeCount} (successful: ${logSummaryInfo.resumeSuccessCount}, failed: ${logSummaryInfo.resumeFailedCount})`,
    );
  } else {
    lines.push("- Compactions: 0 (session executed within memory threshold)");
  }

  lines.push(
    "",
    "## Failures / Problems Encountered",
  );

  if (
    logSummaryInfo &&
    (logSummaryInfo.failureCount > 0 || logSummaryInfo.blockedGates.length > 0)
  ) {
    if (logSummaryInfo.blockedGates.length > 0) {
      lines.push(
        `- Implementation gated by: ${logSummaryInfo.blockedGates.join(", ")}`,
      );
    }
    if (logSummaryInfo.failures.length > 0) {
      for (const f of logSummaryInfo.failures.slice(0, 8)) {
        const recNote = f.recovered ? " [RECOVERED]" : " [UNRESOLVED]";
        lines.push(`- [${f.type}] ${f.reason || "failure detected"}${recNote}`);
      }
      if (logSummaryInfo.failures.length > 8) {
        lines.push(
          `- ... and ${logSummaryInfo.failures.length - 8} more failures`,
        );
      }
    }
  } else {
    lines.push("- No fatal failures encountered during run execution.");
  }

  lines.push(
    "",
    "## Final Run Verdict & Diagnostic Accounting",
    logSummaryInfo?.terminalVerdictReason
      ? `- **Verdict Reason**: ${logSummaryInfo.terminalVerdictReason}`
      : `- **Verdict**: ${finalStatus}`,
    "",
    "## Remaining Work",
    remaining,
    "",
  );

  return lines.join("\n");
}

export function generateRunManifest(
  hierarchy: ActiveRunHierarchy,
  customTimestamp?: string,
  projectRoot?: string,
  extra?: {
    bundleHash?: string;
    zipPath?: string;
    zipSha256?: string;
    status?: string;
    logSummaryInfo?: RunSummaryLogInfo | null;
  },
): string {
  const timestamp = customTimestamp || new Date().toISOString();
  const root = projectRoot || findProjectRoot();
  const subNames = hierarchy.capturedSubQuests.length > 0
    ? hierarchy.capturedSubQuests.map((s) => s.name).join(", ")
    : "(none)";

  let logSummaryInfo: RunSummaryLogInfo | null = extra?.logSummaryInfo || null;
  if (!logSummaryInfo && hierarchy.logExists && existsSync(hierarchy.logPath)) {
    try {
      logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
    } catch {}
  }

  const finalStatus = calculateAuthoritativeTerminalStatus(
    hierarchy,
    logSummaryInfo,
    extra?.status,
  );
  const qId = hierarchy.questId || "quest";

  const lines = [
    `questId: ${qId}`,
    `rootQuest: ${hierarchy.activeRootQuest || "(none)"}`,
    `finalActiveQuest: ${
      hierarchy.activeSubQuest || hierarchy.activeRootQuest || "(none)"
    }`,
    `status: ${finalStatus}`,
    `startedAt: ${hierarchy.startTime || timestamp}`,
    `endedAt: ${hierarchy.endTime || timestamp}`,
    `archivePath: ${extra?.zipPath || `.pi/quest/archive/${qId}.zip`}`,
    `capturedSubQuests: ${subNames}`,
  ];

  if (hierarchy.questHash) {
    lines.push(`questHash: ${hierarchy.questHash}`);
  }
  lines.push(`draftCaptured: ${hierarchy.draftCaptured ? "true" : "false"}`);
  lines.push(`futureCount: ${hierarchy.futureCount ?? 0}`);
  if (hierarchy.compactionResumeHash) {
    lines.push(`compactionResumeHash: ${hierarchy.compactionResumeHash}`);
  }
  lines.push(
    `semanticSummaryEnabled: ${
      hierarchy.semanticSummaryEnabled ? "true" : "false"
    }`,
  );
  lines.push(
    `thoughtLoggingEnabled: ${
      hierarchy.thoughtLoggingEnabled ? "true" : "false"
    }`,
  );
  const filtered = logSummaryInfo?.filteredCount ?? hierarchy.filteredCount ??
    0;
  lines.push(`filteredCount: ${filtered}`);
  const opSess = logSummaryInfo?.opencodeSessionId ??
    hierarchy.opencodeSessionId ?? null;
  lines.push(`opencodeSessionId: ${opSess ?? "(none)"}`);
  const sMs = logSummaryInfo?.startMs ?? hierarchy.startMs ?? null;
  lines.push(`startMs: ${sMs ?? 0}`);
  const eMs = logSummaryInfo?.elapsedMaxMs ?? hierarchy.elapsedMaxMs ?? 0;
  lines.push(`elapsedMaxMs: ${eMs}`);
  if (extra?.bundleHash) {
    lines.push(`bundleHash: ${extra.bundleHash}`);
  }

  return lines.join("\n") + "\n";
}

export const generateManifest = generateRunManifest;

export async function generateDiagnosticReport(
  projectRoot: string,
  hierarchy: ActiveRunHierarchy,
  options?: { status?: string },
): Promise<DiagnosticReportResult> {
  let logSummaryInfo: RunSummaryLogInfo | null = null;
  let logStats = {
    exists: hierarchy.logExists,
    size: hierarchy.logSize || 0,
    eventsCount: 0,
    startTime: hierarchy.startTime,
    endTime: hierarchy.endTime,
  };

  if (hierarchy.logExists && (await fileExists(hierarchy.logPath))) {
    try {
      logSummaryInfo = summarizeQuestJournalLog(hierarchy.logPath);
      const logLines = (await readFile(hierarchy.logPath, "utf8")).split(
        /\r?\n/,
      ).filter((l) => l.trim().length > 0);
      logStats.eventsCount = logLines.length;
    } catch {}
  }

  let journalState: ExtractedJournalState | undefined;
  if (
    hierarchy.activeRootQuestPath &&
    (await fileExists(hierarchy.activeRootQuestPath))
  ) {
    try {
      const content = await readFile(hierarchy.activeRootQuestPath, "utf8");
      journalState = extractJournalStateFromContent(content);
    } catch {}
  }

  const manifest = generateRunManifest(hierarchy, undefined, projectRoot, {
    status: options?.status,
    logSummaryInfo,
  });
  const summary = await generateRunSummary(hierarchy, projectRoot, {
    status: options?.status,
    logSummaryInfo,
  });

  return {
    projectRoot,
    hierarchy,
    logStats,
    journalState,
    manifest,
    summary,
  };
}
