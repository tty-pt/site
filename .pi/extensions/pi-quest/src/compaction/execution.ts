import { calculateCurrentTokens } from "../context.ts";
import {
  logAgentMessageTransition,
  logCompactionTransition,
  logEvent,
  logResumeTransition,
} from "../logging.ts";
import {
  logDebug,
  logError,
  reportAgentError,
  sendInternalAgentMessage,
} from "../messaging.ts";
import { questPath } from "../paths.ts";
import { persist } from "../persistence.ts";
import {
  asyncContext,
  getActiveContext,
  getSessionId,
  getState,
  sessionStates,
  state,
} from "../state.ts";
import {
  ExtensionAPI,
  ExtensionContext,
  QuestErrorCode,
  StoredState,
} from "../types.ts";
import { formatTokens } from "../utils.ts";
import {
  buildPeriodicCheckpointPrompt,
  compactionReady,
  getCompactionInstructions,
} from "./checkpoint.ts";
import { getSubquestCompactThreshold } from "./policy.ts";
import { dispatchCompactionResume, retryPendingResume } from "./resume.ts";
import { createOrGetCompactionTransaction } from "./transaction.ts";
import {
  DEFAULT_CHECKPOINT_INTERVAL_TURNS,
  PERIODIC_CHECKPOINT_BURST_MS,
} from "../constants.ts";
import { readQuestLog } from "../logging/summary/helpers.ts";

export let lastSteerTurnCounter = 0;
export let lastPeriodicSteerTurnLegacy = -1;
// legacy aliases for compat
export let lastSteeredTurn = -1;
export let lastSteeredPressureState: any | null = null;
export let lastSteeredReadyState: boolean | null = null;
export let lastPreCompactionSteerTime = 0;

export function advanceSteerTurnCounter() {
  lastSteerTurnCounter++;
}

export function resetSteeredTrackingState(): void {
  lastSteeredPressureState = null;
  lastSteeredReadyState = null;
}

// --- Unified compaction scheduler internals ---

function isStaleCompaction(
  sessionState: StoredState,
  targetQuestId: string | null | undefined,
  targetActiveQuest: string | null | undefined,
  targetCompactionId: string | null | undefined,
): boolean {
  return Boolean(
    sessionState.questId !== targetQuestId ||
      sessionState.active !== targetActiveQuest ||
      (targetCompactionId &&
        sessionState.activeTransaction?.id !== targetCompactionId),
  );
}

function resetCompactionPendingState(
  sessionState: StoredState,
  targetCompactionId: string | null | undefined,
  extra: { archive?: boolean; subquestLaunch?: boolean } = {},
): void {
  if (
    targetCompactionId &&
    sessionState.activeTransaction?.id !== targetCompactionId
  ) {
    return;
  }
  sessionState.compactionPending = false;
  if (
    sessionState.activeTransaction &&
    (!targetCompactionId ||
      sessionState.activeTransaction.id === targetCompactionId)
  ) {
    sessionState.activeTransaction.phase = "failed";
    sessionState.activeTransaction.failedAt = Date.now();
  }
  if (
    !targetCompactionId ||
    sessionState.activeCompactionId === targetCompactionId
  ) {
    sessionState.activeCompactionId = null;
  }
  if (extra.archive) sessionState.archiveCompactionPending = null;
  if (extra.subquestLaunch) {
    sessionState.subquestLaunchCompactionPending = false;
  }
  sessionState.preCompactionCheckpointPending = false;
  sessionState.preCompactionSaveRequestPending = false;
}

function isBenignCompactionError(msg: string): boolean {
  return (
    msg.includes("Nothing to compact") ||
    msg.includes("Already compacted") ||
    msg.includes("session too small")
  );
}

