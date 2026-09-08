// HIGH_LEVEL: #drafting — one writable file, all else blocked.
// HIGH_LEVEL: #implementing — the quest doc is locked to direct edits; plan
// moves go through peer-reviewed planRevision, completion through claimComplete.
// HIGH_LEVEL: #tools (other agents) — reviewer sessions stay read-only.
// SPEC: B2 (truth table, first match wins), B2.1 (write-signal blocks only,
// unknown tools default to allowed; exemption first; block-message invariant).
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

// B2.1: phases block write signals — direct edits, mutating bash, and
// subagent launch. Every other class (reads, journal, questions, and unknown
// tools like vcc_recall) defaults to allowed, so research is never blocked
// by an unrecognized tool.
function isWriteSignal(toolClass: ToolClass): boolean {
  return toolClass === "write" || toolClass === "mutating-bash" || toolClass === "launch";
}

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

function draftDecision(state: QuestState, draftName: string): Decision {
  if (state.draft?.outstandingFindings === true) {
    const findings = state.lastReview?.findings?.trim() ?? "";
    const findingsText = findings === ""
      ? ""
      : ` Findings from the last review: ${findings.length > 1200 ? `${findings.slice(0, 1200)}…` : findings}`;
    return blocked(
      "DRAFT_REVISION_PENDING",
      "DRAFT_REVIEW_REQUIRED",
      `Edit the draft plan in ${draftName} to address findings; a content-changing save supersedes review and boots a fresh one. Prefer quest_update_state {plan: ...} — it splices the Implementation Plan section and boots a fresh review.${findingsText}`,
    );
  }
  if (state.draft === null || !state.draft.planAuthored) {
    return blocked(
      "DRAFT_PENDING",
      "DRAFT_REVIEW_REQUIRED",
      `Author ## Implementation Plan in ${draftName} — pass {plan: ...} to quest_update_state to write it directly.`,
    );
  }
  return blocked(
    "DRAFT_LOCKED",
    "DRAFT_REVIEW_REQUIRED",
    `Only the quest document (${draftName}) is writable while drafting. Author the plan and save to boot the review; promotion to implementing unlocks the worktree. A completed review — or a live user "go" — is the only path out of drafting.`,
  );
}

function validatingDecision(): Decision {
  return blocked(
    "VALIDATION_LOCKED",
    "VALIDATION_REQUIRED",
    "Validating is write-free: address the validation verdict via quest_update_state {planRevision: ...}, or pass {continueWork: true} to resume implementing.",
  );
}

export function decide(state: QuestState, ref: ToolRef, options: GateOptions = {}): Decision {
  const draftFile = state.qid === null ? null : draftPath(state.qid);
  const isDraftFile = draftFile !== null &&
    ref.toolClass === "write" &&
    ref.path !== undefined &&
    (ref.path === draftFile || ref.path.endsWith(`/${draftFile}`));
  // The drafting exemption keeps its historic precedence: it beats even the
  // reviewer row. The lock below only narrows implementing/validating.
  if (isDraftFile && state.phase !== "implementing" && state.phase !== "validating") {
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
  if (isDraftFile) {
    return blocked(
      "QUEST_DOC_LOCKED",
      "IMPLEMENTATION_BLOCKED",
      "Quest doc is locked during implementation. Use quest_update_state {planRevision: ...} to revise the plan (peer-reviewed), or {claimComplete: true} to claim completion.",
    );
  }
  if (state.activeReview !== null) {
    if (ref.toolClass === "journal" || ref.toolClass === "ask") {
      return { allowed: true };
    }
    return blocked(
      "AWAITING_REVIEW",
      "PLAN_REVIEW_REQUIRED",
      "Review running — end your turn; the verdict arrives as a new turn.",
    );
  }
  // B2.1: only write signals are phase-gated. Reads, journal ops, questions,
  // and unknown-class tools (vcc_recall, …) pass — no read whitelist to
  // maintain, and an unrecognized tool never blocks research.
  if (!isWriteSignal(ref.toolClass)) {
    return { allowed: true };
  }
  if (state.phase === "drafting") {
    return draftDecision(state, draftFile ?? "the draft file");
  }
  if (state.phase === "validating") {
    return validatingDecision();
  }
  if (state.phase === "idle" || state.phase === "archived") {
    return { allowed: true };
  }
  if (state.phase === "provisional") {
    return blocked(
      "PROVISIONAL_RESEARCH_PENDING",
      "RESEARCH_REQUIRED",
      "Investigate, establish quest identity, call quest_update_state with findings. Then create the draft with quest_update_state {draftName} — the quest document lives at .pi/quest/future/<qid>.md; during drafting it is the only writable file, and during implementing and validating it is locked to direct edits.",
    );
  }
  return { allowed: true };
}
