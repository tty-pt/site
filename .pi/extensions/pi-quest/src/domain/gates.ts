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
  if (
    state.qid !== null &&
    ref.toolClass === "write" &&
    ref.path !== undefined &&
    ref.path === draftPath(state.qid)
  ) {
    return { allowed: true };
  }
  if (options.isReviewerSession === true) {
    return blocked(
      "REVIEWER_READ_ONLY",
      "IMPLEMENTATION_BLOCKED",
      "Read/search only; report via verdict.",
    );
  }
  if (ref.toolClass === "read" || ref.toolClass === "journal" || ref.toolClass === "ask") {
    return { allowed: true };
  }
  if (state.phase === "drafting") {
    if (state.draft?.outstandingFindings === true) {
      return blocked(
        "DRAFT_REVISION_PENDING",
        "DRAFT_REVIEW_REQUIRED",
        "Edit the draft plan to address findings; a content-changing save supersedes review and boots a fresh one.",
      );
    }
    if (state.draft === null || !state.draft.planAuthored) {
      return blocked(
        "DRAFT_PENDING",
        "DRAFT_REVIEW_REQUIRED",
        "Author ## Implementation Plan in the draft file.",
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
      "Investigate, establish quest identity, call quest_update_state with findings.",
    );
  }
  if (state.activeReview !== null) {
    return blocked(
      "AWAITING_REVIEW",
      "PLAN_REVIEW_REQUIRED",
      "No writes until verdict — except the draft file; reads allowed.",
    );
  }
  return { allowed: true };
}