function scheduleCompactionInternal(
  pi: ExtensionAPI,
  c: ExtensionContext,
  targetSessionId: string,
  instructions: string,
  onError: (
    latestState: StoredState,
    err: any,
    targetCompactionId: string | null,
  ) => void,
  staleLog: string,
): void {
  const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
  const targetQuestId = sessionState.questId;
  const targetActiveQuest = sessionState.active;
  const targetCompactionId = sessionState.activeTransaction?.id ||
    sessionState.activeCompactionId ||
    null;
  setTimeout(() => {
    asyncContext.run(c, () => {
      const currentSessionState = sessionStates.get(targetSessionId) ??
        getState(c);
      if (
        isStaleCompaction(
          currentSessionState,
          targetQuestId,
          targetActiveQuest,
          targetCompactionId,
        )
      ) {
        logDebug(staleLog);
        return;
      }
      try {
        c.compact!({
          customInstructions: instructions,
          onComplete: () => {},
          onError: (err: any) => {
            const latestState = sessionStates.get(targetSessionId) ??
              getState(c);
            if (
              isStaleCompaction(
                latestState,
                targetQuestId,
                targetActiveQuest,
                targetCompactionId,
              )
            ) {
              logDebug(staleLog);
              return;
            }
            onError(latestState, err, targetCompactionId);
          },
        });
      } catch (err: any) {
        onError(currentSessionState, err, targetCompactionId);
      }
    });
  }, 50);
}

function handleScheduledArchiveError(
  pi: ExtensionAPI,
  c: ExtensionContext,
  sessionState: StoredState,
  err: any,
  parentName: string | null,
  targetCompactionId?: string | null,
): void {
  if (
    targetCompactionId &&
    sessionState.activeTransaction?.id !== targetCompactionId
  ) {
    logDebug(
      `Quest Journal: ignoring stale handleScheduledArchiveError for tx=${targetCompactionId}`,
    );
    return;
  }
  resetCompactionPendingState(sessionState, targetCompactionId, {
    archive: true,
  });
  if (
    sessionState.activeTransaction &&
    (!targetCompactionId ||
      sessionState.activeTransaction.id === targetCompactionId)
  ) {
    sessionState.activeTransaction.error = err?.message || String(err);
  }
  const msg = err?.message || String(err);
  if (!isBenignCompactionError(msg)) {
    if (c.hasUI) c.ui.notify(`Post-archive compaction failed: ${msg}`, "error");
    reportAgentError(pi, c, `Post-archive compaction failed: ${msg}`, {
      code: QuestErrorCode.COMPACTION_FAILURE,
      requiredNextAction: parentName
        ? `Read ${questPath(sessionState.questId)} to resume parent execution.`
        : "Review active memory and continue execution.",
    });
  }
  if (
    parentName &&
    (sessionState.active === parentName ||
      (Array.isArray(sessionState.stack) &&
        sessionState.stack.includes(parentName)))
  ) {
    dispatchCompactionResume(pi, {
      questName: parentName,
      reason: "compaction-failure-fallback",
      ctx: c,
    });
  }
}

export function scheduleArchiveCompaction(
  pi: ExtensionAPI,
  c: ExtensionContext,
  targetSessionId: string,
  instructions: string,
  parentName: string | null,
): void {
  scheduleCompactionInternal(
    pi,
    c,
    targetSessionId,
    instructions,
    (latestState, err, targetCompactionId) => {
      handleScheduledArchiveError(
        pi,
        c,
        latestState,
        err,
        parentName,
        targetCompactionId,
      );
    },
    `Quest Journal: ignoring stale scheduled archive compaction callback`,
  );
}

