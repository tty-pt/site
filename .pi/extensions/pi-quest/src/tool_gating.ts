import { withContext } from "./context.ts";
import { canImplement, getImplementationBlockReason } from "./gates.ts";
import { logEvent, logGateTransition, logImplementationOutcome, logToolActivity, logToolAnomaly, normalizeLogPath } from "./logging.ts";
import { logDebug, reportAgentError, sendInternalAgentMessage } from "./messaging.ts";
import { questPath } from "./paths.ts";
import { persist } from "./persistence.ts";
import { getActiveContext, isRootQuest, state } from "./state.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode, ToolPermission } from "./types.ts";
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

export function canToolExecuteInCriticalReview(toolName: string, input?: any): boolean {
	const norm = (toolName || "").toLowerCase().trim();
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
		const cmd = typeof input === "string" ? input : typeof input?.command === "string" ? input.command : typeof input?.cmd === "string" ? input.cmd : "";
		return classifyBashCommand(cmd) === "read";
	}
	const perm = classifyToolCall(toolName, input);
	return perm === "read" || (perm === "research" && norm !== "subagent") || perm === "interaction";
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

	const transitionKey = `${slug}:v${s.planVersion || 1}:r${s.researchRound || 1}:rv${s.resolvedReassessmentVersion || 0}:conf${s.confirmedQuests?.includes(slug) ? "1" : "0"}:impl${canImplement(s, c) ? "1" : "0"}`;

	if (s.lastContinuationTransitionKey === transitionKey) {
		return;
	}
	s.lastContinuationTransitionKey = transitionKey;
	persist(pi, c);

	const qPath = questPath(slug);

	if (contextReason?.wasReassessmentPending) {
		logGateTransition("GATE_OPENED", "reassessment resolved, implementation gate opened", {
			quest: slug,
			from: "REASSESSMENT_PENDING",
			to: "IMPLEMENTATION_ALLOWED",
		});
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
		logGateTransition("GATE_OPENED", "confirmation accepted, implementation gate opened", {
			quest: slug,
			from: "CONFIRMATION_PENDING",
			to: "IMPLEMENTATION_ALLOWED",
		});
		const steerMsg = `⚡ **CONFIRMATION ACCEPTED — IMPLEMENTATION GATE OPEN** ⚡

User confirmation has been recorded for active quest \`${qPath}\`.
Your implementation gate is now OPEN.

Proceed autonomously with the justified EXACT NEXT ACTION from your execution plan.`;

		sendInternalAgentMessage(pi, steerMsg, "steer", "confirmation_accepted");
		return;
	}

	if (contextReason?.wasResearchPending) {
		const isRoot = isRootQuest(s);
		const isConfirmed = Array.isArray(s.confirmedQuests) && s.confirmedQuests.includes(slug);
		if (isRoot && !isConfirmed) {
			logGateTransition("GATE_STATE_CHANGED", "research complete, confirmation required", {
				quest: slug,
				from: "RESEARCH_PENDING",
				to: "CONFIRMATION_PENDING",
			});
			const steerMsg = `⚡ **RESEARCH COMPLETE — PRESENT PLAN FOR CONFIRMATION** ⚡

Initial research prerequisites have been satisfied and recorded in \`${qPath}\`.

Present your research findings, key assumptions evaluated, and revised execution plan clearly to the user, and request confirmation before editing feature code.`;

			sendInternalAgentMessage(pi, steerMsg, "steer", "research_complete_root");
		} else {
			logGateTransition("GATE_OPENED", "research complete, implementation gate opened", {
				quest: slug,
				from: "RESEARCH_PENDING",
				to: "IMPLEMENTATION_ALLOWED",
			});
			const steerMsg = `⚡ **RESEARCH COMPLETE — AUTONOMOUS EXECUTION OPEN** ⚡

Initial research prerequisites have been satisfied and verified for \`${qPath}\`.
Your implementation gate is now OPEN.

Proceed autonomously with the justified EXACT NEXT ACTION from your execution plan.`;

			sendInternalAgentMessage(pi, steerMsg, "steer", "research_complete_autonomous");
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
					const correlationId = `critic_gate_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;
					const blockMessage = `[Quest Journal Gate: Critical Review Read-Only Enforcement]

Tool: ${toolName}
Permission: ${permission}

Critical review execution is strictly read-only. The critical reviewer is not permitted to edit files, write files, mutate git state, change dependencies, or otherwise modify project state.

Allowed for critical review:
- read files (read, doc_to_md)
- search codebase and memory (search_graph, search_code, rg, grep)
- inspect git diffs and status (git status, git diff, git log)
- run safe read-only commands (pwd, ls, stat, wc)`;

					logGateTransition("GATE_BLOCKED", "critical review read-only enforcement", {
						quest: state.active || "",
						tool: toolName,
						permission,
						correlationId,
					});

					reportAgentError(
						pi,
						ctx,
						`Critical review tool '${toolName}' blocked: Reviewer is strictly read-only.`,
						{
							code: QuestErrorCode.IMPLEMENTATION_BLOCKED,
							correlationId,
							deliverAs: "steer",
							requiredNextAction: "Use read/search tools only during critical review.",
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

			// Allow all read, research, journal, and interaction operations
			if (permission === "read" || permission === "research" || permission === "journal" || permission === "interaction") {
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
			if (gate.blocked && (permission === "implementation" || permission === "unknown")) {
				const correlationId = `gate_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;
				const questLabel = state.questId ? questPath(state.questId) : "(Provisional Root Quest Initializing)";
				const errorCode = permission === "unknown" ? QuestErrorCode.UNKNOWN_TOOL_BLOCKED : (gate.code || QuestErrorCode.IMPLEMENTATION_BLOCKED);
				const blockMessage = `[Quest Journal Gate: Blocked]

Tool: ${toolName}
Permission: ${permission}
State: ${gate.stateName}

This operation may modify project state, but implementation is currently forbidden.

Quest: ${questLabel}
Reason: ${gate.reason}
Required next step: ${gate.requiredAction}

Allowed now:
- read/search/investigate
- inspect architecture
- update the quest journal

Required:
complete the current research/reassessment prerequisite and reopen the implementation gate.`;

				logGateTransition("GATE_BLOCKED", `gate blocked: ${gate.stateName}`, {
					quest: state.active || "",
					gate: gate.stateName,
					reason: gate.reason,
					requiredAction: gate.requiredAction,
					correlationId,
				});

				logImplementationOutcome("IMPLEMENTATION_ATTEMPT", `attempted ${toolName} ${targetPath ? `(${targetPath})` : ""}`.trim(), {
					quest: state.active || "",
					tool: toolName,
					path: targetPath,
					allowed: false,
					correlationId,
				});
				logImplementationOutcome("IMPLEMENTATION_BLOCKED", `blocked by gate ${gate.stateName}: ${gate.reason}`, {
					quest: state.active || "",
					tool: toolName,
					gate: gate.stateName,
					code: errorCode,
					reason: gate.reason,
					correlationId,
				});

				const cmd = event?.input?.command || event?.input?.cmd || "";
				logToolActivity(toolName, "blocked", {
					quest: state.active || "",
					gate: gate.stateName,
					phase: "implementation",
					path: targetPath ? normalizeLogPath(targetPath) : undefined,
					command: cmd ? String(cmd).slice(0, 150) : undefined,
					reason: gate.reason,
					turn: state.currentTurn,
					correlationId,
				});

				reportAgentError(
					pi,
					ctx,
					`Tool '${toolName}' execution blocked in state ${gate.stateName}: ${gate.reason}`,
					{
						code: errorCode,
						correlationId,
						deliverAs: "steer",
						requiredNextAction: gate.requiredAction,
						details: {
							Tool: toolName,
							Permission: permission,
							State: gate.stateName,
							Quest: questLabel,
							Reason: gate.reason,
						},
					},
				);

				return {
					block: true,
					reason: blockMessage,
				};
			}

			if (permission === "implementation") {
				logImplementationOutcome("IMPLEMENTATION_ATTEMPT", `allowed ${toolName} ${targetPath ? `(${targetPath})` : ""}`.trim(), {
					quest: state.active || "",
					tool: toolName,
					path: targetPath,
					allowed: true,
				});
				logImplementationOutcome("IMPLEMENTATION_ALLOWED", `implementation allowed for ${toolName}`, {
					quest: state.active || "",
					tool: toolName,
					path: targetPath,
				});
			}
		}),
	);
}
