import { compactionReady } from "./compaction.ts";
import {
  getCustomSubagentRunner,
  isPlanReviewValidForState,
  isSubagentToolRegistered,
} from "./critical_agent/index.ts";
import { isDraftRevisionOutstanding } from "./critical_agent/snapshot.ts";
import { questPath } from "./paths.ts";
import { resolveCallerSelf } from "./roles.ts";
import { getActiveContext, isRootQuest, state } from "./state.ts";
import {
  ExtensionContext,
  QuestErrorCode,
  QuestLifecycleState,
  StoredState,
} from "./types.ts";

export function canImplement(
  targetState?: StoredState,
  ctx?: ExtensionContext,
): boolean {
  // Reviewer children are never subject to implementer gating: they run
  // read-only in their own isolated session. A child session can never attain
  // implementation permission on the main quest.
  if (resolveCallerSelf(ctx).isChild) return false;
  const s = targetState || state;
  if (s.pendingRootQuest) return false;
  if (s.activeDraft) return false;
  if (!s.active) return true;
  if (
    s.activeTransaction &&
    (s.activeTransaction.phase === "inconsistent" ||
      s.activeTransaction.phase === "failed")
  ) {
    return false;
  }
  if (s.pendingSubquestResume && s.pendingSubquestResume !== s.active) {
    return false;
  }
  if (s.compactionPending) {
    return false;
  }
  // Periodic heartbeat does not gate implementation on token pressure; dirty-state safety is enforced via session_before_compact.
  if (s.researchRequired) return false;
  if (!s.researchComplete) return false;
  if (s.reassessmentRequired) return false;
  if (
    isRootQuest(s) &&
    (isSubagentToolRegistered(undefined, ctx) ||
      Boolean(getCustomSubagentRunner())) &&
    !isPlanReviewValidForState(s)
  ) {
    return false;
  }
  if (
    s.awaitingReview &&
    (s.awaitingReview.kind === "plan_review" ||
      s.awaitingReview.kind === "final_acceptance")
  ) {
    return false;
  }
  if (isRootQuest(s) && s.awaitingUserConfirmation) return false;
  return true;
}

