// HIGH_LEVEL: #quest creation — auto-created on request, qid at creation, one active.
// HIGH_LEVEL: #modes — drafting / implementing / validating lifecycle.
// SPEC: B1.0 (identity), B1.1-B1.5 (lifecycle), B1.4 (Exact Next Action).
import type { Qid } from "./qid";
import { isQid } from "./qid";

// --- Mode vocabulary (HIGH_LEVEL words; B1.1 codes map onto these) ---

export type Phase =
  | "idle"
  | "provisional"
  | "drafting"
  | "implementing"
  | "validating"
  | "archived";

export type ArchivedOutcome = "COMPLETED" | "FAILED" | "ABANDONED";

export type ReviewKind = "draft" | "validation";

export type ApprovedBy = "review" | "user";

export type ReviewVerdict = "PASS" | "FAIL";

// --- Review + draft (revision-bound per the review protocol) ---

export interface ActiveReview {
  kind: ReviewKind;
  target: string;
}

export interface DraftInfo {
  name: string;
  planAuthored: boolean;
  approvedBy: ApprovedBy | null;
  outstandingFindings: boolean;
  contentHash: string | null;
}

export interface LastReview {
  verdict: ReviewVerdict;
  target: string;
  findings: string;
}

export interface DialogueRound {
  round: number;
  timestamp: number;
  reviewerFindings: string;
  implementerRebuttal: string;
  verdictBefore: ReviewVerdict;
  verdictAfter?: ReviewVerdict;
}

// --- Implementing shapes ---

export interface Setback {
  reason: string;
  evidence: string[];
}

export interface Amendment {
  change: string;
  reasons: string;
}

export type ChildStatus = "running" | "returned" | "failed";

export interface ChildLink {
  qid: Qid;
  brief: string;
  status: ChildStatus;
  findings: string | null;
  acknowledged: boolean;
}

export interface HumanAnswer {
  question: string;
  answer: string;
  late: boolean;
}

// --- State ---

export interface QuestState {
  phase: Phase;
  qid: Qid | null;
  parentQid: Qid | null;
  depth: number;
  name: string;
  objective: string;
  pendingRootRequest: string | null;
  refinements: string[];
  humanAnswers: HumanAnswer[];
  draft: DraftInfo | null;
  activeReview: ActiveReview | null;
  lastReview: LastReview | null;
  reviewDialogue: DialogueRound[];
  exactNextAction: string;
  setbacks: Setback[];
  amendments: Amendment[];
  children: ChildLink[];
  archivedOutcome: ArchivedOutcome | null;
  snapshotPending: boolean;
}

export const IDLE_STATE: QuestState = {
  phase: "idle",
  qid: null,
  parentQid: null,
  depth: 0,
  name: "",
  objective: "",
  pendingRootRequest: null,
  refinements: [],
  humanAnswers: [],
  draft: null,
  activeReview: null,
  lastReview: null,
  reviewDialogue: [],
  exactNextAction: "",
  setbacks: [],
  amendments: [],
  children: [],
  archivedOutcome: null,
  snapshotPending: false,
};

// --- Guards ---

function requirePhase(state: QuestState, ...allowed: Phase[]): void {
  if (!allowed.includes(state.phase)) {
    throw new Error(
      `invalid transition from phase ${state.phase} (allowed: ${allowed.join(", ")})`,
    );
  }
}

// Every transition marks the state snapshot-pending: the code form of
// #durability ("stamped on every change"). The emitter clears the flag.
function markChanged(state: QuestState, patch: Partial<QuestState>): QuestState {
  return { ...state, ...patch, snapshotPending: true };
}

// --- Transitions: creation → draft → implement → validate → archive ---

export function createQuest(request: string, qid: string, parentQid: Qid | null = null): QuestState {
  if (!isQid(qid)) throw new Error(`invalid qid: ${qid}`);
  return markChanged(IDLE_STATE, {
    phase: "provisional",
    qid,
    parentQid,
    objective: request,
    pendingRootRequest: request,
    exactNextAction: "Establish quest identity: investigate, then record findings.",
  });
}

export function createDraft(state: QuestState, draftName: string): QuestState {
  requirePhase(state, "provisional");
  if (state.qid === null) throw new Error("cannot draft without a qid");
  return markChanged(state, {
    phase: "drafting",
    name: draftName,
    draft: {
      name: draftName,
      planAuthored: false,
      approvedBy: null,
      outstandingFindings: false,
      contentHash: null,
    },
    exactNextAction: `Author ## Implementation Plan in the draft file for '${draftName}'.`,
  });
}