export function handleSubquestLaunchCompactionFailure(
  pi: ExtensionAPI,
  c: ExtensionContext,
  sessionState: StoredState,
  err: any,
  childName: string,
  isSchedulingError = false,
  targetCompactionId?: string | null,
): void {
  if (
    targetCompactionId &&
    sessionState.activeTransaction?.id !== targetCompactionId
  ) {
    logDebug(
      `Quest Journal: ignoring stale handleSubquestLaunchCompactionFailure for tx=${targetCompactionId}`,
    );
    return;
  }
  resetCompactionPendingState(sessionState, targetCompactionId, {
    subquestLaunch: true,
  });
  if (
    sessionState.activeTransaction &&
    (!targetCompactionId ||
      sessionState.activeTransaction.id === targetCompactionId)
  ) {
    sessionState.activeTransaction.error = err?.message || String(err);
  }
  const msg = err?.message || String(err);
  const prefix = isSchedulingError
    ? "Sub-quest launch compaction scheduling failed"
    : "Sub-quest launch compaction failed";
  if (isSchedulingError) logError(prefix, err, c);
  if (!isBenignCompactionError(msg)) {
    if (c.hasUI) c.ui.notify(`${prefix}: ${msg}`, "error");
    reportAgentError(pi, c, `${prefix}: ${msg}`, {
      code: QuestErrorCode.COMPACTION_FAILURE,
      requiredNextAction: childName
        ? `Read ${
          questPath(sessionState.questId)
        } to proceed with subquest execution.`
        : "Review active memory and continue execution.",
    });
  }
  const fallbackTarget = sessionState.pendingSubquestResume &&
      sessionState.active === sessionState.pendingSubquestResume
    ? sessionState.pendingSubquestResume
    : childName;
  if (
    fallbackTarget &&
    (sessionState.active === fallbackTarget ||
      sessionState.pendingSubquestResume === fallbackTarget)
  ) {
    if (fallbackTarget === sessionState.pendingSubquestResume) {
      logResumeTransition(
        "RESUME_ATTEMPTED",
        `subquest resume after compaction failure fallback: ${fallbackTarget}`,
        {
          quest: fallbackTarget,
          subquest: fallbackTarget,
          reason: "post-launch-compaction-fallback",
        },
      );
    }
    dispatchCompactionResume(pi, {
      questName: fallbackTarget,
      reason: "compaction-failure-fallback",
      ctx: c,
    });
  }
}

export function scheduleSubquestLaunchCompaction(
  pi: ExtensionAPI,
  c: ExtensionContext,
  targetSessionId: string,
  instructions: string,
  childName: string,
): void {
  const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
  const targetQuestId = sessionState.questId;
  const targetActiveQuest = sessionState.active;
  const targetCompactionId = sessionState.activeTransaction?.id ||
    sessionState.activeCompactionId ||
    null;
  setTimeout(() => {
    asyncContext.run(c, () => {
      const currentSessionState = sessionStates.get(targetSessionId) ??
        getState(c);
      if (
        isStaleCompaction(
          currentSessionState,
          targetQuestId,
          targetActiveQuest,
          targetCompactionId,
        )
      ) {
        logDebug(
          `Quest Journal: ignoring stale scheduled subquest launch compaction callback`,
        );
        return;
      }
      try {
        c.compact!({
          customInstructions: instructions,
          onComplete: () => {},
          onError: (err: any) => {
            const latestState = sessionStates.get(targetSessionId) ??
              getState(c);
            if (
              isStaleCompaction(
                latestState,
                targetQuestId,
                targetActiveQuest,
                targetCompactionId,
              )
            ) {
              logDebug(
                `Quest Journal: ignoring stale subquest launch compaction error callback`,
              );
              return;
            }
            handleSubquestLaunchCompactionFailure(
              pi,
              c,
              latestState,
              err,
              childName,
              false,
              targetCompactionId,
            );
          },
        });
      } catch (err: any) {
        handleSubquestLaunchCompactionFailure(
          pi,
          c,
          currentSessionState,
          err,
          childName,
          true,
          targetCompactionId,
        );
      }
    });
  }, 50);
}

// --- Periodic heartbeat checkpoint (replaces pressure-driven pre-compaction) ---

export function getPeriodicLogTail(): string | undefined {
  try {
    const qid = state.questId || state.active;
    if (!qid) return undefined;
    const raw = readQuestLog(qid, 10);
    if (!raw) return undefined;
    const lines = raw.split(/\r?\n/).filter((l) => l.trim().length > 0);
    if (lines.length === 0) return undefined;
    return lines.slice(-10).join("\n").slice(-1200);
  } catch {
    return undefined;
  }
}

