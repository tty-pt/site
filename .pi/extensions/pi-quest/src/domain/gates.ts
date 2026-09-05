// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// HIGH_LEVEL: #tools (other agents) — reviewer sessions stay read-only.
// SPEC: B2 (truth table, first match wins), B2.1 (exemption first, block-message invariant).
import type { QuestState } from "./quest";
import { draftPath } from "./paths";

export type ToolClass =
  | "read"
  | "write"
  | "mutating-bash"
  | "launch"
  | "journal"
  | "ask"
  | "other";

export interface ToolRef {
  toolName: string;
  toolClass: ToolClass;
  path?: string;
}

export type Decision =
  | { allowed: true }
  | { allowed: false; phaseName: string; code: string; action: string };

export type BlockedDecision = Extract<Decision, { allowed: false }>;

export function reasonText(decision: BlockedDecision): string {
  return `${decision.phaseName}: ${decision.code} — ${decision.action}`;
}

function blocked(phaseName: string, code: string, action: string): Decision {
  return { allowed: false, phaseName, code, action };
}

export interface GateOptions {
  isReviewerSession?: boolean;
}

export function decide(state: QuestState, ref: ToolRef, options: GateOptions = {}): Decision {
  const draftFile = state.qid === null ? null : draftPath(state.qid);
  if (
    draftFile !== null &&
    ref.toolClass === "write" &&
    ref.path !== undefined &&
    (ref.path === draftFile || ref.path.endsWith(`/${draftFile}`))
  ) {
    return { allowed: true };
  }
  if (ref.toolClass === "read" && options.isReviewerSession === true) {
    return { allowed: true };
  }
  if (options.isReviewerSession === true) {
    return blocked(
      "REVIEWER_READ_ONLY",
      "IMPLEMENTATION_BLOCKED",
      "Read/search only; report via verdict.",
    );
  }
  if (state.activeReview !== null) {
    if (ref.toolClass === "journal" || ref.toolClass === "ask") {
      return { allowed: true };
    }
    return blocked(
      "AWAITING_REVIEW",
      "PLAN_REVIEW_REQUIRED",
      "Review running — end your turn; the verdict arrives as a new turn. Draft saves still supersede.",
    );
  }
  if (ref.toolClass === "read" || ref.toolClass === "journal" || ref.toolClass === "ask") {
    return { allowed: true };
  }
  if (state.phase === "drafting") {
    const draftName = draftFile ?? "the draft file";
    if (state.draft?.outstandingFindings === true) {
      return blocked(
        "DRAFT_REVISION_PENDING",
        "DRAFT_REVIEW_REQUIRED",
        `Edit the draft plan in ${draftName} to address findings; a content-changing save supersedes review and boots a fresh one.`,
      );
    }
    if (state.draft === null || !state.draft.planAuthored) {
      return blocked(
        "DRAFT_PENDING",
        "DRAFT_REVIEW_REQUIRED",
        `Author ## Implementation Plan in ${draftName}.`,
      );
    }
  }
  if (state.phase === "idle" || state.phase === "archived") {
    return { allowed: true };
  }
  if (state.phase === "provisional") {
    return blocked(
      "PROVISIONAL_RESEARCH_PENDING",
      "RESEARCH_REQUIRED",
      "Investigate, establish quest identity, call quest_update_state with findings. Then create the draft with quest_update_state {draftName} — drafts live under .pi/quest/future/ and the draft file is the only writable path; never write current/, it renders at archive.",
    );
  }
  return { allowed: true };
}