export function promote(state: QuestState, approvedBy: ApprovedBy): QuestState {
  requirePhase(state, "drafting");
  if (state.draft === null || !state.draft.planAuthored) {
    throw new Error("cannot promote a draft with no authored plan");
  }
  return markChanged(state, {
    phase: "implementing",
    draft: { ...state.draft, approvedBy, outstandingFindings: false },
    activeReview: null,
    exactNextAction: "Proceed autonomously from the draft plan.",
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

// Promotion requires recorded research alongside the actionable plan:
// evidence items, refinements, or setback evidence on file.
export function researchRecorded(state: QuestState, draftEvidence: number): boolean {
  if (draftEvidence > 0) return true;
  if (state.refinements.length > 0) return true;
  return state.setbacks.some((s) => s.evidence.length > 0);
}

// A child deviates from its brief when it recorded anything beyond the
// brief itself: amendments, setbacks, refinements, or review dialogue.
// Clean children skip the draft reviewer; the validator still judges them.
export function childDeviated(state: QuestState): boolean {
  return state.amendments.length > 0 ||
    state.setbacks.length > 0 ||
    state.refinements.length > 0 ||
    state.reviewDialogue.length > 0;
}

// --- Review history ---

export function recordReviewResult(
  state: QuestState,
  verdict: ReviewVerdict,
  target: string,
  findings: string,
): QuestState {
  return markChanged(state, {
    lastReview: { verdict, target, findings },
    exactNextAction: verdict === "PASS"
      ? state.exactNextAction
      : "Address the review findings, then continue.",
  });
}

export function recordRebuttal(
  state: QuestState,
  rebuttal: string,
  verdictBefore: ReviewVerdict,
  reviewerFindings: string,
  now: number = Date.now(),
): { state: QuestState; round: number } {
  if (rebuttal.trim().length < 10) throw new Error("rebuttal needs substantive evidence");
  const round = state.reviewDialogue.length + 1;
  const next: QuestState = markChanged(state, {
    reviewDialogue: [
      ...state.reviewDialogue,
      { round, timestamp: now, reviewerFindings, implementerRebuttal: rebuttal, verdictBefore },
    ],
  });
  return { state: next, round };
}

export function resolveDialogueRound(
  state: QuestState,
  round: number,
  verdictAfter: ReviewVerdict,
): QuestState {
  return markChanged(state, {
    reviewDialogue: state.reviewDialogue.map((d) =>
      d.round === round ? { ...d, verdictAfter } : d
    ),
  });
}

// --- Implementing records ---

export function recordAdvisoryNote(state: QuestState, reason: string, evidence: string[]): QuestState {
  if (reason.trim() === "") throw new Error("setback needs a reason");
  return markChanged(state, {
    setbacks: [...state.setbacks, { reason, evidence }],
    exactNextAction: state.exactNextAction,
  });
}

export function recordAmendment(state: QuestState, change: string, reasons: string): QuestState {
  requirePhase(state, "implementing", "validating");
  if (change.trim() === "") throw new Error("amendment needs a change");
  return markChanged(state, {
    amendments: [...state.amendments, { change, reasons }],
  });
}

export function recordRefinement(state: QuestState, text: string): QuestState {
  if (text.trim() === "") throw new Error("refinement needs text");
  return markChanged(state, { refinements: [...state.refinements, text] });
}

export function recordHumanAnswer(
  state: QuestState,
  question: string,
  answer: string,
  late: boolean,
): QuestState {
  return markChanged(state, {
    humanAnswers: [...state.humanAnswers, { question, answer, late }],
  });
}

// --- Sub-quest links ---

export function addChild(state: QuestState, link: ChildLink): QuestState {
  if (state.children.some((c) => c.qid === link.qid)) throw new Error(`child ${link.qid} already linked`);
  return markChanged(state, { children: [...state.children, link] });
}

export function settleChild(state: QuestState, qid: Qid, status: ChildStatus, findings: string | null): QuestState {
  return markChanged(state, {
    children: state.children.map((c) => c.qid === qid ? { ...c, status, findings } : c),
  });
}

export function acknowledgeChild(state: QuestState, qid: Qid): QuestState {
  const link = state.children.find((c) => c.qid === qid);
  if (!link) throw new Error(`no linked child ${qid}`);
  if (link.status === "running") throw new Error(`child ${qid} has not returned yet`);
  return markChanged(state, {
    children: state.children.map((c) => c.qid === qid ? { ...c, acknowledged: true } : c),
  });
}

export function unfinishedChildren(state: QuestState): ChildLink[] {
  return state.children.filter((c) => c.status === "running");
}
