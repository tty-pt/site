import { withContext } from "./context.ts";
import { canImplement, getImplementationBlockReason } from "./gates.ts";
import {
  logEvent,
  logGateTransition,
  logImplementationOutcome,
  logToolActivity,
  logToolAnomaly,
  normalizeLogPath,
} from "./logging.ts";
import {
  logDebug,
  reportAgentError,
  sendInternalAgentMessage,
} from "./messaging.ts";
import { questPath } from "./paths.ts";
import { persist } from "./persistence.ts";
import { getActiveContext, isRootQuest, state } from "./state.ts";
import {
  ExtensionAPI,
  ExtensionContext,
  QuestErrorCode,
  ToolPermission,
} from "./types.ts";
import {
  classifyBashCommand,
  classifyBashInvestigationKind,
  classifyInvestigationKind,
  classifySingleBashCommand,
  classifySingleBashInvestigationKind,
  classifyToolCall,
  hasFileRedirection,
  splitBashCommandChain,
} from "./utils.ts";

export {
  classifyBashCommand,
  classifyBashInvestigationKind,
  classifyInvestigationKind,
  classifySingleBashCommand,
  classifySingleBashInvestigationKind,
  classifyToolCall,
  hasFileRedirection,
  splitBashCommandChain,
};

export function canToolExecuteInCriticalReview(
  toolName: string,
  input?: any,
): boolean {
  const norm = (toolName || "").toLowerCase().trim();
  // B1' allows rebut/human escalation even during critical review
  if (norm === "quest_rebut" || norm === "quest_ask_human") return true;
  if (
    norm === "edit" ||
    norm === "write" ||
    norm === "user_edit" ||
    norm === "user_write" ||
    norm === "bg_run" ||
    norm === "bg_run_pi_attested" ||
    norm === "bg_kill" ||
    norm === "manage_adr" ||
    norm === "delete_project" ||
    norm === "index_repository" ||
    norm.startsWith("quest_")
  ) {
    return false;
  }
  if (norm === "bash" || norm === "user_bash") {
    const cmd = typeof input === "string"
      ? input
      : typeof input?.command === "string"
      ? input.command
      : typeof input?.cmd === "string"
      ? input.cmd
      : "";
    return classifyBashCommand(cmd) === "read";
  }
  const perm = classifyToolCall(toolName, input);
  return perm === "read" || (perm === "research" && norm !== "subagent") ||
    perm === "interaction";
}