export function getImplementationBlockReason(
  targetState?: StoredState,
  ctx?: ExtensionContext,
): {
  blocked: boolean;
  code: QuestErrorCode;
  stateName: string;
  reason: string;
  requiredAction: string;
} {
  const s = targetState || state;
  // Reviewer children run read-only in their own session; report a distinct role-based
  // reason instead of main-agent implementation reasons.
  if (resolveCallerSelf(ctx).isChild) {
    return {
      blocked: true,
      code: QuestErrorCode.IMPLEMENTATION_BLOCKED,
      stateName: "REVIEWER_READ_ONLY",
      reason:
        "Reviewer child agents are strictly read-only and never granted implementation permission.",
      requiredAction:
        "Use read/search tools only; report findings via the review verdict.",
    };
  }
  if (s.pendingRootQuest) {
    return {
      blocked: true,
      code: QuestErrorCode.RESEARCH_REQUIRED,
      stateName: "PROVISIONAL_RESEARCH_PENDING",
      reason:
        "Initial orientation & research required to understand the objective and establish the quest identity before modifying project code.",
      requiredAction:
        "Investigate relevant architecture and code paths using read/search/bash tools, establish a concise semantic quest identity, and call quest_update_state to initialize the durable quest with your research findings.",
    };
  }
  if (s.activeDraft) {
    // F3(ii): hard throttle while REVISE outstanding
    try {
      if (isDraftRevisionOutstanding(s)) {
        return {
          blocked: true,
          code: QuestErrorCode.DRAFT_REVIEW_REQUIRED,
          stateName: "DRAFT_REVISION_PENDING",
          reason:
            "A draft plan review returned REVISE \u2014 research and non-quest reads are blocked until you revise the plan.",
          requiredAction:
            `Edit \`.pi/quest/future/${s.activeDraft}.md\` (the \`## Implementation Plan\` section) to address the reviewer's findings, then save. Saving triggers re-review automatically.`,
        };
      }
    } catch {}
    return {
      blocked: true,
      code: QuestErrorCode.RESEARCH_REQUIRED,
      stateName: "PROVISIONAL_RESEARCH_PENDING",
      reason:
        "Initial orientation & research required to understand the objective and establish the quest identity before modifying project code.",
      requiredAction:
        "Investigate relevant architecture and code paths using read/search/bash tools, establish a concise semantic quest identity, and call quest_update_state to initialize the durable quest with your research findings.",
    };
  }
  if (!s.active) {
    return {
      blocked: false,
      code: QuestErrorCode.IMPLEMENTATION_BLOCKED,
      stateName: "IDLE",
      reason: "",
      requiredAction: "",
    };
  }
  if (s.pendingSubquestResume && s.pendingSubquestResume !== s.active) {
    return {
      blocked: true,
      code: QuestErrorCode.PENDING_RESUME_INCONSISTENT,
      stateName: "PENDING_RESUME_INCONSISTENT",
      reason:
        `A pending sub-quest continuation exists for '${s.pendingSubquestResume}', but the current authoritative quest state does not establish that continuation as active or completed.`,
      requiredAction:
        "reconcile the quest hierarchy and durable child/parent state before continuing.",
    };
  }
  if (s.activeTransaction && s.activeTransaction.phase === "inconsistent") {
    return {
      blocked: true,
      code: QuestErrorCode.RESUME_STATE_INCONSISTENT,
      stateName: "RESUME_STATE_INCONSISTENT",
      reason:
        "The completed compaction transaction no longer matches the durable checkpoint that was prepared for this transaction.",
      requiredAction: `Investigate the discrepancy in ${
        questPath(s.questId)
      }, update the epistemic state, and complete reassessment via quest_update_state({ reassessmentComplete: true, reassessmentConclusion: "..." }) to reconcile the transaction before modifying project code.`,
    };
  }
  if (s.activeTransaction && s.activeTransaction.phase === "failed") {
    return {
      blocked: true,
      code: QuestErrorCode.COMPACTION_FAILURE,
      stateName: "COMPACTION_TRANSACTION_FAILED",
      reason: s.activeTransaction.error || "The compaction transaction failed.",
      requiredAction: `Investigate the failure in ${
        questPath(s.questId)
      }, update the epistemic state, and complete reassessment via quest_update_state({ reassessmentComplete: true, reassessmentConclusion: "..." }) to reconcile the transaction before modifying project code.`,
    };
  }
  if (s.compactionPending) {
    return {
      blocked: true,
      code: QuestErrorCode.CHECKPOINT_REQUIRED,
      stateName: "COMPACTION_IN_PROGRESS",
      reason: "A compaction transaction is currently in progress.",
      requiredAction:
        "Wait for compaction to complete and deliver its autonomous resumption directive.",
    };
  }
  if (s.reassessmentRequired) {
    return {
      blocked: true,
      code: QuestErrorCode.REASSESSMENT_REQUIRED,
      stateName: "REASSESSMENT_PENDING",
      reason: s.reassessmentReason ||
        "The current plan has been invalidated by contradictory evidence or test failure.",
      requiredAction:
        `Investigate the contradiction, challenge prior assumptions, update ${
          questPath(s.questId)
        }, and complete reassessment via quest_update_state({ reassessmentComplete: true, reassessmentConclusion: "..." }) before modifying project code. Escapes: quest_rebut (present evidence to reviewer), quest_ask_human (escalate to human), quest_archive --abandon (record unresolved contradiction and archive).`,
    };
  }
  if (
    isRootQuest(s) &&
    (isSubagentToolRegistered(undefined, ctx) ||
      Boolean(getCustomSubagentRunner())) &&
    !isPlanReviewValidForState(s)
  ) {
    return {
      blocked: true,
      code: QuestErrorCode.PLAN_REVIEW_REQUIRED,
      stateName: "PLAN_REVIEW_PENDING",
      reason:
        "The current plan draft has not been approved by an independent adversarial review against the original user request.",
      requiredAction:
        "Submit the plan draft for independent critical review and obtain an explicit APPROVE verdict before modifying project code.",
    };
  }
  // Turn-stop gate A: awaitingReview scalar (plan_review / final_acceptance only, survives compaction)
  const aw = s.awaitingReview;
  if (aw && (aw.kind === "plan_review" || aw.kind === "final_acceptance")) {
    return {
      blocked: true,
      code: QuestErrorCode.PLAN_REVIEW_REQUIRED,
      stateName: "AWAITING_REVIEW",
      reason: `Plan review ${aw.reviewId} running — await verdict.`,
      requiredAction:
        "No writes until verdict; reads and quest_mark_saved allowed.",
    };
  }
  if (s.researchRequired || !s.researchComplete) {
    return {
      blocked: true,
      code: QuestErrorCode.RESEARCH_REQUIRED,
      stateName: "RESEARCH_PENDING",
      reason: `Research & falsification pass is pending (Round ${
        s.researchRound || 1
      }). Key architecture, module boundaries, and assumptions must be verified first.`,
      requiredAction: `Perform targeted read/search investigation, update ${
        questPath(s.questId)
      } with verified understanding and plan, and call quest_update_state({ researchComplete: true }) with medium or high confidence.`,
    };
  }
  if (isRootQuest(s) && s.awaitingUserConfirmation) {
    return {
      blocked: true,
      code: QuestErrorCode.CONFIRMATION_REQUIRED,
      stateName: "CONFIRMATION_PENDING",
      reason:
        "Root quest research is complete, but user confirmation is required before modifying project code.",
      requiredAction:
        "Present your research findings, tested assumptions, and proposed plan clearly to the user (using ask_questions or a plain text question), and wait for user confirmation before editing code.",
    };
  }
  return {
    blocked: false,
    code: QuestErrorCode.IMPLEMENTATION_BLOCKED,
    stateName: "IMPLEMENTATION_ALLOWED",
    reason: "",
    requiredAction: "",
  };
}

export function syncImplementationPermission(
  targetState?: StoredState,
  ctx?: ExtensionContext,
): boolean {
  const s = targetState || state;
  s.implementationAllowed = canImplement(s, ctx);
  return s.implementationAllowed;
}

export function getLifecycleState(
  targetState?: StoredState | ExtensionContext,
  _ctx?: ExtensionContext,
): QuestLifecycleState {
  const s = targetState && "active" in targetState
    ? targetState as StoredState
    : state;
  if (!s.active) return QuestLifecycleState.IDLE;
  if (s.compactionPending) return QuestLifecycleState.COMPACTING;

  if (s.reassessmentRequired) {
    return QuestLifecycleState.REASSESSMENT_PENDING;
  }

  if (s.researchRequired || !s.researchComplete) {
    return QuestLifecycleState.RESEARCH_PENDING;
  }

  if (s.dirty || !compactionReady()) {
    return QuestLifecycleState.ACTIVE_DIRTY;
  }
  return QuestLifecycleState.ACTIVE_CLEAN;
}
