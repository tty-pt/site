import { acceptRootConfirmation, classifyUserMessage, handleAskQuestionsResult } from "../classification.ts";
import {
	advanceSteerTurnCounter,
	checkAndTriggerDeferredCompaction,
	compactionReady,
	createOrGetCompactionTransaction,
	dispatchCompactionResume,
	drainPendingResumesAndNotifications,
	handleCompactionCompleted,
	retryPendingResume,
} from "../compaction.ts";
import { checkAndTriggerDirectionReview } from "../critical_agent.ts";
import { FUTURE_DIR, PROMPT_MAX_CHARS, PROMPT_MAX_COUNT, QUEST_CURRENT_DIR } from "../constants.ts";
import { withContext } from "../context.ts";
import { ensureRootQuestForPrompt } from "../lifecycle.ts";
import { createHash } from "node:crypto";
import { logCompactionTransition, logCriticalReviewTransition, logEvent, logUserInteraction } from "../logging.ts";
import { readFileSync } from "node:fs";
import { appendFile, mkdir, readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { getGuidelinesFingerprint } from "../context.ts";
import { validatePhasedPlan } from "../compaction/checkpoint.ts";
import { COMPACT_WORKFLOW_RULES } from "../markdown/template/rules.ts";
import { getCompactWorkflowInstructions, getFullWorkflowInstructions, getWorkflowInstructions } from "../markdown.ts";
import { getCachedWorkflow, setCachedWorkflow } from "../utils/cache.ts";
import { logDebug, logError, sendInternalAgentMessage, sendSaveRequest, shouldCapturePrompt } from "../messaging.ts";
import { appendToFutureDraft, createFutureDraftFromPrompt, futureDraftPath, generateSlugFromPrompt, questDirPath, questPath, shouldStartPersistentQuest } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadActiveQuestResumeContext } from "../reconstruction.ts";
import { triggerReassessment } from "../research.ts";
import { getActiveContext, getSessionId, getState, sessionStates, state } from "../state.ts";
import { ExtensionAPI, ExtensionContext, UserMessageClassification } from "../types.ts";
import { buildSessionAwarenessBlock, updateUIStatus } from "../ui.ts";
import { normalizePath } from "../utils.ts";
import { handleCompactionFailure, handleToolResult, handleTurnEnd, handleTurnStart } from "./handlers.ts";
import { analyzeTurnToolResults, applyTurnEndStateTransitions, classifyActivityPhase, detectBashToolFailure, isPathToActiveQuest, isQuestUpdateTool, isSubstantiveToolName, isToolExecutionError } from "./turn_analysis.ts";

export * from "./turn_analysis.ts";
export * from "./handlers.ts";

export function installTurnStart(pi: ExtensionAPI) {
	pi.on(
		"turn_start",
		withContext(async (event: any, ctx: ExtensionContext) => {
			await handleTurnStart(event, ctx);
		}),
	);
}

export function installTurnEnd(pi: ExtensionAPI) {
	pi.on(
		"turn_end",
		withContext(async (event: any, ctx: ExtensionContext) => {
			await handleTurnEnd(pi, ctx, event);
		}),
	);
}

export function installToolResultListener(pi: ExtensionAPI) {
	pi.on(
		"tool_result",
		withContext(async (event: any, ctx: ExtensionContext) => {
			await handleToolResult(event, ctx);
		}),
	);
}