export function checkAndEmitGateContinuationSteer(
  pi: ExtensionAPI,
  ctx?: ExtensionContext,
  targetSlug?: string,
  contextReason?: {
    wasReassessmentPending?: boolean;
    wasResearchPending?: boolean;
    wasAwaitingConfirmation?: boolean;
  },
) {
  const c = getActiveContext(ctx);
  const s = state;
  const slug = targetSlug || s.active;
  if (!slug) return;

  const transitionKey = `${slug}:v${s.planVersion || 1}:r${
    s.researchRound || 1
  }:rv${s.resolvedReassessmentVersion || 0}:conf${
    s.confirmedQuests?.includes(slug) ? "1" : "0"
  }:impl${canImplement(s, c) ? "1" : "0"}`;

  if (s.lastContinuationTransitionKey === transitionKey) {
    return;
  }
  s.lastContinuationTransitionKey = transitionKey;
  persist(pi, c);

  const qPath = questPath(slug);

  if (contextReason?.wasReassessmentPending) {
    logGateTransition(
      "GATE_OPENED",
      "reassessment resolved, implementation gate opened",
      {
        quest: slug,
        from: "REASSESSMENT_PENDING",
        to: "IMPLEMENTATION_ALLOWED",
      },
    );
    const steerMsg = `⚡ **REASSESSMENT RESOLVED — IMPLEMENTATION GATE OPEN** ⚡

The required reassessment has been completed and the replacement epistemic state has been validated.

Your implementation gate is now OPEN for \`${qPath}\`.

Do not stop after updating the quest file.
Re-read the current quest state if necessary and continue autonomously with the newly justified EXACT NEXT ACTION.

Do not blindly repeat the action that was previously blocked if the reassessment changed the plan; follow the revised plan and current Exact Next Action.`;

    sendInternalAgentMessage(pi, steerMsg, "steer", "reassessment_resolved");
    return;
  }

  if (contextReason?.wasAwaitingConfirmation) {
    logGateTransition(
      "GATE_OPENED",
      "confirmation accepted, implementation gate opened",
      {
        quest: slug,
        from: "CONFIRMATION_PENDING",
        to: "IMPLEMENTATION_ALLOWED",
      },
    );
    const steerMsg = `⚡ **CONFIRMATION ACCEPTED — IMPLEMENTATION GATE OPEN** ⚡

User confirmation has been recorded for active quest \`${qPath}\`.
Your implementation gate is now OPEN.

Proceed autonomously with the justified EXACT NEXT ACTION from your execution plan.`;

    sendInternalAgentMessage(pi, steerMsg, "steer", "confirmation_accepted");
    return;
  }

  if (contextReason?.wasResearchPending) {
    const isRoot = isRootQuest(s);
    const isConfirmed = Array.isArray(s.confirmedQuests) &&
      s.confirmedQuests.includes(slug);
    if (isRoot && !isConfirmed) {
      logGateTransition(
        "GATE_STATE_CHANGED",
        "research complete, confirmation required",
        {
          quest: slug,
          from: "RESEARCH_PENDING",
          to: "CONFIRMATION_PENDING",
        },
      );
      const steerMsg =
        `⚡ **RESEARCH COMPLETE — PRESENT PLAN FOR CONFIRMATION** ⚡

Initial research prerequisites have been satisfied and recorded in \`${qPath}\`.

Present your research findings, key assumptions evaluated, and revised execution plan clearly to the user, and request confirmation before editing feature code.`;

      sendInternalAgentMessage(pi, steerMsg, "steer", "research_complete_root");
    } else {
      logGateTransition(
        "GATE_OPENED",
        "research complete, implementation gate opened",
        {
          quest: slug,
          from: "RESEARCH_PENDING",
          to: "IMPLEMENTATION_ALLOWED",
        },
      );
      const steerMsg = `⚡ **RESEARCH COMPLETE — AUTONOMOUS EXECUTION OPEN** ⚡

Initial research prerequisites have been satisfied and verified for \`${qPath}\`.
Your implementation gate is now OPEN.

Proceed autonomously with the justified EXACT NEXT ACTION from your execution plan.`;

      sendInternalAgentMessage(
        pi,
        steerMsg,
        "steer",
        "research_complete_autonomous",
      );
    }
    return;
  }

  // Generic implementation gate open steer
  const steerMsg = `⚡ **IMPLEMENTATION GATE OPEN** ⚡

Prerequisites have been satisfied for \`${qPath}\`.
Your implementation gate is now OPEN.

Proceed autonomously with the justified EXACT NEXT ACTION from your execution plan.`;

  sendInternalAgentMessage(pi, steerMsg, "steer", "gate_open");
}

