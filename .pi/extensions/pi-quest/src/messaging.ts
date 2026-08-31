import { INTERNAL_MESSAGE_PREFIX, QuestErrorCode, SYNTHETIC_PROMPT_PREFIXES } from "./constants.ts";
import { logAgentMessageTransition, logEvent } from "./logging.ts";
import { createAgentObligation, getPendingObligations, queueAgentObligation, reconcileObligations } from "./obligations.ts";
import { questPath } from "./paths.ts";
import { persist } from "./persistence.ts";
import { getActiveContext, getSessionId, getState, sessionStates, setSessionState, snapshotState, state } from "./state.ts";
import { getQuestLockKey, isQuestLocked } from "./utils/mutex.ts";
import { AgentErrorOptions, ExtensionAPI, ExtensionContext, PendingAgentNotification } from "./types.ts";

/**
 * ARCHITECTURAL INVARIANT:
 * Any extension event that changes what the agent is permitted or required to do
 * must produce a model-visible message describing the state change and required next action.
 * UI notifications and diagnostic logs are never considered sufficient model feedback.
 */

export function logError(
	msg: string,
	err?: any,
	ctx?: ExtensionContext,
	code?: QuestErrorCode | string,
	correlationId?: string,
) {
	const errMsg = err?.message ? `${msg}: ${err.message}` : msg;
	const errorCode = code || (typeof err?.code === "string" ? err.code : undefined) || "ERROR";
	logEvent("ERROR", errMsg, {
		quest: state?.active || "",
		code: errorCode,
		error: err?.message,
		correlationId,
	});
	if (ctx?.hasUI && typeof ctx.ui?.notify === "function") {
		ctx.ui.notify(`Quest Journal Error: ${errMsg}`, "error");
	}
}

export function logDebug(_msg: string) {
	// Debug logging is routed through logEvent when high-level, or no-op.
}

export function safeSendUserMessage(
	pi: ExtensionAPI,
	text: string,
	options?: { deliverAs?: "steer" | "followUp" | "nextTurn"; expandPromptTemplates?: boolean },
): boolean {
	try {
		if (options) {
			pi.sendUserMessage(text, options);
		} else {
			pi.sendUserMessage(text);
		}
		return true;
	} catch (err: any) {
		logError("Failed to send user message to agent", err);
		return false;
	}
}

export function sendInternalAgentMessage(
	pi: ExtensionAPI,
	text: string,
	deliverAs: "steer" | "followUp" | "nextTurn" = "followUp",
	type?: string,
	correlationId?: string,
): boolean {
	logAgentMessageTransition("AGENT_MESSAGE_ATTEMPTED", `agent message attempted (${deliverAs})`, { deliverAs, type, correlationId });
	if (typeof pi.sendMessage === "function") {
		try {
			pi.sendMessage(
				{
					customType: "quest_journal",
					content: text,
					display: false,
				},
				{
					deliverAs,
				},
			);
			logAgentMessageTransition("AGENT_MESSAGE_DELIVERED", `agent message delivered (${deliverAs})`, { deliverAs, type, correlationId });
			return true;
		} catch (err: any) {
			logAgentMessageTransition("AGENT_MESSAGE_FAILED", `agent message delivery failed (${deliverAs})`, { deliverAs, type, correlationId, error: err?.message });
			logError("Failed to send internal custom message", err, undefined, "MESSAGE_DELIVERY_FAILURE", correlationId);
		}
	}
	const marked = text.startsWith(INTERNAL_MESSAGE_PREFIX) ? text : `${INTERNAL_MESSAGE_PREFIX}\n${text}`;
	const sent = safeSendUserMessage(pi, marked, { deliverAs });
	if (sent) {
		logAgentMessageTransition("AGENT_MESSAGE_DELIVERED", `agent user message delivered (${deliverAs})`, { deliverAs, type, correlationId });
	} else {
		logAgentMessageTransition("AGENT_MESSAGE_FAILED", `agent user message failed (${deliverAs})`, { deliverAs, type, correlationId });
	}
	return sent;
}

export function formatAgentErrorMessage(
	code: string,
	message: string,
	requiredNextAction?: string,
	details?: Record<string, any> | string,
): string {
	const lines = [
		`[Quest Journal] ${code}`,
		"",
		message,
	];
	if (details) {
		if (typeof details === "string") {
			lines.push("", details);
		} else {
			lines.push("");
			for (const [k, v] of Object.entries(details)) {
				if (v !== undefined && v !== null) {
					lines.push(`${k}: ${v}`);
				}
			}
		}
	}
	if (requiredNextAction) {
		lines.push("", "Required next action:", requiredNextAction);
	}
	return lines.join("\n");
}

export function reportAgentError(
	pi: ExtensionAPI,
	ctx: ExtensionContext | undefined,
	message: string,
	options: AgentErrorOptions,
): boolean {
	logError(`[${options.code}] ${message}`, undefined, undefined, options.code, options.correlationId);
	const text = formatAgentErrorMessage(options.code, message, options.requiredNextAction, options.details);
	const deliverAs = options.deliverAs || "followUp";
	const messageType = typeof options.code === "string" ? options.code.toLowerCase() : "error";
	const delivered = sendInternalAgentMessage(pi, text, deliverAs, messageType, options.correlationId);

	if (delivered) {
		return true;
	}

	// Transport failed to deliver agent notification. Make the notification durable so it is not lost.
	try {
		const c = getActiveContext(ctx);
		const targetSessionId = getSessionId(c);
		const targetState = sessionStates.get(targetSessionId) ?? getState(c);

		const obligation = createAgentObligation(targetState, {
			kind: "error",
			code: options.code,
			message,
			deliverAs,
			requiredNextAction: options.requiredNextAction,
			details: options.details,
			correlationId: options.correlationId,
		});
		queueAgentObligation(targetState, obligation);
		persist(pi, ctx);
	} catch (err: any) {
		logError("Failed to persist pending agent notification", err, ctx, QuestErrorCode.PERSISTENCE_FAILURE, options.correlationId);
	}

	return false;
}