export function installBeforeCompact(pi: ExtensionAPI) {
	pi.on(
		"session_before_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;

			// If a pending resume obligation exists, attempt delivery before starting a new transaction
			if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
				retryPendingResume(pi, ctx);
				if (state.pendingResume || state.activeTransaction?.phase === "resume-pending") {
					logDebug("Quest Journal: cancelling session_before_compact because previous resume obligation is still pending delivery.");
					return { cancel: true };
				}
			}

			if (!compactionReady()) {
				state.compactionPending = false;
				state.preCompactionCheckpointPending = true;
				state.preCompactionSaveRequestPending = true;
				persist(pi, ctx);

				const activeFile = questPath(state.active);
				const msg = `⚡ **Compaction Blocked (Unsaved Working Memory)**:
Compaction is blocked because the active quest file \`${activeFile}\` contains unsaved changes or unverified state.

To allow auto-compaction and preserve continuity across the boundary:
1. Update \`${activeFile}\` with your current understanding, decisions, plan confidence, remaining work, and exact next step.
2. Call \`quest_mark_saved\` to persist the state.
Once saved, auto-compaction will safely proceed.`;

				logCompactionTransition("COMPACTION_BLOCKED", "compaction blocked: unsaved working memory", {
					quest: state.active || "",
					reason: "unsaved working memory",
				});

				sendInternalAgentMessage(pi, msg, "steer");

				if (ctx?.hasUI) {
					ctx.ui.notify(`Quest-journal: blocking compaction until '${activeFile}' is saved.`, "warning");
				}
				return { cancel: true };
			}

			try {
				if ((state.researchComplete || (state.planVersion && state.planVersion > 1)) && state.questId) {
					const p = questPath(state.questId);
					const content = readFileSync(p, "utf8");
					if (!validatePhasedPlan(content)) {
						sendInternalAgentMessage(pi, `⚠️ **Phased plan advisory before compaction**: \`${p}#Plan\` does not yet contain numbered stages with verification or \`[[subquest]]\` links and a concrete \`## Exact Next Action\`. Update the quest file before next compaction.`, "steer");
					}
				}
			} catch {}

			// Orphan detection: scoped per-session, do not leak across asyncContext default
			try {
				const targetSessionId = (ctx as any)?.sessionId || (ctx as any)?.session?.id || getSessionId(getActiveContext(ctx) as any) || "default";
				const targetState = sessionStates.get(targetSessionId) ?? getState(ctx as any) ?? state;
				const { getActiveReviews } = await import("../critical_agent/tracker.ts");
				const active = [...getActiveReviews().values()].filter((r) => r.status === "starting" || r.status === "running");
				if (active.length > 0) {
					logCriticalReviewTransition("CRITICAL_REVIEW_ERROR" as any, `critical review orphaned at compaction boundary (active=${active.length})`, {
						quest: targetState.active || state.active || "",
						sessionId: targetSessionId,
						reason: "compaction_orphan",
					} as any);
				}
				if (active.length === 0 && targetState.inCriticalReview) {
					try { logCriticalReviewTransition("CRITICAL_REVIEW_ORPHAN_CLEARED" as any, `orphan flag no awaiting`, { quest: targetState.active || state.active || "", reason: "orphan_flag_no_awaiting" } as any); } catch {}
					targetState.inCriticalReview = false;
					try { persist(pi, ctx); } catch {}
				}
			} catch {}

			// Establish transaction in-flight
			const tx = createOrGetCompactionTransaction(state, "normal-compaction");
			tx.phase = "in-flight";
			persist(pi, ctx);
			logCompactionTransition("COMPACTION_STARTED", "compaction started", {
				quest: state.active || "",
				compactionId: tx.id,
			});
		}),
	);
}

export function installAfterCompact(pi: ExtensionAPI) {
	pi.on(
		"session_compact",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			await handleCompactionCompleted(pi, ctx);
		}),
	);
	pi.on(
		"session_compact_failed",
		withContext(async (event: any, ctx: ExtensionContext) => {
			await handleCompactionFailure(pi, ctx, event);
		}),
	);
}

export function installContextListener(_pi: ExtensionAPI) {
	// Periodic checkpoint is turn-count driven; no context-percentage listener.
	return;
}

export function installBeforeSwitch(pi: ExtensionAPI) {
	pi.on(
		"session_before_switch",
		withContext(async (_event: any, ctx: ExtensionContext) => {
			if (!state.active) return;
			if (ctx.hasUI && !compactionReady()) {
				ctx.ui.notify(`Quest-journal: active quest '${state.active}' has unsaved changes before session switch.`, "warning");
			}
		}),
	);
}