export function installToolCallGate(pi: ExtensionAPI) {
  pi.on(
    "tool_call",
    withContext(async (event: any, ctx: ExtensionContext) => {
      if (!state.active && !state.pendingRootQuest) return;
      const toolName = event?.toolName || "";
      const permission = classifyToolCall(toolName, event?.input);

      // Critical review execution is strictly read-only
      if (state.inCriticalReview) {
        if (!canToolExecuteInCriticalReview(toolName, event?.input)) {
          const correlationId = `critic_gate_${Date.now().toString(36)}_${
            Math.random().toString(36).slice(2, 6)
          }`;
          const blockMessage =
            `[Quest Journal Gate: Critical Review Read-Only Enforcement]

Tool: ${toolName}
Permission: ${permission}

Critical review execution is strictly read-only. The critical reviewer is not permitted to edit files, write files, mutate git state, change dependencies, or otherwise modify project state.

Allowed for critical review:
- read files (read, doc_to_md)
- search codebase and memory (search_graph, search_code, rg, grep)
- inspect git diffs and status (git status, git diff, git log)
- run safe read-only commands (pwd, ls, stat, wc)`;

          logGateTransition(
            "GATE_BLOCKED",
            "critical review read-only enforcement",
            {
              quest: state.active || "",
              tool: toolName,
              permission,
              correlationId,
            },
          );

          reportAgentError(
            pi,
            ctx,
            `Critical review tool '${toolName}' blocked: Reviewer is strictly read-only.`,
            {
              code: QuestErrorCode.IMPLEMENTATION_BLOCKED,
              correlationId,
              deliverAs: "steer",
              requiredNextAction:
                "Use read/search tools only during critical review.",
              details: {
                Tool: toolName,
                Permission: permission,
                ReviewState: "IN_CRITICAL_REVIEW",
              },
            },
          );

          return {
            block: true,
            reason: blockMessage,
          };
        }
        return;
      }

      // AWAITING_REVIEW scalar gate (A): plan_review / final_acceptance only, blocks writes but allows reads + quest_mark_saved
      const awGate = state.awaitingReview as
        | {
          kind: string;
          reviewId: string;
          triggerReason?: string;
          since: number;
        }
        | null
        | undefined;
      const isAwaitingReview = Boolean(
        awGate &&
          (awGate.kind === "plan_review" || awGate.kind === "final_acceptance"),
      );
      if (isAwaitingReview) {
        const normTool = (toolName || "").toLowerCase().trim();
        // Allow reads, research, interaction, and quest_mark_saved (journal save marker)
        if (normTool === "quest_mark_saved") {
          return;
        }
        // Allow read/research/interaction
        if (
          permission === "read" || permission === "research" ||
          permission === "interaction"
        ) {
          return;
        }
        // For journal, only quest_mark_saved is allowed above; quest_update_state and other journals block
        // Fall through to GATE_BLOCKED for implementation/unknown/journal(edit/write) when awaiting
        const isBlockedJournal = permission === "journal" &&
          normTool !== "quest_mark_saved";
        if (
          isBlockedJournal || permission === "implementation" ||
          permission === "unknown"
        ) {
          const gate = getImplementationBlockReason(state, ctx);
          // If gates already says AWAITING_REVIEW, use it; otherwise synthesize
          const gateState = gate.blocked && gate.stateName === "AWAITING_REVIEW"
            ? gate
            : {
              blocked: true,
              code: QuestErrorCode.PLAN_REVIEW_REQUIRED,
              stateName: "AWAITING_REVIEW",
              reason: `Plan review ${
                awGate!.reviewId
              } running — await verdict.`,
              requiredAction:
                "No writes until verdict; reads and quest_mark_saved allowed.",
            };
          const correlationId = `await_${Date.now().toString(36)}_${
            Math.random().toString(36).slice(2, 6)
          }`;
          const blockMessage = `[Quest Journal Gate: Blocked]

Tool: ${toolName}
Permission: ${permission}
State: ${gateState.stateName}

This operation may modify project state, but implementation is currently forbidden.

Quest: ${state.questId ? questPath(state.questId) : "(Provisional)"}
Reason: ${gateState.reason}
Required next step: ${gateState.requiredAction}

Allowed now:
- read/search/investigate
- quest_mark_saved

Required: await review verdict.`;
          logGateTransition(
            "GATE_BLOCKED",
            `gate blocked: ${gateState.stateName}`,
            {
              quest: state.active || "",
              gate: gateState.stateName,
              activeGate: gateState.stateName,
              reason: gateState.reason,
              requiredAction: gateState.requiredAction,
              correlationId,
              consequence: "OPERATION_BLOCKED",
            },
          );
          reportAgentError(
            pi,
            ctx,
            `Tool '${toolName}' execution blocked in state ${gateState.stateName}: ${gateState.reason}`,
            {
              code: gateState.code,
              correlationId,
              deliverAs: "steer",
              requiredNextAction: gateState.requiredAction,
              details: {
                Tool: toolName,
                Permission: permission,
                State: gateState.stateName,
                Reason: gateState.reason,
              },
            },
          );
          return { block: true, reason: blockMessage };
        }
        // Non-blocked journal (none) already returned; otherwise allow
        return;
      }

      // Allow all read, research, journal, and interaction operations
      if (
        permission === "read" || permission === "research" ||
        permission === "journal" || permission === "interaction"
      ) {
        return;
      }

      if (permission === "unknown") {
        logToolAnomaly("UNKNOWN_TOOL", `unknown tool invoked: ${toolName}`, {
          quest: state.active || "",
          tool: toolName,
          permission: "unknown",
        });
      }

      const gate = getImplementationBlockReason(state, ctx);
      const targetPath = event?.input?.path || event?.input?.file || "";
      // 11: direct quest.md write via bash must be steered to quest_update_state before attempt burns failures
      const rawCmd =
        (event?.input?.command || event?.input?.cmd || "") as string;
      const isDirectQuestWrite =
        gate.stateName === "PROVISIONAL_RESEARCH_PENDING" &&
        (toolName || "").toLowerCase() === "bash" &&
        hasFileRedirection(rawCmd) && /quest\.md/.test(rawCmd);
      const effectiveRequiredAction = isDirectQuestWrite
        ? `Do NOT write ${
          /\S*quest\.md/.exec(rawCmd)?.[0] || "quest.md"
        } via bash/cat/echo — use quest_update_state to initialize the durable quest with research findings (see Required next step).`
        : gate.requiredAction;
      const effectiveReason = isDirectQuestWrite
        ? `${gate.reason} Direct bash write to quest.md is forbidden before quest_update_state.`
        : gate.reason;
      if (
        gate.blocked &&
        (permission === "implementation" || permission === "unknown")
      ) {
        const correlationId = `gate_${Date.now().toString(36)}_${
          Math.random().toString(36).slice(2, 6)
        }`;
        const questLabel = state.questId
          ? questPath(state.questId)
          : "(Provisional Root Quest Initializing)";
        const errorCode = permission === "unknown"
          ? QuestErrorCode.UNKNOWN_TOOL_BLOCKED
          : (gate.code || QuestErrorCode.IMPLEMENTATION_BLOCKED);
        const escapeHint = gate.stateName === "REASSESSMENT_PENDING" ||
            gate.stateName === "PLAN_REVIEW_PENDING" ||
            gate.stateName === "AWAITING_REVIEW"
          ? "\nEscapes: quest_rebut (argue with reviewer), quest_ask_human (escalate to human), quest_archive --abandon (record contradiction and archive), quest_update_state (revise plan & reassessmentConclusion)"
          : permission === "unknown"
          ? "\nIf tool not found: allowed tools are quest_update_state, quest_mark_saved, quest_archive, quest_subquest, quest_rebut, quest_ask_human, read, bash, grep, etc."
          : "";
        const blockMessage = `[Quest Journal Gate: Blocked]

Tool: ${toolName}
Permission: ${permission}
State: ${gate.stateName}

This operation may modify project state, but implementation is currently forbidden.

Quest: ${questLabel}
Reason: ${effectiveReason}
Required next step: ${effectiveRequiredAction}${escapeHint}

Allowed now:
- read/search/investigate
- inspect architecture
- update the quest journal

Required:
complete the current research/reassessment prerequisite and reopen the implementation gate.`;

        const failureId = `block_${Date.now().toString(36)}_${
          Math.random().toString(36).slice(2, 6)
        }`;
        state.lastFailureId = failureId;

        logGateTransition("GATE_BLOCKED", `gate blocked: ${gate.stateName}`, {
          quest: state.active || "",
          gate: gate.stateName,
          activeGate: gate.stateName,
          reason: effectiveReason,
          requiredAction: effectiveRequiredAction,
          correlationId,
          failureId,
          consequence: "OPERATION_BLOCKED",
        });

        logImplementationOutcome(
          "IMPLEMENTATION_ATTEMPT",
          `attempted ${toolName} ${targetPath ? `(${targetPath})` : ""}`.trim(),
          {
            quest: state.active || "",
            tool: toolName,
            path: targetPath,
            allowed: false,
            correlationId,
            failureId,
            consequence: "OPERATION_BLOCKED",
          },
        );
        logImplementationOutcome(
          "IMPLEMENTATION_BLOCKED",
          `blocked by gate ${gate.stateName}: ${effectiveReason}`,
          {
            quest: state.active || "",
            tool: toolName,
            gate: gate.stateName,
            activeGate: gate.stateName,
            code: errorCode,
            reason: effectiveReason,
            correlationId,
            failureId,
            consequence: "OPERATION_BLOCKED",
          },
        );

        const cmd = event?.input?.command || event?.input?.cmd || "";
        logToolActivity(toolName, "blocked", {
          quest: state.active || "",
          gate: gate.stateName,
          activeGate: gate.stateName,
          phase: "implementation",
          path: targetPath ? normalizeLogPath(targetPath) : undefined,
          command: cmd ? String(cmd).slice(0, 150) : undefined,
          reason: effectiveReason,
          turn: state.currentTurn,
          correlationId,
          failureId,
          consequence: "OPERATION_BLOCKED",
        });

        reportAgentError(
          pi,
          ctx,
          `Tool '${toolName}' execution blocked in state ${gate.stateName}: ${effectiveReason}`,
          {
            code: errorCode,
            correlationId,
            deliverAs: "steer",
            requiredNextAction: effectiveRequiredAction + escapeHint,
            details: {
              Tool: toolName,
              Permission: permission,
              State: gate.stateName,
              Quest: questLabel,
              Reason: effectiveReason,
            },
          },
        );

        return {
          block: true,
          reason: blockMessage,
        };
      }

      if (permission === "implementation") {
        logImplementationOutcome(
          "IMPLEMENTATION_ATTEMPT",
          `allowed ${toolName} ${targetPath ? `(${targetPath})` : ""}`.trim(),
          {
            quest: state.active || "",
            tool: toolName,
            path: targetPath,
            allowed: true,
          },
        );
        logImplementationOutcome(
          "IMPLEMENTATION_ALLOWED",
          `implementation allowed for ${toolName}`,
          {
            quest: state.active || "",
            tool: toolName,
            path: targetPath,
          },
        );
      }
    }),
  );
}