export function drainAgentObligations(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
): boolean {
	try {
		const c = getActiveContext(ctx);
		const targetSessionId = getSessionId(c);
		const targetState = sessionStates.get(targetSessionId) ?? getState(c);

		if (!Array.isArray(targetState.pendingNotifications) || targetState.pendingNotifications.length === 0) {
			return false;
		}

		const lockKey = getQuestLockKey(targetState.questId || targetState.active || "quest", targetSessionId);
		if (isQuestLocked(lockKey)) {
			return false;
		}

		reconcileObligations(targetState);

		const actionable = getPendingObligations(targetState);
		if (actionable.length === 0) {
			return false;
		}

		let anyDelivered = false;

		for (const obligation of actionable) {
			const text = formatAgentErrorMessage(
				String(obligation.code || "NOTIFICATION"),
				obligation.message,
				obligation.requiredNextAction,
				obligation.details,
			);
			const deliverAs = obligation.deliverAs || "followUp";
			const delivered = sendInternalAgentMessage(
				pi,
				text,
				deliverAs,
				String(obligation.code || "notification").toLowerCase(),
				obligation.correlationId,
			);

			obligation.attempts = (obligation.attempts || 0) + 1;
			obligation.lastAttemptAt = Date.now();

			if (delivered) {
				obligation.deliveredAt = Date.now();
				obligation.status = "delivering";
				anyDelivered = true;
				logAgentMessageTransition("AGENT_MESSAGE_RETRIED", `agent obligation delivered (${deliverAs}): [${obligation.code || obligation.kind}]`, {
					code: String(obligation.code || obligation.kind),
					correlationId: obligation.correlationId,
					attempts: obligation.attempts,
					obligationId: obligation.id,
				});
			} else {
				logAgentMessageTransition("AGENT_MESSAGE_FAILED", `agent obligation delivery failed (${deliverAs}): [${obligation.code || obligation.kind}]`, {
					code: String(obligation.code || obligation.kind),
					correlationId: obligation.correlationId,
					attempts: obligation.attempts,
					obligationId: obligation.id,
				});
			}
		}

		if (anyDelivered) {
			persist(pi, ctx);
		}

		return anyDelivered;
	} catch {
		return false;
	}
}

export function drainPendingAgentNotifications(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
): boolean {
	return drainAgentObligations(pi, ctx);
}

export function sendInternalUserMessage(pi: ExtensionAPI, text: string, options?: { deliverAs?: "steer" | "followUp" | "nextTurn"; expandPromptTemplates?: boolean }): boolean {
	const marked = text.startsWith(INTERNAL_MESSAGE_PREFIX) ? text : `${INTERNAL_MESSAGE_PREFIX}\n${text}`;
	return safeSendUserMessage(pi, marked, options);
}


export function sendSaveRequest(pi: ExtensionAPI, message: string) {
	if (!state.active) return;
	const promptReminder = `Original user request -- keep VERBATIM under ## Original request in the quest file:\n"${originalRequestText()}"${
		state.refinements && state.refinements.length > 0 ? `\n\nUser refinements -- list under ## Quest Refinements & User Feedback Loops:\n${refinementsBlock()}` : ""
	}`;
	const text = `${message}\n\n${promptReminder}\n\nActive quest file: \`${questPath(state.active)}\`\n\nUpdate that file with the latest state (goal, progress, decisions, files touched, findings, Test / Build Status, remaining work, next step). Ensure external working memory is updated accurately, then call \`quest_mark_saved\`.`;
	sendInternalAgentMessage(pi, text, "followUp", "checkpoint_required");
}

export function originalRequestText(): string {
	if (!state.prompts || state.prompts.length === 0) {
		return "(none captured yet -- this is the first substantive request; use the current user message)";
	}
	return state.prompts[0];
}

export function refinementsBlock(): string {
	if (!state.refinements || state.refinements.length === 0) {
		return "(none captured yet)";
	}
	return state.refinements.map((r, i) => `${i + 1}. ${r}`).join("\n\n");
}

export function promptsBlock(): string {
	const orig = originalRequestText();
	if (!state.refinements || state.refinements.length === 0) {
		return orig;
	}
	const refs = state.refinements.map((r, i) => `Refinement ${i + 1}: ${r}`).join("\n");
	return `Original Request:\n${orig}\n\nUser Refinements:\n${refs}`;
}

export function shouldCapturePrompt(text: string): boolean {
	const t = text.trim();
	if (!t || t.length < 2) return false;
	if (t.startsWith("/")) return false;
	const lower = t.toLowerCase();
	if (lower.startsWith(INTERNAL_MESSAGE_PREFIX.toLowerCase())) return false;
	// Explicit STARTS_WITH list — precise directives only
	for (const p of SYNTHETIC_PROMPT_PREFIXES) {
		if (lower.startsWith(p.toLowerCase())) return false;
	}
	// Explicit CONTAINS_PHRASES — exact multi-word synthetic directives (not generic discussion)
	if (lower.includes("post-compaction autonomous resumption directive")) return false;
	if (lower.includes("pre-compaction exhaustive context preservation protocol")) return false;
	if (lower.includes("context is approaching the configured compaction threshold")) return false;
	if (lower.includes("context compaction is now being requested")) return false;
	if (lower.includes("context compaction is imminent")) return false;
	if (lower.includes("critical quest journal compaction safety state")) return false;
	return true;
}
