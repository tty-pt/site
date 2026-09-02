import { MajorPhaseName, QuestLogEventType } from "../types.ts";

export function mapEventTypeToMajorPhase(type: QuestLogEventType | string, context?: Record<string, string>): MajorPhaseName | null {
  switch (type) {
    case "TOOL_ACTIVITY":
      if (context?.phase) {
        const p = String(context.phase).toUpperCase();
        if (p === "RESEARCH") return "RESEARCH";
        if (p === "PLANNING") return "PLANNING";
        if (p === "IMPLEMENTATION") return "IMPLEMENTATION";
        if (p === "VERIFICATION") return "VERIFICATION";
        if (p === "REASSESSMENT") return "REASSESSMENT";
        if (p === "CHECKPOINT") return "CHECKPOINT";
        if (p === "COMPACTION") return "COMPACTION";
        if (p === "RESUME") return "RESUME";
        if (p === "RECOVERY") return "RECOVERY";
        if (p === "COMPLETION") return "COMPLETION";
      }
      return null;

    case "QUEST_DETECTED":
    case "QUEST_REUSED":
    case "QUEST_REUSED_COALESCED":
    case "QUEST_CREATED":
    case "QUEST_START":
    case "QUEST_SWITCH":
    case "QUEST_INITIALIZATION_FAILED":
    case "QUEST_ACTIVATION_FAILED":
      return "INITIALIZATION";

    case "RESEARCH_REQUIRED":
    case "RESEARCH_EVIDENCE":
    case "RESEARCH_REJECTED":
    case "RESEARCH_COMPLETED":
      return "RESEARCH";

    case "STATE_UPDATE_REJECTED":
    case "STATE_UPDATE_ACCEPTED":
    case "STATE_UPDATE_FAILED":
    case "STATE_RECONCILIATION_REQUIRED":
      return "PLANNING";

    case "CONFIRMATION_REQUESTED":
    case "CONFIRMATION_RECEIVED":
    case "CONFIRMATION_REJECTED":
    case "USER_REFINEMENT_RECEIVED":
      return "CONFIRMATION";

    case "IMPLEMENTATION_ATTEMPT":
    case "IMPLEMENTATION_ALLOWED":
    case "IMPLEMENTATION_BLOCKED":
    case "IMPLEMENTATION_COMPLETED":
    case "IMPLEMENTATION_FAILED":
    case "UNKNOWN_TOOL":
    case "UNEXPECTED_TOOL_RESULT":
    case "TOOL_CLASSIFICATION_MISMATCH":
    case "TOOL_FAILURE":
    case "TOOL_TIMEOUT":
    case "TOOL_CANCELLED":
      return "IMPLEMENTATION";

    case "TEST_STARTED":
    case "TEST_PASSED":
    case "TEST_FAILED":
    case "BUILD_STARTED":
    case "BUILD_PASSED":
    case "BUILD_FAILED":
    case "TEST_FAILURE":
      return "VERIFICATION";

    case "REASSESSMENT_REQUIRED":
    case "REASSESSMENT_EVIDENCE":
    case "REASSESSMENT_REJECTED":
    case "REASSESSMENT_COMPLETED":
    case "REASSESSMENT_RESOLUTION_FAILED":
      return "REASSESSMENT";

    case "CHECKPOINT":
    case "SAVE_STARTED":
    case "SAVE_VERIFIED":
    case "SAVE_REJECTED":
    case "SAVE_FAILED":
    case "PERSISTENCE_DEGRADED":
    case "PERSISTENCE_RECOVERED":
      return "CHECKPOINT";

    case "COMPACTION_PREPARED":
    case "COMPACTION_INVALIDATED":
    case "COMPACTION_STARTED":
    case "COMPACTION_COMPLETED":
    case "COMPACTION_FAILED":
    case "COMPACTION_INCONSISTENT":
    case "COMPACTION_EXTERNAL":
    case "COMPACTION_BLOCKED":
      return "COMPACTION";

    case "RESUME_OBLIGATION_CREATED":
    case "RESUME_ATTEMPTED":
    case "RESUME_DELIVERED":
    case "RESUME_FAILED":
    case "RESUME_RETRIED":
    case "RESUME_RECONCILIATION_REQUIRED":
    case "RESUME_OBSOLETED":
      return "RESUME";

    case "STATE_INCONSISTENT":
    case "RECOVERY_STARTED":
    case "RECOVERY_COMPLETED":
    case "RECOVERY_FAILED":
    case "ERROR":
    case "NO_PROGRESS":
    case "REPEATED_BLOCK":
    case "REPEATED_FAILURE":
      return "RECOVERY";

    case "SUBQUEST_START":
    case "SUBQUEST_SWITCH":
    case "SUBQUEST_RETURN":
    case "SUBQUEST_FAILED":
    case "SUBQUEST_RESUME_PENDING":
    case "SUBQUEST_RESUME_FAILED":
    case "SUBQUEST_COMPLETE":
    case "ARCHIVE":
      return "COMPLETION";

    case "CRITICAL_REVIEW_REQUESTED":
    case "CRITICAL_REVIEW_STARTED":
    case "CRITICAL_REVIEW_PASSED":
    case "CRITICAL_REVIEW_FAILED":
    case "CRITICAL_REVIEW_UNCERTAIN":
    case "CRITICAL_REVIEW_UNAVAILABLE":
    case "CRITICAL_REVIEW_ERROR":
    case "CRITICAL_REVIEW_SUPPRESSED_DUPLICATE":
    case "CRITICAL_REVIEW_COALESCED":
    case "GLOBAL_REVIEW_CAP_HIT":
    case "DIRECTION_REVIEW_THROTTLED":
    case "PLAN_REVIEW_REQUESTED":
    case "PLAN_REVIEW_STARTED":
    case "PLAN_REVIEW_APPROVED":
    case "PLAN_REVIEW_FAILED":
    case "PLAN_REVIEW_UNCERTAIN":
    case "REMEDIATION_REQUIRED":
    case "SELF_CRITIQUE_STARTED":
    case "SELF_CRITIQUE_REVISED":
    case "SUBAGENT_CWD_REANCHORED":
      return "VERIFICATION";

    case "GATE_BLOCKED":
    case "GATE_OPENED":
    case "GATE_STATE_CHANGED":
    case "TURN_START":
    case "TURN_END":
    case "AGENT_MESSAGE_ATTEMPTED":
    case "AGENT_MESSAGE_DELIVERED":
    case "AGENT_MESSAGE_FAILED":
    case "AGENT_MESSAGE_QUEUED":
    case "AGENT_MESSAGE_RETRIED":
    default:
      return null;
  }
}