export function requestPeriodicCheckpoint(
  pi: ExtensionAPI,
  ctx?: ExtensionContext,
  force = false,
): boolean {
  const c = getActiveContext(ctx);
  if (!c || (!state.active && !state.pendingRootQuest && !state.activeDraft)) {
    return false;
  }
  if (state.compactionPending) return false;
  if (state.pickerCancelled) return false;
  const hasRisk = Boolean(
    state.dirty ||
      (Array.isArray(state.draftPrompts) && state.draftPrompts.length > 0) ||
      state.pendingRootQuest ||
      state.activeDraft,
  );
  if (!hasRisk && !force) return false;

  // Suppress when awaiting plan review (turn-stop gate active)
  const aw = state.awaitingReview;
  if (aw && (aw.kind === "plan_review" || aw.kind === "final_acceptance")) {
    return false;
  }

  const turnsSince = state.substantiveTurnsSinceCheckpoint || 0;
  if (!force && turnsSince < DEFAULT_CHECKPOINT_INTERVAL_TURNS) return false;
  if (!force && turnsSince % DEFAULT_CHECKPOINT_INTERVAL_TURNS !== 0) {
    return false;
  }

  const now = Date.now();
  const lastSteerAt = state.lastPeriodicSteerAt || 0;
  if (!force && now - lastSteerAt < PERIODIC_CHECKPOINT_BURST_MS) return false;

  // Update tracking (decoupled from direction review counter)
  state.lastPeriodicSteerAt = now;
  state.lastPeriodicSteerTurn = lastSteerTurnCounter;
  lastSteerTurnCounter && (lastPreCompactionSteerTime = now); // compat
  state.lastNotifiedPressure = undefined;

  const filesModified = Array.isArray(state.sessionModifiedFiles)
    ? state.sessionModifiedFiles
    : undefined;
  let logTail = getPeriodicLogTail();

  const provisionalSlug = state.active || state.activeDraft || "provisional";
  const text = buildPeriodicCheckpointPrompt(provisionalSlug, {
    turnsSinceCheckpoint: turnsSince,
    filesModified,
    logTail,
  });

  sendInternalAgentMessage(pi, text, "steer");
  logAgentMessageTransition(
    "AGENT_MESSAGE_DELIVERED",
    "periodic checkpoint prompt",
    {
      quest: provisionalSlug || "",
      type: "periodic_checkpoint",
      deliverAs: "steer",
    },
  );
  persist(pi, c);
  return true;
}

// Deprecated aliases (keep exports for tests that import old name)
export function requestPreCompactionCheckpoint(
  pi: ExtensionAPI,
  ctx?: ExtensionContext,
  force = false,
  _triggerSource?: string,
): boolean {
  return requestPeriodicCheckpoint(pi, ctx, force);
}

// Legacy handlers — retained for race-condition tests (not used by periodic checkpoint)
export function handleCriticalCompactionPressure(): boolean {
  return false;
}
export function handleWarningCompactionPressure(): boolean {
  return false;
}
function handleScheduledEconomyError(
  pi: ExtensionAPI,
  c: ExtensionContext,
  sessionState: StoredState,
  err: any,
  targetCompactionId?: string | null,
): void {
  if (
    targetCompactionId &&
    sessionState.activeTransaction?.id !== targetCompactionId
  ) {
    logDebug(
      `Quest Journal: ignoring stale handleScheduledEconomyError for tx=${targetCompactionId}`,
    );
    return;
  }
  resetCompactionPendingState(sessionState, targetCompactionId);
  if (
    sessionState.activeTransaction &&
    (!targetCompactionId ||
      sessionState.activeTransaction.id === targetCompactionId)
  ) {
    sessionState.activeTransaction.error = err?.message || String(err);
  }
  const msg = err?.message || String(err);
  if (!isBenignCompactionError(msg)) {
    if (c.hasUI) c.ui.notify(`Economy auto-compaction failed: ${msg}`, "error");
    reportAgentError(pi, c, `Economy auto-compaction failed: ${msg}`, {
      code: QuestErrorCode.COMPACTION_FAILURE,
      requiredNextAction:
        "Review working memory and continue execution; compaction will be re-attempted when context pressure warrants.",
    });
  }
}
export function scheduleEconomyCompaction(
  pi: ExtensionAPI,
  c: ExtensionContext,
  targetSessionId: string,
  instructions: string,
): void {
  scheduleCompactionInternal(
    pi,
    c,
    targetSessionId,
    instructions,
    (latestState, err, targetCompactionId) => {
      handleScheduledEconomyError(pi, c, latestState, err, targetCompactionId);
    },
    `Quest Journal: ignoring stale scheduled economy compaction callback`,
  );
}

