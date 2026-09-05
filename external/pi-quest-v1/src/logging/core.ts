import { appendFileSync, existsSync, mkdirSync, writeFileSync } from "node:fs";
import { dirname } from "node:path";
import { getActiveContext, getSessionId, getState, state } from "../state.ts";
import { ExtensionContext } from "../types.ts";
import {
  formatLogEntry,
  normalizeLogPath,
  sanitizeLogString,
} from "./formatters.ts";
import { getQuestLogPath } from "./paths.ts";
import { QuestLogContext, QuestLogEventType } from "./types.ts";

let isLoggingDegraded = false;
let hasWarnedLoggingDegraded = false;

export function isQuestLoggingDegraded(): boolean {
  return isLoggingDegraded;
}

export function resetQuestLoggingDegraded(): void {
  isLoggingDegraded = false;
  hasWarnedLoggingDegraded = false;
}

export function writeLogLineSync(
  logPath: string,
  line: string,
  ctx?: ExtensionContext,
): boolean {
  try {
    const dir = dirname(logPath);
    if (!existsSync(dir)) {
      mkdirSync(dir, { recursive: true });
    }
    appendFileSync(logPath, line + "\n", "utf8");
    if (isLoggingDegraded) {
      isLoggingDegraded = false;
      hasWarnedLoggingDegraded = false;
      logPersistenceTransition(
        "PERSISTENCE_RECOVERED",
        `logging persistence recovered for ${logPath}`,
        { logPath },
      );
    }
    return true;
  } catch (err: any) {
    if (!isLoggingDegraded) {
      isLoggingDegraded = true;
      if (!hasWarnedLoggingDegraded) {
        hasWarnedLoggingDegraded = true;
        const c = getActiveContext(ctx);
        if (c?.hasUI && typeof c.ui?.notify === "function") {
          c.ui.notify(
            `Quest Journal Warning: Logging degraded (${
              err?.message || "write failure"
            })`,
            "warning",
          );
        }
      }
    }
    return false;
  }
}

export function logEvent(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
  ctx?: ExtensionContext,
): void {
  const c = getActiveContext(ctx);
  const targetSessionId = getSessionId(c);
  const s = getState(c);

  const targetRoot = s?.rootQuest ||
    (s?.stack && s.stack.length > 0
      ? s.stack[0]
      : (context?.root || context?.rootQuest || s?.active || ""));

  const piSid = targetSessionId || context?.piSessionId ||
    context?.opencodeSessionId;
  const basePi = piSid || targetSessionId;
  const baseOpen = context?.opencodeSessionId || piSid || targetSessionId;
  const enrichedContext: QuestLogContext = {
    questId: s?.questId || context?.questId || "default",
    root: targetRoot,
    rootQuest: targetRoot,
    sessionId: targetSessionId,
    quest: context?.quest || s?.active || s?.activeDraft || "",
    turn: context?.turn !== undefined ? context.turn : s?.currentTurn,
    correlationId: context?.correlationId || s?.currentTurnCorrelationId,
    ...context,
    piSessionId: context?.piSessionId || basePi,
    opencodeSessionId: context?.opencodeSessionId || baseOpen,
  } as QuestLogContext;

  const targetLogPath = enrichedContext.logPath ||
    getQuestLogPath(enrichedContext.questId || s?.questId);
  const line = formatLogEntry(type, message, enrichedContext);

  writeLogLineSync(targetLogPath, line, c);
}

export function logQuestTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logTurnBoundary(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logGateTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logResearchTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logReassessmentTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logImplementationOutcome(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logToolActivity(
  tool: string,
  operation: string,
  context?: QuestLogContext,
): void {
  logEvent("TOOL_ACTIVITY", `tool ${tool} ${operation}`, {
    tool,
    operation,
    ...context,
  });
}

export function logToolFailure(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logToolAnomaly(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logVerificationTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logStateTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logStateUpdateTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logSaveTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logPersistenceTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logCompactionTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logResumeTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logAgentMessageTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logContinuationAnomaly(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logUserInteraction(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logSubquestTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logCriticalReviewTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}

export function logRecoveryTransition(
  type: QuestLogEventType,
  message: string,
  context?: QuestLogContext,
): void {
  logEvent(type, message, context);
}
