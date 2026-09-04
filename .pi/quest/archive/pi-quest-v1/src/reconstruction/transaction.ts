import type {
  AgentObligation,
  CompactionTransaction,
  PendingAgentNotification,
  PendingResume,
  PendingSubquestResumeResolution,
} from "../types.ts";

export function reconstructActiveTransaction(
  txData: any,
): CompactionTransaction | null {
  if (!txData || typeof txData !== "object") return null;
  return {
    id: String(txData.id || ""),
    phase: txData.phase || "in-flight",
    activeQuest: String(txData.activeQuest || ""),
    questPath: typeof txData.questPath === "string"
      ? txData.questPath
      : undefined,
    reason: txData.reason || "normal-compaction",
    checkpointSaveCount: typeof txData.checkpointSaveCount === "number"
      ? txData.checkpointSaveCount
      : undefined,
    checkpointHash: typeof txData.checkpointHash === "string"
      ? txData.checkpointHash
      : undefined,
    observedSaveCount: typeof txData.observedSaveCount === "number"
      ? txData.observedSaveCount
      : undefined,
    observedHash: typeof txData.observedHash === "string"
      ? txData.observedHash
      : undefined,
    observedQuestPath: typeof txData.observedQuestPath === "string"
      ? txData.observedQuestPath
      : undefined,
    stack: Array.isArray(txData.stack) ? [...txData.stack] : [],
    researchRound: Number(txData.researchRound || 1),
    reassessmentVersion: Number(txData.reassessmentVersion || 0),
    planVersion: Number(txData.planVersion || 1),
    createdAt: Number(txData.createdAt || Date.now()),
    completedAt: typeof txData.completedAt === "number"
      ? txData.completedAt
      : undefined,
    failedAt: typeof txData.failedAt === "number" ? txData.failedAt : undefined,
    error: typeof txData.error === "string" ? txData.error : undefined,
  };
}

export function reconstructPendingResume(
  pendingResume: any,
): PendingResume | null {
  if (!pendingResume || typeof pendingResume !== "object") return null;
  return {
    compactionId: String(pendingResume.compactionId || ""),
    activeQuest: String(pendingResume.activeQuest || ""),
    reason: pendingResume.reason || "normal-compaction",
    checkpointSaveCount: typeof pendingResume.checkpointSaveCount === "number"
      ? Number(pendingResume.checkpointSaveCount)
      : undefined,
    checkpointHash: typeof pendingResume.checkpointHash === "string"
      ? String(pendingResume.checkpointHash)
      : undefined,
    checkpointQuestPath: typeof pendingResume.checkpointQuestPath === "string"
      ? String(pendingResume.checkpointQuestPath)
      : undefined,
    attempts: Number(pendingResume.attempts || 0),
    createdAt: Number(pendingResume.createdAt || Date.now()),
    lastAttemptAt: typeof pendingResume.lastAttemptAt === "number"
      ? pendingResume.lastAttemptAt
      : undefined,
    deliveredAt: typeof pendingResume.deliveredAt === "number"
      ? pendingResume.deliveredAt
      : undefined,
  };
}