function triggerDeferredArchiveCompaction(
  pi: ExtensionAPI,
  c: ExtensionContext,
  targetName: string,
): boolean {
  const tx = createOrGetCompactionTransaction(
    state,
    "archive-compaction",
    state.active || targetName,
  );
  tx.phase = "in-flight";
  state.compactionPending = true;
  persist(pi, c);

  const targetSessionId = getSessionId(c);
  const parentName = state.active;
  const parentPath = parentName ? questPath(state.questId) : "";
  const instructions = parentName
    ? `Sub-quest '${targetName}' completed and archived. Returning to parent quest '${parentName}'. Focus summary on key architecture decisions, completed sub-quest findings, and remaining parent roadmap. Parent quest state is safely preserved on disk in ${parentPath}. Following compaction, read ${parentPath} first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified parent action.`
    : `Quest '${targetName}' completed and archived. Focus summary on key architecture decisions, completed work, and remaining roadmap.`;

  scheduleArchiveCompaction(pi, c, targetSessionId, instructions, parentName);
  return true;
}

function triggerDeferredSubquestLaunchCompaction(
  pi: ExtensionAPI,
  c: ExtensionContext,
  childName: string,
): boolean {
  const subLaunchThreshold = getSubquestCompactThreshold();
  const tokens = calculateCurrentTokens(c);

  if (
    !childName ||
    !compactionReady(childName) ||
    subLaunchThreshold <= 0 ||
    tokens === null ||
    tokens < subLaunchThreshold
  ) {
    return false;
  }

  const tx = createOrGetCompactionTransaction(
    state,
    "subquest-launch",
    childName,
  );
  tx.phase = "in-flight";
  state.compactionPending = true;
  persist(pi, c);

  const isSubQuest = Array.isArray(state.stack) && state.stack.length > 1;
  const parentName = isSubQuest ? state.stack[state.stack.length - 2] : null;
  const childPath = questPath(state.questId);
  const parentPath = parentName ? questPath(state.questId) : "";
  const instructions = parentName
    ? `Launching sub-quest '${childName}' (parent: '${parentName}'). Focus summary on parent quest status, key architectural decisions, and why sub-quest '${childName}' was launched. Child sub-quest state is safely saved on disk in ${childPath}. Following compaction, read ${childPath} first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified next action.`
    : `Launching sub-quest '${childName}'. Focus summary on key architectural decisions and why sub-quest '${childName}' was launched. Child sub-quest state is safely saved on disk in ${childPath}. Following compaction, read ${childPath} first to recover established knowledge, validate the plan against recovered evidence, re-investigate if uncertainty or contradictions exist, and proceed with the most justified next action.`;

  const targetSessionId = getSessionId(c);
  scheduleSubquestLaunchCompaction(
    pi,
    c,
    targetSessionId,
    instructions,
    childName,
  );
  return true;
}

export function checkAndTriggerDeferredCompaction(
  pi: ExtensionAPI,
  ctx?: ExtensionContext,
): boolean {
  const c = getActiveContext(ctx);
  if (
    !c ||
    state.pickerCancelled ||
    state.compactionPending ||
    typeof c.compact !== "function"
  ) {
    return false;
  }

  if (
    state.pendingResume ||
    state.activeTransaction?.phase === "resume-pending"
  ) {
    retryPendingResume(pi, c);
    if (
      state.pendingResume ||
      state.activeTransaction?.phase === "resume-pending"
    ) {
      logDebug(
        "Quest Journal: postponing deferred compaction because previous resume obligation is still pending delivery.",
      );
      return false;
    }
  }

  if (state.archiveCompactionPending) {
    return triggerDeferredArchiveCompaction(
      pi,
      c,
      state.archiveCompactionPending,
    );
  }

  if (state.subquestLaunchCompactionPending && state.active) {
    return triggerDeferredSubquestLaunchCompaction(pi, c, state.active);
  }

  return false;
}