export function installShutdownSave(pi: ExtensionAPI) {
	pi.on("session_shutdown", async (event: any, ctx: any) => {
		if (event.reason !== "quit") return;
		if (!state.active) return;
		if (ctx?.hasUI && !compactionReady()) {
			ctx.ui.notify(`Quest-journal: quest '${state.active}' has unsaved changes.`, "warning");
		}
	});
}

export function installFileWatch(pi: ExtensionAPI) {
	pi.on("tool_result", async (event: any, ctx: any) => {
		if (event.isError || event.error || (event.details && (event.details.error || event.details.success === false))) {
			return;
		}

		if (state.activeTransaction && state.activeTransaction.phase === "resume-delivered") {
			state.activeTransaction = null;
		}

		if (event.toolName === "ask_questions" || (typeof event.toolName === "string" && event.toolName.toLowerCase().includes("ask_question"))) {
			handleAskQuestionsResult(pi, event, ctx);
			return;
		}

		if (event.toolName !== "write" && event.toolName !== "edit") {
			if (
				event.toolName === "bash" ||
				event.toolName === "user_bash" ||
				event.toolName === "subagent" ||
				(typeof event.toolName === "string" && (event.toolName.startsWith("bg_run") || event.toolName.startsWith("fusion_") || event.toolName === "doc_to_md"))
			) {
				state.dirty = true;
			}
			return;
		}

		const p = event.input?.path as string | undefined;
		if (typeof p !== "string") return;
		const norm = normalizePath(p);

		if (state.active && norm === questPath(state.questId)) {
			await verifyAndMarkSaved(pi, ctx, state.active);
		} else if (!state.active && norm.startsWith(`${QUEST_CURRENT_DIR}/`) && norm.endsWith("quest.md")) {
			const parts = norm.split("/");
			const qid = parts[parts.length - 2];
			state.questId = qid;
			state.active = qid;
			if (!Array.isArray(state.stack)) state.stack = [qid];
			else if (!state.stack.includes(qid)) state.stack.push(qid);
			await verifyAndMarkSaved(pi, ctx, qid);
		} else {
			state.dirty = true;
			if (!Array.isArray(state.sessionModifiedFiles)) {
				state.sessionModifiedFiles = [];
			}
			if (!state.sessionModifiedFiles.includes(norm)) {
				state.sessionModifiedFiles.push(norm);
			}
		}
	});
}

