import { logAgentMessageTransition } from "../logging.ts";
import {
  AgentObligation,
  ObligationKind,
  ObligationStatus,
  QuestErrorCode,
  StoredState,
} from "../types.ts";

const MAX_HISTORY_SIZE = 50;

export function pushToObligationHistory(
  state: StoredState,
  obligation: AgentObligation,
): void {
  if (!Array.isArray(state.obligationHistory)) {
    state.obligationHistory = [];
  }
  const existingIdx = state.obligationHistory.findIndex((o) =>
    o.id === obligation.id
  );
  if (existingIdx >= 0) {
    state.obligationHistory[existingIdx] = { ...obligation };
  } else {
    state.obligationHistory.push({ ...obligation });
    if (state.obligationHistory.length > MAX_HISTORY_SIZE) {
      state.obligationHistory = state.obligationHistory.slice(
        -MAX_HISTORY_SIZE,
      );
    }
  }
}

export function createAgentObligation(
  state: StoredState,
  opts: {
    id?: string;
    kind: ObligationKind;
    code?: QuestErrorCode | string;
    message: string;
    status?: ObligationStatus;
    deliverAs?: "steer" | "followUp" | "nextTurn";
    requiredNextAction?: string;
    details?: Record<string, any> | string;
    correlationId?: string;
    dedupKey?: string;
    isCurrent?: (currentState: StoredState) => boolean;
    isFulfilled?: (currentState: StoredState) => boolean;
  },
): AgentObligation {
  return {
    id: opts.id ||
      `obl_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`,
    questId: state.questId || state.active || "",
    kind: opts.kind,
    code: opts.code,
    message: opts.message,
    status: opts.status || "pending",
    deliverAs: opts.deliverAs || "followUp",
    requiredNextAction: opts.requiredNextAction,
    details: opts.details,
    stateGeneration: state.saveCount || 0,
    planVersion: state.planVersion || 1,
    reassessmentVersion: state.reassessmentVersion || 0,
    correlationId: opts.correlationId,
    dedupKey: opts.dedupKey,
    createdAt: Date.now(),
    attempts: 0,
    isCurrent: opts.isCurrent,
    isFulfilled: opts.isFulfilled,
  };
}

export function queueAgentObligation(
  state: StoredState,
  obligation: AgentObligation,
): AgentObligation {
  if (!Array.isArray(state.pendingNotifications)) {
    state.pendingNotifications = [];
  }

  if (Array.isArray(state.obligationHistory) && obligation.id) {
    const historical = state.obligationHistory.find((o) =>
      o.id === obligation.id
    );
    if (
      historical &&
      (historical.status === "fulfilled" ||
        historical.status === "superseded" || historical.status === "cancelled")
    ) {
      return historical;
    }
  }

  const dedupKey = obligation.dedupKey ||
    (obligation.code && obligation.message
      ? `${obligation.code}:${obligation.message}`
      : (obligation.id || ""));

  const existing = state.pendingNotifications.find(
    (n) =>
      (n.id && n.id === obligation.id) ||
      (dedupKey && n.dedupKey && n.dedupKey === dedupKey) ||
      (n.code && n.code === obligation.code &&
        n.message === obligation.message),
  );

  if (existing) {
    existing.attempts = (existing.attempts || 0) + 1;
    existing.lastAttemptAt = Date.now();
    if (obligation.correlationId) {
      existing.correlationId = obligation.correlationId;
    }
    if (obligation.details) {
      existing.details = obligation.details;
    }
    if (obligation.requiredNextAction) {
      existing.requiredNextAction = obligation.requiredNextAction;
    }
    return existing;
  } else {
    if (!obligation.status) {
      obligation.status = "pending";
    }
    state.pendingNotifications.push(obligation);
    if (state.pendingNotifications.length > 50) {
      state.pendingNotifications = state.pendingNotifications.slice(-50);
    }
    logAgentMessageTransition(
      "AGENT_MESSAGE_QUEUED",
      `agent obligation queued: [${obligation.code || obligation.kind}]`,
      {
        code: typeof obligation.code === "string"
          ? obligation.code
          : String(obligation.code || obligation.kind),
        correlationId: obligation.correlationId,
        deliverAs: obligation.deliverAs,
        obligationId: obligation.id,
      },
    );
    return obligation;
  }
}

export function getPendingObligations(state: StoredState): AgentObligation[] {
  if (!Array.isArray(state.pendingNotifications)) return [];
  return state.pendingNotifications.filter(
    (o) => (o.status === "pending" || !o.status) && !o.superseded,
  );
}

export function getObligationHistory(state: StoredState): AgentObligation[] {
  return Array.isArray(state.obligationHistory)
    ? [...state.obligationHistory]
    : [];
}
