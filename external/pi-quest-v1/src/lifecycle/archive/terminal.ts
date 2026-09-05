import { existsSync } from "node:fs";
import { readFile, writeFile } from "node:fs/promises";
import {
  calculateAuthoritativeTerminalStatus,
  findProjectRoot,
  resolveActiveRunHierarchy,
} from "../../diagnostic.ts";
import {
  logImplementationOutcome,
  logSubquestTransition,
  summarizeQuestJournalLog,
} from "../../logging.ts";
import { spliceMarkdownSections } from "../../markdown.ts";
import { questLogPath } from "../../paths.ts";
import { verifyAndMarkSaved } from "../../persistence.ts";
import { onLifecycleStageTransition } from "../../lifecycle.ts";
import type { ExtensionAPI, ExtensionContext } from "../../types.ts";

export async function commitTerminalState(
  path: string,
  questContent: string,
): Promise<string> {
  const terminalUpdates = new Map<string, string>();
  terminalUpdates.set("current status", "Completed");
  terminalUpdates.set("final status", "COMPLETED");
  terminalUpdates.set("exact next action", "None");
  terminalUpdates.set("remaining work", "- [x] All tasks completed");
  const terminalQuestContent = spliceMarkdownSections(
    questContent,
    terminalUpdates,
  );
  await writeFile(path, terminalQuestContent, "utf8");
  return terminalQuestContent;
}

export async function verifyTerminalState(
  pi: ExtensionAPI,
  ctx: ExtensionContext | undefined,
  questName: string,
  questContent: string,
  path: string,
): Promise<
  { verifiedLogContent: string; verifiedQuestContent: string; error?: string }
> {
  logImplementationOutcome(
    "IMPLEMENTATION_COMPLETED",
    `quest '${questName}' completed successfully`,
    { quest: questName, status: "COMPLETED" },
  );
  logSubquestTransition("ARCHIVE", `archived quest ${questName}`, {
    quest: questName,
    subquest: questName,
    dest: "",
    status: "COMPLETED",
  });

  const saveVerification = await verifyAndMarkSaved(pi, ctx, questName);
  if (!saveVerification.success) {
    return {
      verifiedLogContent: "",
      verifiedQuestContent: questContent,
      error:
        `Failed to durably verify terminal quest state on disk: ${saveVerification.error}`,
    };
  }

  const targetQid = questName; // caller will provide qid separately if needed
  let verifiedLogContent = "";
  let verifiedQuestContent = questContent;
  try {
    verifiedLogContent = await readFile(questLogPath(targetQid), "utf8");
  } catch {
    verifiedLogContent = "";
  }
  try {
    verifiedQuestContent = await readFile(path, "utf8");
  } catch {
    verifiedQuestContent = questContent;
  }
  return { verifiedLogContent, verifiedQuestContent };
}

export async function finalizeTerminalState(
  pi: ExtensionAPI,
  ctx: ExtensionContext | undefined,
  questName: string,
  questId: string,
  path: string,
  questContent: string,
  projectRoot: string,
  checkActiveDirExists: () => boolean,
  checkZipExists: () => boolean,
): Promise<
  {
    verifiedLogContent: string;
    verifiedQuestContent: string;
    finalizedHierarchy: any;
    authoritativeTerminalStatus: "COMPLETED" | "FAILED";
    error?: string;
  }
> {
  const terminalQuestContent = await commitTerminalState(path, questContent);

  logImplementationOutcome(
    "IMPLEMENTATION_COMPLETED",
    `quest '${questName}' completed successfully`,
    { quest: questName, status: "COMPLETED" },
  );
  logSubquestTransition("ARCHIVE", `archived quest ${questName}`, {
    quest: questName,
    subquest: questName,
    dest: "",
    status: "COMPLETED",
  });

  const saveVerification = await verifyAndMarkSaved(pi, ctx, questName);
  if (!saveVerification.success) {
    return {
      verifiedLogContent: "",
      verifiedQuestContent: terminalQuestContent,
      finalizedHierarchy: null,
      authoritativeTerminalStatus: "COMPLETED",
      error:
        `Failed to durably verify terminal quest state on disk: ${saveVerification.error}`,
    };
  }

  let verifiedLogContent = "";
  let verifiedQuestContent = terminalQuestContent;
  try {
    verifiedLogContent = await readFile(questLogPath(questId), "utf8");
  } catch {
    verifiedLogContent = "";
  }
  try {
    verifiedQuestContent = await readFile(path, "utf8");
  } catch {
    verifiedQuestContent = terminalQuestContent;
  }

  const finalizedHierarchy = await resolveActiveRunHierarchy(projectRoot, {
    questId,
  });

  let logSummaryInfo: ReturnType<typeof summarizeQuestJournalLog> | null = null;
  try {
    if (existsSync(questLogPath(questId))) {
      logSummaryInfo = summarizeQuestJournalLog(questLogPath(questId));
    }
  } catch {}

  const calculatedStatus = calculateAuthoritativeTerminalStatus(
    finalizedHierarchy,
    logSummaryInfo,
    "COMPLETED",
  );
  const authoritativeTerminalStatus = calculatedStatus === "FAILED"
    ? "FAILED" as const
    : "COMPLETED" as const;

  onLifecycleStageTransition?.("terminal_commit", {
    questId,
    questName,
    authoritativeTerminalStatus,
    verifiedQuestContent,
    verifiedLogContent,
    activeDirExists: checkActiveDirExists(),
    zipExists: checkZipExists(),
  });

  return {
    verifiedLogContent,
    verifiedQuestContent,
    finalizedHierarchy,
    authoritativeTerminalStatus,
  };
}
