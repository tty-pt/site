import { CUSTOM_TYPE, INTERNAL_MESSAGE_PREFIX, QuestErrorCode, SYNTHETIC_PROMPT_PREFIXES } from "./constants.ts";
import { logAgentMessageTransition, logEvent } from "./logging.ts";
import { questPath } from "./paths.ts";
import { getActiveContext, getSessionId, getState, sessionStates, setSessionState, snapshotState, state } from "./state.ts";
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

		if (!Array.isArray(targetState.pendingNotifications)) {
			targetState.pendingNotifications = [];
		}

		const existing = targetState.pendingNotifications.find(
			(n) => n.code === options.code && n.message === message,
		);
		if (existing) {
			existing.attempts = (existing.attempts || 0) + 1;
			existing.lastAttemptAt = Date.now();
			if (options.correlationId) {
				existing.correlationId = options.correlationId;
			}
		} else {
			const pendingNotif: PendingAgentNotification = {
				id: `notif_${Date.now()}_${Math.random().toString(36).slice(2, 8)}`,
				code: typeof options.code === "string" ? options.code : String(options.code),
				correlationId: options.correlationId,
				message,
				deliverAs,
				requiredNextAction: options.requiredNextAction,
				details: options.details,
				attempts: 1,
				createdAt: Date.now(),
				lastAttemptAt: Date.now(),
			};
			targetState.pendingNotifications.push(pendingNotif);
			if (targetState.pendingNotifications.length > 20) {
				targetState.pendingNotifications = targetState.pendingNotifications.slice(-20);
			}
			logAgentMessageTransition("AGENT_MESSAGE_QUEUED", `agent notification queued: [${options.code}]`, {
				code: typeof options.code === "string" ? options.code : String(options.code),
				correlationId: options.correlationId,
				deliverAs,
			});
		}

		if (typeof pi?.appendEntry === "function") {
			pi.appendEntry(CUSTOM_TYPE, snapshotState(c));
		}
		setSessionState(c, targetState);
	} catch (err: any) {
		logError("Failed to persist pending agent notification", err, ctx, QuestErrorCode.PERSISTENCE_FAILURE, options.correlationId);
	}

	return false;
}

export function drainPendingAgentNotifications(
	pi: ExtensionAPI,
	ctx?: ExtensionContext,
): boolean {
	try {
		const c = getActiveContext(ctx);
		const targetSessionId = getSessionId(c);
		const targetState = sessionStates.get(targetSessionId) ?? getState(c);

		if (!Array.isArray(targetState.pendingNotifications) || targetState.pendingNotifications.length === 0) {
			return true;
		}

		const remaining: PendingAgentNotification[] = [];
		let anyDelivered = false;

		for (const notif of targetState.pendingNotifications) {
			const messageType = typeof notif.code === "string" ? notif.code.toLowerCase() : "notification";
			logAgentMessageTransition("AGENT_MESSAGE_RETRIED", `retrying pending notification: [${notif.code}]`, {
				code: notif.code,
				correlationId: notif.correlationId,
				type: messageType,
				attempt: (notif.attempts || 0) + 1,
				deliverAs: notif.deliverAs || "followUp",
			});
			const text = formatAgentErrorMessage(notif.code, notif.message, notif.requiredNextAction, notif.details);
			const delivered = sendInternalAgentMessage(pi, text, notif.deliverAs || "followUp", messageType, notif.correlationId);
			if (delivered) {
				anyDelivered = true;
			} else {
				notif.attempts = (notif.attempts || 0) + 1;
				notif.lastAttemptAt = Date.now();
				remaining.push(notif);
			}
		}

		targetState.pendingNotifications = remaining;
		if (anyDelivered || remaining.length > 0) {
			if (typeof pi?.appendEntry === "function") {
				pi.appendEntry(CUSTOM_TYPE, snapshotState(c));
			}
			setSessionState(c, targetState);
		}
		return remaining.length === 0;
	} catch (err: any) {
		logError("Failed while draining pending agent notifications", err);
		return false;
	}
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
	for (const p of SYNTHETIC_PROMPT_PREFIXES) {
		if (lower.startsWith(p)) return false;
	}
	if (lower.includes("post-compaction autonomous resumption directive")) return false;
	if (lower.includes("pre-compaction exhaustive context preservation protocol")) return false;
	if (lower.includes("context is approaching the configured compaction threshold")) return false;
	if (lower.includes("context compaction is now being requested")) return false;
	if (lower.includes("context compaction is imminent")) return false;
	if (lower.includes("final exhaustive durable state save")) return false;
	if (lower.includes("critical quest journal compaction safety state")) return false;
	if (lower.includes("context compaction warning")) return false;
	if (lower.includes("context usage has reached or exceeded")) return false;
	if (lower.includes("compaction safety state")) return false;
	return true;
}
