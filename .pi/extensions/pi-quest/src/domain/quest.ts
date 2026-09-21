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

// HIGH_LEVEL: #modes — kind is immutable per quest; "analysis" swaps the
// deliverable (## Analysis) for the implementation plan in the same flow.
export type QuestKind = "standard" | "analysis";

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
  // HIGH_LEVEL: #review request — re-review briefs diff against the last reviewed plan.
  lastReviewedPlan?: string | null;
  // HIGH_LEVEL: #plan revision — the plan the implementer is bound to; re-binds validation.
  approvedPlanHash: string | null;
  // HIGH_LEVEL: #plan revision — append-only record of superseded plan texts.
  planRevisions: PlanRevision[];
}

export interface PlanRevision {
  hash: string | null;
  at: number;
  note: string;
  plan: string;
}

export interface LastReview {
  verdict: ReviewVerdict;
  target: string;
  findings: string;
  // Verbatim reviewer text (truncated). The gate echoes it so a FAIL's
  // concrete details always reach the implementer — never a blind rewrite.
  reviewText?: string;
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
  kind: QuestKind;
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
  kind: "standard",
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

export function requirePhase(state: QuestState, ...allowed: Phase[]): void {
  if (!allowed.includes(state.phase)) {
    throw new Error(
      `invalid transition from phase ${state.phase} (allowed: ${allowed.join(", ")})`,
    );
  }
}

// Every transition marks the state snapshot-pending: the code form of
// #durability ("stamped on every change"). The emitter clears the flag.
// Exported for the domain slices (children, plan revisions) sharing this file's shape.
export function markChanged(state: QuestState, patch: Partial<QuestState>): QuestState {
  return { ...state, ...patch, snapshotPending: true };
}

// --- Transitions: creation → draft → implement → validate → archive ---
//
// The modal lifecycle transitions (createDraft, promote, promoteToValidation,
// claimComplete, demoteToImplementing, demoteToDrafting, noteDraftFindings,
// archive) live in ./transitions.ts so this file stays under the complexity
// budget; they are re-exported by importers from that module.

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
  reviewText?: string,
): QuestState {
  const lastReview: LastReview = reviewText === undefined || reviewText === ""
    ? { verdict, target, findings }
    : { verdict, target, findings, reviewText };
  return markChanged(state, {
    lastReview,
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

// --- Sub-quest links live in ./children.ts (complexity budget) ---