function mapNotification(n: any): PendingAgentNotification {
  return {
    id: String(n.id || `notif_${Date.now()}`),
    questId: typeof n.questId === "string" ? n.questId : undefined,
    kind: n.kind,
    code: String(n.code || ""),
    message: String(n.message || ""),
    status: n.status || (n.superseded ? "superseded" : "pending"),
    deliverAs: n.deliverAs || "followUp",
    requiredNextAction: n.requiredNextAction,
    details: n.details,
    stateGeneration: typeof n.stateGeneration === "number"
      ? n.stateGeneration
      : undefined,
    planVersion: typeof n.planVersion === "number" ? n.planVersion : undefined,
    reassessmentVersion: typeof n.reassessmentVersion === "number"
      ? n.reassessmentVersion
      : undefined,
    correlationId: typeof n.correlationId === "string"
      ? n.correlationId
      : undefined,
    dedupKey: typeof n.dedupKey === "string" ? n.dedupKey : undefined,
    attempts: Number(n.attempts || 0),
    createdAt: Number(n.createdAt || Date.now()),
    lastAttemptAt: typeof n.lastAttemptAt === "number"
      ? n.lastAttemptAt
      : undefined,
    deliveredAt: typeof n.deliveredAt === "number" ? n.deliveredAt : undefined,
    fulfilledAt: typeof n.fulfilledAt === "number" ? n.fulfilledAt : undefined,
    fulfilledReason: typeof n.fulfilledReason === "string"
      ? n.fulfilledReason
      : undefined,
    supersededAt: typeof n.supersededAt === "number"
      ? n.supersededAt
      : undefined,
    supersededReason: typeof n.supersededReason === "string"
      ? n.supersededReason
      : undefined,
    cancelledAt: typeof n.cancelledAt === "number" ? n.cancelledAt : undefined,
    cancelledReason: typeof n.cancelledReason === "string"
      ? n.cancelledReason
      : undefined,
    failedAt: typeof n.failedAt === "number" ? n.failedAt : undefined,
    failedReason: typeof n.failedReason === "string"
      ? n.failedReason
      : undefined,
    superseded: Boolean(n.superseded || n.status === "superseded"),
  };
}

export function reconstructPendingNotifications(
  notifications: any,
): PendingAgentNotification[] {
  if (!Array.isArray(notifications)) return [];
  return notifications.map((n: any) => mapNotification(n));
}

export function reconstructObligationHistory(history: any): AgentObligation[] {
  if (!Array.isArray(history)) return [];
  return history.map((n: any) => ({
    id: String(n.id || `obl_hist_${Date.now()}`),
    questId: typeof n.questId === "string" ? n.questId : undefined,
    kind: n.kind,
    code: String(n.code || ""),
    message: String(n.message || ""),
    status: n.status ||
      (n.fulfilledAt ? "fulfilled" : n.superseded ? "superseded" : "pending"),
    deliverAs: n.deliverAs || "followUp",
    requiredNextAction: n.requiredNextAction,
    details: n.details,
    stateGeneration: typeof n.stateGeneration === "number"
      ? n.stateGeneration
      : undefined,
    planVersion: typeof n.planVersion === "number" ? n.planVersion : undefined,
    reassessmentVersion: typeof n.reassessmentVersion === "number"
      ? n.reassessmentVersion
      : undefined,
    correlationId: typeof n.correlationId === "string"
      ? n.correlationId
      : undefined,
    dedupKey: typeof n.dedupKey === "string" ? n.dedupKey : undefined,
    attempts: Number(n.attempts || 0),
    createdAt: Number(n.createdAt || Date.now()),
    lastAttemptAt: typeof n.lastAttemptAt === "number"
      ? n.lastAttemptAt
      : undefined,
    deliveredAt: typeof n.deliveredAt === "number" ? n.deliveredAt : undefined,
    fulfilledAt: typeof n.fulfilledAt === "number" ? n.fulfilledAt : undefined,
    fulfilledReason: typeof n.fulfilledReason === "string"
      ? n.fulfilledReason
      : undefined,
    supersededAt: typeof n.supersededAt === "number"
      ? n.supersededAt
      : undefined,
    supersededReason: typeof n.supersededReason === "string"
      ? n.supersededReason
      : undefined,
    cancelledAt: typeof n.cancelledAt === "number" ? n.cancelledAt : undefined,
    cancelledReason: typeof n.cancelledReason === "string"
      ? n.cancelledReason
      : undefined,
    failedAt: typeof n.failedAt === "number" ? n.failedAt : undefined,
    failedReason: typeof n.failedReason === "string"
      ? n.failedReason
      : undefined,
    superseded: Boolean(n.superseded || n.status === "superseded"),
  }));
}

export function reconstructPendingSubquestResolution(
  res: any,
): PendingSubquestResumeResolution | null {
  if (!res || typeof res !== "object") return null;
  return {
    child: String(res.child || ""),
    resolution: res.resolution || "obsolete-after-archive",
    resolvedAt: Number(res.resolvedAt || Date.now()),
    parent: res.parent ? String(res.parent) : null,
    details: typeof res.details === "string" ? res.details : undefined,
  };
}