export function installWorkflowSystemPrompt(pi: ExtensionAPI) {
	pi.on(
		"before_agent_start",
		withContext(async (event: any, ctx: ExtensionContext) => {
			try {
				const raw = (event as { prompt?: unknown })?.prompt;
				if (typeof raw === "string" && shouldCapturePrompt(raw)) {
					const trimmed = raw.trim().slice(0, PROMPT_MAX_CHARS);

					if (state.activeDraft) {
						const classification = classifyUserMessage(trimmed);
						try { logEvent("CLASSIFICATION_RESULT", `classification ${classification}`, { classification, quest: state.activeDraft || "" } as any); } catch {}
						try { if (classification !== UserMessageClassification.CONVERSATIONAL_ACK) { const slice = trimmed.slice(0, 120); const hash = createHash("sha256").update(trimmed).digest("hex").slice(0, 12); logEvent("USER_PROMPT", `user prompt`, { classification, quest: state.activeDraft || "", slice, hash, intentHash: hash } as any); } } catch {}
						if (classification === UserMessageClassification.CONFIRMATION) {
							const { isDraftReviewValid } = await import("../critical_agent/policy.ts");
							const { getCustomSubagentRunner, isSubagentToolRegistered } = await import("../critical_agent/index.ts");
							const hasReviewer = Boolean(getCustomSubagentRunner()) || isSubagentToolRegistered(pi as any, ctx as any);
							const valid = hasReviewer ? isDraftReviewValid(state) : true;
							if (valid) {
								const { promoteDraft } = await import("../commands/promote.ts");
								const res = await promoteDraft(state.activeDraft, ctx, pi);
								if (res.success) {
									logUserInteraction("QUEST_CREATED", `draft '${state.active}' promoted after user go`, { quest: state.active || "" });
								} else {
									logUserInteraction("GATE_BLOCKED", res.message || "draft promotion blocked", { quest: state.activeDraft || "" });
									sendInternalAgentMessage(pi, res.message || "Draft promotion blocked: reviewer approval required before presenting plan.", "steer");
								}
							} else {
								logUserInteraction("CONFIRMATION_REJECTED", "user confirmed before draft reviewer approval", { quest: state.activeDraft || "" });
								sendInternalAgentMessage(pi, `⚠️ Draft '${state.activeDraft}' not yet reviewer-approved. Accumulate requirements first, then submit draft for review; promotion requires APPROVE before "go". Trigger review via plan_review or wait for auto-review.`, "steer");
								// Trigger draft review now if not yet run
								try {
									const { checkAndTriggerPlanReview } = await import("../critical_agent/policy.ts");
									await checkAndTriggerPlanReview(pi, ctx);
								} catch {}
							}
							persist(pi, ctx);
							updateUIStatus(ctx);
						} else if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT || classification === UserMessageClassification.QUESTION_OR_DISCUSSION) {
							if (!Array.isArray(state.draftPrompts)) state.draftPrompts = [];
							if (!state.draftPrompts.includes(trimmed)) {
								state.draftPrompts.push(trimmed);
								if (state.draftPrompts.length > PROMPT_MAX_COUNT) {
									state.draftPrompts = [state.draftPrompts[0], ...state.draftPrompts.slice(-(PROMPT_MAX_COUNT - 1))];
								}
								try {
									const qid = (state as any).questId || state.questId;
									if (qid) {
										const jPath = join(questDirPath(qid), "draft-prompts.jsonl");
										await mkdir(dirname(jPath), { recursive: true });
										const rec = JSON.stringify({ ts: Date.now(), hash: createHash("sha256").update(trimmed).digest("hex").slice(0, 12), slice: trimmed.slice(0, 200), len: trimmed.length }) + "\n";
										await appendFile(jPath, rec, "utf8");
									}
								} catch {}
							}
							const appended = await appendToFutureDraft(state.activeDraft, trimmed);
							try {
								const full = await readFile(futureDraftPath(state.activeDraft), "utf8");
								state.draftLastSavedHash = createHash("sha256").update(full).digest("hex").slice(0, 12);
								persist(pi, ctx);
							} catch {
								try { state.draftLastSavedHash = createHash("sha256").update(trimmed).digest("hex").slice(0, 12); persist(pi, ctx); } catch {}
							}
							try {
								const hash = state.draftLastSavedHash || createHash("sha256").update(trimmed).digest("hex").slice(0, 12);
								if (appended) {
									logEvent("DRAFT_APPENDED" as any, `draft appended`, { quest: state.activeDraft || "", slug: state.activeDraft, hash, draftPromptsCount: state.draftPrompts.length } as any);
								} else {
									logEvent("DRAFT_APPEND_DEDUPED" as any, `draft append deduped`, { quest: state.activeDraft || "", slug: state.activeDraft, hash, draftPromptsCount: state.draftPrompts.length } as any);
								}
							} catch {}
							logUserInteraction("USER_REFINEMENT_RECEIVED", `draft requirement accumulated for '${state.activeDraft}'`, { quest: state.activeDraft || "" });
							try { persist(pi, ctx); } catch {}
							updateUIStatus(ctx);
							// 38: if a plan_review draft review is already awaiting/running, supersede-candidate + eager coalesce new boundary
							try {
								const newHash = state.draftLastSavedHash || createHash("sha256").update(trimmed).digest("hex").slice(0, 12);
								const slug = state.activeDraft;
								if (slug) {
									const newBoundary = `draft:${slug}:${newHash}`;
									let hasActive = !!(state as any).awaitingReview && (state as any).awaitingReview.kind === "plan_review";
									try {
										const { findActiveReviewForQuest } = await import("../critical_agent/tracker.ts");
										if (findActiveReviewForQuest(slug)?.kind === "plan_review") hasActive = true;
									} catch {}
									if (hasActive) {
										try {
											const { setPendingReview } = await import("../critical_agent/tracker.ts");
											setPendingReview(slug, {
												questSlug: slug,
												kind: "plan_review" as any,
												triggerReason: "draft_followup",
												planVersion: state.planVersion || 1,
												stateHash: state.lastSavedHash || (state.saveGeneration ? state.saveGeneration.hash : null),
												boundaryKey: newBoundary,
												saveCount: state.saveCount || 0,
												requestedAt: Date.now(),
											} as any);
											logEvent("PENDING_COALESCED_RESOLVED" as any, `pending coalesced resolved (draft followup hash=${newHash})`, { quest: slug, chosenKind: "plan_review", boundaryKey: newBoundary, hash: newHash } as any);
										} catch {}
									}
								}
							} catch {}
							// Efficient: auto-trigger draft compliance review once we have at least 2 requirements and not already approved
							if ((state.draftPrompts?.length || 0) >= 2) {
								try {
									const { isDraftReviewValid } = await import("../critical_agent/policy.ts");
									if (!isDraftReviewValid(state)) {
										const { checkAndTriggerPlanReview } = await import("../critical_agent/policy.ts");
										// fire-and-forget, deduped via __lastDraftReviewKey
										checkAndTriggerPlanReview(pi, ctx).catch(() => {});
									}
								} catch {}
							}
						}
						try {
								const hash = createHash("sha256").update(trimmed).digest("hex").slice(0, 12);
								logEvent("DRAFT_CONVERSATIONAL_IGNORED" as any, `draft conversational ignored`, { quest: state.activeDraft || "", slug: state.activeDraft, hash, draftPromptsCount: (state.draftPrompts?.length || 0) } as any);
							} catch {}
						// CONVERSATIONAL_ACK ignored while drafting
					} else if (state.active) {
						if (!Array.isArray(state.refinements)) state.refinements = [];
						if (!Array.isArray(state.prompts)) state.prompts = [];

						const isOriginal = state.prompts.length > 0 && state.prompts[0] === trimmed;
						const isLatestRefinement = state.refinements.length > 0 && state.refinements[state.refinements.length - 1] === trimmed;

						if (!isOriginal && !isLatestRefinement) {
							const classification = classifyUserMessage(trimmed);
							try { logEvent("CLASSIFICATION_RESULT", `classification ${classification}`, { classification, quest: state.active || "" } as any); } catch {}
							try { if (classification !== UserMessageClassification.CONVERSATIONAL_ACK) { const slice = trimmed.slice(0, 120); const hash = createHash("sha256").update(trimmed).digest("hex").slice(0, 12); logEvent("USER_PROMPT", `user prompt`, { classification, quest: state.active || "", slice, hash, intentHash: hash } as any); } } catch {}

							if (classification === UserMessageClassification.CONFIRMATION) {
								logUserInteraction("CONFIRMATION_RECEIVED", "user confirmation received", { quest: state.active || "" });
								acceptRootConfirmation(pi, ctx);
							} else if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT) {
								logUserInteraction("USER_REFINEMENT_RECEIVED", "user refinement received", { quest: state.active || "" });
								state.refinements.push(trimmed);
								state.prompts.push(trimmed);
								if (state.prompts.length > PROMPT_MAX_COUNT) {
									state.prompts = [state.prompts[0], ...state.prompts.slice(-(PROMPT_MAX_COUNT - 1))];
								}
								if (state.refinements.length > PROMPT_MAX_COUNT) {
									state.refinements = state.refinements.slice(-PROMPT_MAX_COUNT);
								}
								triggerReassessment(state, `User refinement received: "${trimmed.slice(0, 100)}..."`, trimmed);
								persist(pi, ctx);
								updateUIStatus(ctx);
							}
						}
					} else if (state.pendingRootQuest) {
						const classification = classifyUserMessage(trimmed);
						try { logEvent("CLASSIFICATION_RESULT", `classification ${classification}`, { classification, quest: state.active || "" } as any); } catch {}
						try { if (classification !== UserMessageClassification.CONVERSATIONAL_ACK) { const slice = trimmed.slice(0, 120); const hash = createHash("sha256").update(trimmed).digest("hex").slice(0, 12); logEvent("USER_PROMPT", `user prompt`, { classification, quest: state.active || "", slice, hash, intentHash: hash } as any); } } catch {}
						if (classification === UserMessageClassification.REFINEMENT_OR_REQUIREMENT) {
							if (!Array.isArray(state.refinements)) state.refinements = [];
							state.refinements.push(trimmed);
							if (!state.prompts.includes(trimmed)) {
								state.prompts.push(trimmed);
							}
							persist(pi, ctx);
							updateUIStatus(ctx);
						}
					} else if (shouldStartPersistentQuest(trimmed)) {
						// First try to resume existing quest (substring match) like ensureRootQuestForPrompt
						const { listActiveQuestRecords, listQuestFiles } = await import("../paths.ts");
						const { FUTURE_DIR } = await import("../constants.ts");
						let activated = false;
						try {
							const activeRecords = await listActiveQuestRecords();
							for (const r of activeRecords) {
								if ((r.name.length >= 3 && trimmed.toLowerCase().includes(r.name.toLowerCase())) || trimmed.toLowerCase().includes(r.qid.toLowerCase())) {
									activated = await ensureRootQuestForPrompt(pi, ctx, trimmed);
									break;
								}
							}
							if (!activated) {
								const futureFiles = await listQuestFiles(FUTURE_DIR);
								for (const f of futureFiles) {
									const s = f.replace(/\.md$/, "");
									if (s.length >= 3 && trimmed.toLowerCase().includes(s.toLowerCase())) {
										activated = await ensureRootQuestForPrompt(pi, ctx, trimmed);
										break;
									}
								}
							}
						} catch {}
						if (activated) {
							// existing quest resumed, no draft
						} else {
							const slug = generateSlugFromPrompt(trimmed, 45);
							await createFutureDraftFromPrompt(slug, trimmed);
							try { const c = await readFile(join(FUTURE_DIR, `${slug}.md`), "utf8"); state.draftLastSavedHash = createHash("sha256").update(c).digest("hex").slice(0, 12); } catch { try { state.draftLastSavedHash = createHash("sha256").update(trimmed).digest("hex").slice(0, 12); } catch {} }
							state.activeDraft = slug;
							state.draftPrompts = [trimmed];
							state.draftCreatedAt = Date.now();
							state.pendingRootQuest = true;
							state.pendingRootRequest = trimmed;
							if (!state.questId) {
								const { ensureQuestId } = await import("../state.ts");
								ensureQuestId(ctx as any);
							}
							if (!Array.isArray(state.prompts)) state.prompts = [];
							if (!state.prompts.includes(trimmed)) state.prompts.push(trimmed);
							const { logQuestTransition } = await import("../logging.ts");
							logQuestTransition("QUEST_DETECTED", `draft auto-detected for '${slug}'`, { quest: slug });
							logQuestTransition("QUEST_CREATED", `draft auto-created '${slug}'`, { quest: slug });
							logUserInteraction("QUEST_CREATED", `auto-drafted '${slug}' from prompt`, { quest: slug });
							persist(pi, ctx);
							updateUIStatus(ctx);
							sendInternalAgentMessage(pi, `📝 **Draft auto-created**: \`.pi/quest/future/${slug}.md\` — accumulating requirements while you talk. Requirements stay in draft (not yet part of active quest). When ready, the reviewer will validate compliance before the plan is presented; then say "go" to promote.`, "steer");
						}
					}
				}

				drainPendingResumesAndNotifications(pi, ctx);
				// Phase 22: orphan awaitingReview re-queue + turn-stop steer (A: plan_review/final_acceptance only) — 3-case CRITICAL_REVIEW_ORPHAN_CLEARED
				try {
					const c = getActiveContext(ctx);
					const targetSessionId = getSessionId(c);
					const targetState = sessionStates.get(targetSessionId) ?? getState(c);
					const aw = (targetState as any).awaitingReview as { kind: string; reviewId: string; triggerReason?: string } | null | undefined;
					// case A: reviewer disabled
					let reviewerDisabled = false;
					try { const { getCustomSubagentRunner, isSubagentToolRegistered } = await import("../critical_agent/index.ts"); reviewerDisabled = !Boolean(getCustomSubagentRunner()) && !isSubagentToolRegistered(pi as any, ctx as any); } catch {}
					if (aw && reviewerDisabled) {
						try { logCriticalReviewTransition("CRITICAL_REVIEW_ORPHAN_CLEARED" as any, `orphan reviewer disabled`, { quest: targetState.active || state.active || "", reason: "reviewer_disabled", reviewId: aw.reviewId, triggerReason: aw.triggerReason } as any); } catch {}
						(targetState as any).awaitingReview = null;
						targetState.inCriticalReview = false;
						try { persist(pi, ctx); } catch {}
					} else if (aw && (aw.kind === "plan_review" || aw.kind === "final_acceptance")) {
						const { getActiveReviews } = await import("../critical_agent/tracker.ts");
						const hasActive = getActiveReviews().has(aw.reviewId);
						if (!hasActive) {
							try { logCriticalReviewTransition("CRITICAL_REVIEW_ORPHAN_CLEARED" as any, `orphan awaiting pending requeue`, { quest: targetState.active || state.active || "", reason: "orphan_awaiting_pending_requeue", reviewId: aw.reviewId, triggerReason: aw.triggerReason } as any); } catch {}
							// Orphan: re-queue as pending coalesced if not already pending
							try {
								const { getPendingReview, setPendingReview } = await import("../critical_agent/tracker.ts");
								if (!getPendingReview(targetState.active || "", aw.kind as any)) {
									setPendingReview(targetState.active || targetState.questId || "quest", {
										questSlug: targetState.active || targetState.questId || "quest",
										kind: aw.kind as any,
										triggerReason: aw.triggerReason,
										planVersion: targetState.planVersion || 1,
										stateHash: targetState.lastSavedHash || (targetState.saveGeneration ? targetState.saveGeneration.hash : null),
										boundaryKey: targetState.lastPlanReviewBoundaryKey || null,
										saveCount: targetState.saveCount || 0,
										requestedAt: Date.now(),
									} as any);
								}
							} catch {}
							// Re-assert steer so model stops turn
							sendInternalAgentMessage(pi, `⏸ Awaiting ${aw.kind}/${aw.triggerReason || aw.kind} ${aw.reviewId} — verdict pending. No writes until verdict; reads and quest_mark_saved allowed.`, "steer");
						} else {
							// Active exists, still assert turn-stop
							sendInternalAgentMessage(pi, `⏸ Awaiting ${aw.kind}/${aw.triggerReason || aw.kind} ${aw.reviewId} — verdict pending. No writes until verdict; reads and quest_mark_saved allowed.`, "steer");
						}
					}
				} catch {}

				const guidelineFp = getGuidelinesFingerprint();
				const awarenessBlock = buildSessionAwarenessBlock(ctx);
				const resumeContext = await loadActiveQuestResumeContext();
				const pressureKey = `${state.saveGeneration?.hash || ""}:${state.researchRound || 1}:${state.pendingRootQuest ? "pending" : "active"}:${guidelineFp}`;
				const cached = getCachedWorkflow(state.saveGeneration?.hash || "", pressureKey);
				if (cached) {
					if (event && typeof event.systemPrompt === "string") {
						return { systemPrompt: `${event.systemPrompt}\n\n${awarenessBlock}${cached}` };
					}
				}
				const isSteadyState = !state.pendingRootQuest && !state.researchRequired && !state.reassessmentRequired && (() => { try { return compactionReady(); } catch { return false; } })();
				const workflowInstructions = isSteadyState && (state.researchRound || 1) > 1
					? getCompactWorkflowInstructions(resumeContext)
					: getFullWorkflowInstructions(resumeContext);
				setCachedWorkflow(state.saveGeneration?.hash || "", pressureKey, workflowInstructions);

				if (event && typeof event.systemPrompt === "string") {
					return { systemPrompt: `${event.systemPrompt}\n\n${awarenessBlock}${workflowInstructions}` };
				}
			} catch (err: any) {
				logError("Failed in before_agent_start hook", err, ctx);
				return;
			}
		}),
	);
}

