// HIGH_LEVEL: #modes — drafting / implementing / validating lifecycle.
// SPEC: B1.1-B1.5 (lifecycle), B1.4 (Exact Next Action).
import type { ApprovedBy, ArchivedOutcome, QuestKind, QuestState } from "./quest";
import { markChanged, requirePhase } from "./quest";

export function createDraft(state: QuestState, draftName: string, kind: QuestKind = "standard"): QuestState {
  requirePhase(state, "provisional");
  if (state.qid === null) throw new Error("cannot draft without a qid");
  const analysis = kind === "analysis";
  return markChanged(state, {
    phase: "drafting",
    kind,
    name: draftName,
    draft: {
      name: draftName,
      planAuthored: false,
      approvedBy: null,
      outstandingFindings: false,
      contentHash: null,
      approvedPlanHash: null,
      planRevisions: [],
    },
    exactNextAction: analysis
      ? `Author ## Analysis in the draft file for '${draftName}'.`
      : `Author ## Implementation Plan in the draft file for '${draftName}'.`,
  });
}

export function promote(state: QuestState, approvedBy: ApprovedBy): QuestState {
  requirePhase(state, "drafting");
  if (state.draft === null || !state.draft.planAuthored) {
    throw new Error("cannot promote a draft with no authored plan");
  }
  return markChanged(state, {
    phase: "implementing",
    draft: { ...state.draft, approvedBy, outstandingFindings: false, approvedPlanHash: state.draft.contentHash },
    activeReview: null,
    exactNextAction: "Proceed autonomously from the draft plan.",
  });
}

// Analysis quests skip the implementing gap: the deliverable is the analysis
// itself, so a reviewer PASS claims straight to validating (D3).
export function promoteToValidation(state: QuestState, approvedBy: ApprovedBy): QuestState {
  requirePhase(state, "drafting");
  if (state.draft === null || !state.draft.planAuthored) {
    throw new Error("cannot promote a draft with no authored deliverable");
  }
  return markChanged(state, {
    phase: "validating",
    draft: { ...state.draft, approvedBy, outstandingFindings: false, approvedPlanHash: state.draft.contentHash },
    activeReview: null,
    exactNextAction: "Await validation verdict against the delivered analysis.",
  });
}

export function claimComplete(state: QuestState): QuestState {
  requirePhase(state, "implementing");
  return markChanged(state, {
    phase: "validating",
    exactNextAction: "Await validation verdict against the approved plan.",
  });
}

export function demoteToImplementing(state: QuestState): QuestState {
  requirePhase(state, "validating");
  return markChanged(state, {
    phase: "implementing",
    activeReview: null,
    exactNextAction: "Address validation findings, then claim completion again.",
  });
}

// Validating FAIL sends analysis quests back to drafting: the analysis is a
// draft-like artifact, and implementing would lock the quest doc (D1/D3).
export function demoteToDrafting(state: QuestState): QuestState {
  requirePhase(state, "validating");
  return markChanged(state, {
    phase: "drafting",
    draft: state.draft === null ? null : { ...state.draft, outstandingFindings: true, approvedBy: null },
    activeReview: null,
    exactNextAction: "Revise the delivered analysis to address the findings, then save.",
  });
}

export function noteDraftFindings(state: QuestState): QuestState {
  requirePhase(state, "drafting");
  if (state.draft === null) throw new Error("no draft to revise");
  return markChanged(state, {
    draft: { ...state.draft, outstandingFindings: true, approvedBy: null },
    exactNextAction: "Revise the draft plan to address the findings, then save.",
  });
}

export function archive(state: QuestState, outcome: ArchivedOutcome): QuestState {
  requirePhase(state, "implementing", "validating");
  return markChanged(state, {
    phase: "archived",
    archivedOutcome: outcome,
    activeReview: null,
    exactNextAction: "",
  });
}