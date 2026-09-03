import { logAgentMessageTransition } from "../logging.ts";
import { AgentObligation, ObligationStatus, StoredState } from "../types.ts";
import { isObligationCurrent } from "./evaluators.ts";
import { pushToObligationHistory } from "./queue.ts";

type ObligationTransitionConfig = {
  status: ObligationStatus;
  timestampField: "fulfilledAt" | "supersededAt" | "cancelledAt" | "failedAt";
  reasonField:
    | "fulfilledReason"
    | "supersededReason"
    | "cancelledReason"
    | "failedReason";
  defaultReason: string;
  extra?: (obl: AgentObligation) => void;
  logEvent?: "AGENT_MESSAGE_DELIVERED" | "AGENT_MESSAGE_SUPERSEDED";
  logMessage?: (obl: AgentObligation) => string;
};

function transitionObligations(
  state: StoredState,
  predicate: (obl: AgentObligation) => boolean,
  config: ObligationTransitionConfig,
  reason: string,
): boolean {
  if (!Array.isArray(state.pendingNotifications)) return false;
  const remaining: AgentObligation[] = [];
  let any = false;
  for (const obl of state.pendingNotifications) {
    if (predicate(obl)) {
      obl.status = config.status;
      obl[config.timestampField] = Date.now();
      obl[config.reasonField] = reason;
      if (config.extra) config.extra(obl);
      pushToObligationHistory(state, obl);
      any = true;
      if (config.logEvent && config.logMessage) {
        logAgentMessageTransition(config.logEvent, config.logMessage(obl), {
          code: String(obl.code || obl.kind),
          correlationId: obl.correlationId,
          obligationId: obl.id,
          reason,
        });
      }
    } else {
      remaining.push(obl);
    }
  }
  if (any) state.pendingNotifications = remaining;
  return any;
}

function resolveObligationPredicate(
  obligationIdOrPredicate: string | ((obl: AgentObligation) => boolean),
): (obl: AgentObligation) => boolean {
  return typeof obligationIdOrPredicate === "function"
    ? obligationIdOrPredicate
    : (o) => o.id === obligationIdOrPredicate;
}

function requirePendingNotifications(state: StoredState): boolean {
  return Array.isArray(state.pendingNotifications) &&
    state.pendingNotifications.length > 0;
}

export function fulfillObligation(
  state: StoredState,
  obligationIdOrPredicate: string | ((obl: AgentObligation) => boolean),
  reason = "Obligation requirement satisfied by authoritative state",
): boolean {
  if (!requirePendingNotifications(state)) return false;
  return transitionObligations(
    state,
    resolveObligationPredicate(obligationIdOrPredicate),
    {
      status: "fulfilled",
      timestampField: "fulfilledAt",
      reasonField: "fulfilledReason",
      defaultReason: reason,
      logEvent: "AGENT_MESSAGE_DELIVERED",
      logMessage: (obl) =>
        `agent obligation fulfilled: [${obl.code || obl.kind}]`,
    },
    reason,
  );
}

export function supersedeObligation(
  state: StoredState,
  obligationIdOrPredicate: string | ((obl: AgentObligation) => boolean),
  reason = "Obligation condition no longer applicable",
): boolean {
  if (!requirePendingNotifications(state)) return false;
  return transitionObligations(
    state,
    resolveObligationPredicate(obligationIdOrPredicate),
    {
      status: "superseded",
      timestampField: "supersededAt",
      reasonField: "supersededReason",
      defaultReason: reason,
      extra: (obl) => {
        obl.superseded = true;
      },
      logEvent: "AGENT_MESSAGE_SUPERSEDED",
      logMessage: (obl) =>
        `agent obligation superseded: [${obl.code || obl.kind}]`,
    },
    reason,
  );
}

export function cancelObligation(
  state: StoredState,
  obligationIdOrPredicate: string | ((obl: AgentObligation) => boolean),
  reason = "Obligation cancelled",
): boolean {
  if (!requirePendingNotifications(state)) return false;
  return transitionObligations(
    state,
    resolveObligationPredicate(obligationIdOrPredicate),
    {
      status: "cancelled",
      timestampField: "cancelledAt",
      reasonField: "cancelledReason",
      defaultReason: reason,
    },
    reason,
  );
}

export function failObligation(
  state: StoredState,
  obligationIdOrPredicate: string | ((obl: AgentObligation) => boolean),
  reason = "Obligation failed",
): boolean {
  if (!requirePendingNotifications(state)) return false;
  return transitionObligations(
    state,
    resolveObligationPredicate(obligationIdOrPredicate),
    {
      status: "failed",
      timestampField: "failedAt",
      reasonField: "failedReason",
      defaultReason: reason,
    },
    reason,
  );
}

export function reconcileObligations(
  state: StoredState,
  _pi?: unknown,
  _ctx?: unknown,
): void {
  if (
    !Array.isArray(state.pendingNotifications) ||
    state.pendingNotifications.length === 0
  ) {
    return;
  }

  const queue = [...state.pendingNotifications];
  for (const obligation of queue) {
    if (
      typeof obligation.isFulfilled === "function" &&
      obligation.isFulfilled(state)
    ) {
      fulfillObligation(
        state,
        obligation.id,
        "Obligation fulfillment verified by predicate",
      );
    } else if (!isObligationCurrent(obligation, state)) {
      supersedeObligation(
        state,
        obligation.id,
        "Obligation condition no longer applicable in authoritative state",
      );
    }
  }
}
