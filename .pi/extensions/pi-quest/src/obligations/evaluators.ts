import { AgentObligation, StoredState } from "../types.ts";

export type ObligationCurrentnessEvaluator = (
  obligation: AgentObligation,
  currentState: StoredState,
) => boolean;

const obligationEvaluators = new Map<string, ObligationCurrentnessEvaluator>();

export function registerObligationEvaluator(
  kind: string,
  evaluator: ObligationCurrentnessEvaluator,
): void {
  obligationEvaluators.set(kind.toLowerCase(), evaluator);
}

export function getObligationEvaluator(
  kind: string,
): ObligationCurrentnessEvaluator | undefined {
  return obligationEvaluators.get(kind.toLowerCase());
}

const EVALUATOR_TABLE: Array<[string, ObligationCurrentnessEvaluator]> = [
  [
    "research",
    (_obl, s) => !s.researchComplete || Boolean(s.reassessmentRequired),
  ],
  ["reassessment", (obl, s) => {
    if (!s.reassessmentRequired) return false;
    if (
      typeof obl.reassessmentVersion === "number" &&
      (s.resolvedReassessmentVersion || 0) >= obl.reassessmentVersion
    ) return false;
    return true;
  }],
  ["confirmation", (_obl, s) => Boolean(s.awaitingUserConfirmation)],
  [
    "checkpoint",
    (obl, s) =>
      !(typeof obl.stateGeneration === "number" &&
        s.saveCount > obl.stateGeneration && !s.dirty),
  ],
  ["error", () => true],
  [
    "research_required",
    (_obl, s) => !s.researchComplete || Boolean(s.reassessmentRequired),
  ],
  [
    "reassessment_required",
    (obl, s) =>
      Boolean(s.reassessmentRequired) &&
      ((obl.reassessmentVersion === undefined) ||
        (s.resolvedReassessmentVersion || 0) < obl.reassessmentVersion),
  ],
  ["confirmation_required", (_obl, s) => Boolean(s.awaitingUserConfirmation)],
  [
    "checkpoint_required",
    (obl, s) =>
      !(typeof obl.stateGeneration === "number" &&
        s.saveCount > obl.stateGeneration && !s.dirty),
  ],
];
for (const [kind, fn] of EVALUATOR_TABLE) registerObligationEvaluator(kind, fn);

export function isObligationCurrent(
  obligation: AgentObligation,
  currentState: StoredState,
): boolean {
  if (
    obligation.status === "fulfilled" || obligation.status === "superseded" ||
    obligation.status === "cancelled" || obligation.status === "failed" ||
    obligation.superseded
  ) {
    return false;
  }

  if (
    obligation.questId &&
    currentState.questId &&
    obligation.questId !== currentState.questId &&
    obligation.questId !== currentState.active
  ) {
    return false;
  }

  if (
    typeof obligation.isFulfilled === "function" &&
    obligation.isFulfilled(currentState)
  ) {
    return false;
  }

  if (typeof obligation.isCurrent === "function") {
    return obligation.isCurrent(currentState);
  }

  const codeKey = String(obligation.code || "").toLowerCase();
  if (codeKey) {
    const codeEvaluator = obligationEvaluators.get(codeKey);
    if (codeEvaluator) {
      return codeEvaluator(obligation, currentState);
    }
  }

  const kindKey = String(obligation.kind || "").toLowerCase();
  const kindEvaluator = obligationEvaluators.get(kindKey);
  if (kindEvaluator) {
    return kindEvaluator(obligation, currentState);
  }

  return true;
}