export function registerQuestJournalCRBHook() {
	if (typeof globalThis !== "undefined") {
		const g = globalThis as any;
		if (!g.__pi_crb_providers) {
			g.__pi_crb_providers = [];
		}
		g.__pi_crb_providers.push((_ctx: ExtensionContext, tools: string[]) => {
			const set = new Set(tools.map((t) => t.toLowerCase()));
			const isSteadyState = !state.pendingRootQuest && !state.researchRequired && !state.reassessmentRequired && (() => { try { return compactionReady(); } catch { return false; } })() && (state.researchRound || 1) > 1;
			if (set.has("quest_mark_saved") || set.has("quest_update_state") || state.active || state.pendingRootQuest) {
				if (isSteadyState) {
					return COMPACT_WORKFLOW_RULES.map((r) => r);
				}
				return [
					"Never propose anything without doing your homework first: thoroughly investigate codebase architecture, read files, discover build/run commands, and evaluate constraints before proposing plans or code changes.",
					"Research-Grounded Quest Formation: Investigate first to understand the actual problem, establish a short intelligible semantic quest identity, and initialize the durable quest with research findings via quest_update_state.",
					"Turn 1 Confirmation: In turn 1 of any root quest, present research findings, key assumptions tested, architectural trade-offs, and revised plan clearly to the user, and ask for confirmation BEFORE writing code.",
					"Continuous Durable Epistemic Memory: `.pi/quest/current/<qid>/quest.md` is your durable working memory and single source of truth on disk. Proactively record understanding, assumptions, plan confidence, plan revisions, and exact next action whenever discoveries occur.",
					"Dynamic Epistemic Re-Investigation: Use the quest file to recover established knowledge without repeating routine research (no unnecessary re-research). Re-investigate whenever new evidence contradicts an assumption, tests fail, or the plan fails to explain observed behavior.",
					"Autonomous Continuation: Following compaction or sub-quest return, read `.pi/quest/current/<qid>/quest.md`, validate the plan against recovered state, and proceed immediately without user interruption.",
					"Meaningful Sub-Quest Decomposition: Decompose according to the discovered structure of the problem, not arbitrary bullet counts. During research, identify genuinely separable workstreams (distinct subsystems, independent investigations, separate verification boundaries) and create sub-quests (`quest_subquest({ switchNow: false })`) linked into the parent plan (`[[subquest-name]]`). Avoid artificial fragmentation for trivial or tightly coupled steps. Sub-quests independently verify inherited context.",
					"Durable-State Reconciliation: The quest file must describe what is true NOW. After substantive changes, synchronize Completed, Files Modified, Test Status, Remaining Work, and Exact Next Action. Exact Next Action is a live pointer to the next justified action, never a repeat of completed work. Calibrate plan confidence against evidence and explain plan revisions.",
					"Full Test Suite Quality Gate: Before completing/archiving a top-level quest, restart the test server/daemon, run the fresh FULL test suite (`make test`), and verify zero errors.",
					"Top-level Quest Completion: When root quest is done, prompt user via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
				];
			}
			return [];
		});
	}
}
