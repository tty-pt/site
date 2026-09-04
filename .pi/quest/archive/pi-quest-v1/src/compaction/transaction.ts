import { logCompactionTransition } from "../logging.ts";
import { questPath } from "../paths.ts";
import { CompactionTransaction, ResumeReason, StoredState } from "../types.ts";

export function generateCompactionId(): string {
  return `cmp_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`;
}

function createPendingResumeTransaction(
  targetState: StoredState,
): CompactionTransaction {
  const pending = targetState.pendingResume!;
  const hasCheckpointIdentity =
    typeof pending.checkpointSaveCount === "number" &&
    typeof pending.checkpointHash === "string" &&
    typeof pending.checkpointQuestPath === "string";

  const pendingTx: CompactionTransaction = {
    id: pending.compactionId,
    phase: hasCheckpointIdentity ? "resume-pending" : "inconsistent",
    activeQuest: pending.activeQuest,
    questPath: pending.checkpointQuestPath || questPath(targetState.questId),
    reason: pending.reason,
    checkpointSaveCount: hasCheckpointIdentity
      ? pending.checkpointSaveCount
      : undefined,
    checkpointHash: hasCheckpointIdentity ? pending.checkpointHash : undefined,
    stack: Array.isArray(targetState.stack)
      ? [...targetState.stack]
      : (pending.activeQuest ? [pending.activeQuest] : []),
    researchRound: targetState.researchRound || 1,
    reassessmentVersion: targetState.reassessmentVersion || 0,
    planVersion: targetState.planVersion || 1,
    createdAt: pending.createdAt || Date.now(),
  };
  targetState.activeTransaction = pendingTx;
  targetState.activeCompactionId = pending.compactionId;
  return pendingTx;
}

function createNewCompactionTransaction(
  targetState: StoredState,
  reason: ResumeReason = "normal-compaction",
  questName?: string,
): CompactionTransaction {
  const active = questName || targetState.active || "";
  const p = questPath(targetState.questId);
  const id = generateCompactionId();
  const tx: CompactionTransaction = {
    id,
    phase: "prepared",
    activeQuest: active,
    questPath: p,
    reason,
    checkpointSaveCount: targetState.saveCount,
    checkpointHash: targetState.saveGeneration?.hash ||
      targetState.lastSavedHash || "",
    stack: Array.isArray(targetState.stack)
      ? [...targetState.stack]
      : (active ? [active] : []),
    researchRound: targetState.researchRound || 1,
    reassessmentVersion: targetState.reassessmentVersion || 0,
    planVersion: targetState.planVersion || 1,
    createdAt: Date.now(),
  };
  targetState.activeTransaction = tx;
  targetState.activeCompactionId = id;
  logCompactionTransition(
    "COMPACTION_PREPARED",
    `compaction prepared for ${active}`,
    {
      quest: active,
      compactionId: id,
      phase: "prepared",
      gen: targetState.saveCount,
    },
  );
  return tx;
}

export function invalidatePreparedCompactionTransaction(
  targetState: StoredState,
  reason: string = "invalidated_by_new_save",
): void {
  if (
    targetState.activeTransaction &&
    targetState.activeTransaction.phase === "prepared"
  ) {
    const oldTx = targetState.activeTransaction;
    oldTx.phase = "invalidated_by_new_save";
    logCompactionTransition(
      "COMPACTION_INVALIDATED",
      `prepared compaction checkpoint invalidated (${reason})`,
      {
        quest: oldTx.activeQuest,
        compactionId: oldTx.id,
        gen: oldTx.checkpointSaveCount,
        reason,
      },
    );
    targetState.activeTransaction = null;
    targetState.activeCompactionId = null;
  }
}

export function createOrGetCompactionTransaction(
  targetState: StoredState,
  reason: ResumeReason = "normal-compaction",
  questName?: string,
): CompactionTransaction {
  const currentQuest = questName || targetState.active || "";
  const currentQuestPath = questPath(targetState.questId);
  const currentHash = targetState.saveGeneration?.hash ||
    targetState.lastSavedHash || "";
  const currentSaveCount = targetState.saveCount;

  if (
    targetState.activeTransaction &&
    targetState.activeTransaction.phase === "prepared"
  ) {
    // Check if the prepared transaction's checkpoint matches the latest save state
    if (
      targetState.activeTransaction.checkpointSaveCount !== currentSaveCount ||
      targetState.activeTransaction.checkpointHash !== currentHash ||
      (currentQuest &&
        targetState.activeTransaction.activeQuest !== currentQuest) ||
      (currentQuestPath &&
        targetState.activeTransaction.questPath !== currentQuestPath)
    ) {
      invalidatePreparedCompactionTransaction(
        targetState,
        "stale_checkpoint_detected",
      );
    } else {
      return targetState.activeTransaction;
    }
  }

  if (
    targetState.activeTransaction &&
    (targetState.activeTransaction.phase === "in-flight" ||
      targetState.activeTransaction.phase === "resume-pending")
  ) {
    if (
      (!currentQuest ||
        targetState.activeTransaction.activeQuest === currentQuest) &&
      (!currentQuestPath ||
        targetState.activeTransaction.questPath === currentQuestPath)
    ) {
      return targetState.activeTransaction;
    }
  }

  if (targetState.pendingResume) {
    if (
      targetState.activeTransaction &&
      targetState.activeTransaction.id ===
        targetState.pendingResume.compactionId &&
      (!currentQuest ||
        targetState.activeTransaction.activeQuest === currentQuest)
    ) {
      return targetState.activeTransaction;
    }
    if (
      !currentQuest || targetState.pendingResume.activeQuest === currentQuest
    ) {
      return createPendingResumeTransaction(targetState);
    }
  }

  return createNewCompactionTransaction(targetState, reason, questName);
}
