// HIGH_LEVEL: #plan revision — revisions replace remaining steps, history appends, binding re-binds.
// Pure reducer helpers over QuestState; the DraftInfo shape stays in quest.ts.
import { markChanged, type QuestState } from "./quest";

function requireImplementingDraft(state: QuestState): NonNullable<QuestState["draft"]> {
  if (state.phase !== "implementing" || state.draft === null) {
    throw new Error(`plan revision needs an implementing quest (phase ${state.phase})`);
  }
  return state.draft;
}

// Stage a revision: the content moves to the new hash, the superseded plan
// text appends to history, and the approved binding stays put until a
// reviewer PASSes the revision.
export function recordPlanRevision(
  state: QuestState,
  previousHash: string | null,
  nextHash: string,
  note: string,
  previousPlan: string,
  now: number = Date.now(),
): QuestState {
  const draft = requireImplementingDraft(state);
  if (nextHash.trim() === "") throw new Error("plan revision needs a content hash");
  if (previousHash !== null && previousHash.trim() === "") throw new Error("plan revision needs a content hash");
  return markChanged(state, {
    draft: {
      ...draft,
      contentHash: nextHash,
      planRevisions: [...(draft.planRevisions ?? []), { hash: previousHash, at: now, note, plan: previousPlan }],
    },
  });
}

// Adopt a reviewed revision: the binding moves to the reviewed hash and the
// review baseline follows the new plan text.
export function approvePlanRevision(state: QuestState, hash: string, planText: string): QuestState {
  const draft = requireImplementingDraft(state);
  if (hash.trim() === "") throw new Error("plan approval needs a content hash");
  return markChanged(state, {
    draft: { ...draft, approvedPlanHash: hash, lastReviewedPlan: planText },
  });
}

// Revert a failed revision: the content returns to the last approved hash.
// History stays append-only — the failed attempt remains recorded.
export function revertPlanRevision(state: QuestState): QuestState {
  const draft = requireImplementingDraft(state);
  return markChanged(state, {
    draft: { ...draft, contentHash: draft.approvedPlanHash },
  });
}
