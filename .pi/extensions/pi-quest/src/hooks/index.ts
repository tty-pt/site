import {
  acceptRootConfirmation,
  classifyUserMessage,
  handleAskQuestionsResult,
} from "../classification.ts";
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
import {
  FUTURE_DIR,
  PROMPT_MAX_CHARS,
  PROMPT_MAX_COUNT,
  QUEST_CURRENT_DIR,
} from "../constants.ts";
import { withContext } from "../context.ts";
import { ensureRootQuestForPrompt } from "../lifecycle.ts";
import { createHash } from "node:crypto";
import {
  logCompactionTransition,
  logCriticalReviewTransition,
  logEvent,
  logUserInteraction,
  tryLog,
} from "../logging.ts";
import { readFileSync } from "node:fs";
import { appendFile, mkdir, readFile } from "node:fs/promises";
import { dirname, join } from "node:path";
import { getGuidelinesFingerprint } from "../context.ts";
import { validatePhasedPlan } from "../compaction/checkpoint.ts";
import { COMPACT_WORKFLOW_RULES } from "../markdown/template/rules.ts";
import {
  getCompactWorkflowInstructions,
  getFullWorkflowInstructions,
  getWorkflowInstructions,
} from "../markdown.ts";
import { getCachedWorkflow, setCachedWorkflow } from "../utils/cache.ts";
import {
  logDebug,
  logError,
  sendInternalAgentMessage,
  sendSaveRequest,
  shouldCapturePrompt,
} from "../messaging.ts";
import {
  appendToFutureDraft,
  createFutureDraftFromPrompt,
  futureDraftPath,
  generateSlugFromPrompt,
  questDirPath,
  questPath,
  shouldStartPersistentQuest,
} from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadActiveQuestResumeContext } from "../reconstruction.ts";
import { triggerReassessment } from "../research.ts";
import {
  getActiveContext,
  getSessionId,
  getState,
  sessionStates,
  state,
} from "../state.ts";
import {
  ExtensionAPI,
  ExtensionContext,
  UserMessageClassification,
} from "../types.ts";
import { buildSessionAwarenessBlock, updateUIStatus } from "../ui.ts";
import { normalizePath } from "../utils.ts";
import {
  handleCompactionFailure,
  handleToolResult,
  handleTurnEnd,
  handleTurnStart,
} from "./handlers.ts";
import {
  analyzeTurnToolResults,
  applyTurnEndStateTransitions,
  classifyActivityPhase,
  detectBashToolFailure,
  isPathToActiveQuest,
  isQuestUpdateTool,
  isSubstantiveToolName,
  isToolExecutionError,
} from "./turn_analysis.ts";

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
      await handleToolResult(event, ctx, pi);
    }),
  );
}

export function installBeforeCompact(pi: ExtensionAPI) {
  pi.on(
    "session_before_compact",
    withContext(async (_event: any, ctx: ExtensionContext) => {
      if (!state.active) return;

      // If a pending resume obligation exists, attempt delivery before starting a new transaction
      if (
        state.pendingResume ||
        state.activeTransaction?.phase === "resume-pending"
      ) {
        retryPendingResume(pi, ctx);
        if (
          state.pendingResume ||
          state.activeTransaction?.phase === "resume-pending"
        ) {
          logDebug(
            "Quest Journal: cancelling session_before_compact because previous resume obligation is still pending delivery.",
          );
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

        logCompactionTransition(
          "COMPACTION_BLOCKED",
          "compaction blocked: unsaved working memory",
          {
            quest: state.active || "",
            reason: "unsaved working memory",
          },
        );

        sendInternalAgentMessage(pi, msg, "steer");

        if (ctx?.hasUI) {
          ctx.ui.notify(
            `Quest-journal: blocking compaction until '${activeFile}' is saved.`,
            "warning",
          );
        }
        return { cancel: true };
      }

      try {
        if (
          (state.researchComplete ||
            (state.planVersion && state.planVersion > 1)) &&
          state.questId
        ) {
          const p = questPath(state.questId);
          const content = readFileSync(p, "utf8");
          if (!validatePhasedPlan(content)) {
            sendInternalAgentMessage(
              pi,
              `⚠️ **Phased plan advisory before compaction**: \`${p}#Plan\` does not yet contain numbered stages with verification or \`[[subquest]]\` links and a concrete \`## Exact Next Action\`. Update the quest file before next compaction.`,
              "steer",
            );
          }
        }
      } catch {}

      // Orphan detection: scoped per-session, do not leak across asyncContext default
      try {
        const targetSessionId =
          (ctx as { session?: { id?: string }; sessionId?: string })
            ?.sessionId ||
          (ctx as { session?: { id?: string }; sessionId?: string })?.session
            ?.id ||
          getSessionId(getActiveContext(ctx)) ||
          "default";
        const targetState = sessionStates.get(targetSessionId) ??
          getState(ctx) ?? state;
        const { getActiveReviews } = await import(
          "../critical_agent/tracker.ts"
        );
        const active = [...getActiveReviews().values()].filter(
          (r) => r.status === "starting" || r.status === "running",
        );
        if (active.length > 0) {
          logCriticalReviewTransition(
            "CRITICAL_REVIEW_ERROR",
            `critical review orphaned at compaction boundary (active=${active.length})`,
            {
              quest: targetState.active || state.active || "",
              sessionId: targetSessionId,
              reason: "compaction_orphan",
            },
          );
        }
        if (active.length === 0 && targetState.inCriticalReview) {
          tryLog(
            "CRITICAL_REVIEW_ORPHAN_CLEARED",
            "orphan flag no awaiting",
            {
              quest: targetState.active || state.active || "",
              reason: "orphan_flag_no_awaiting",
            },
          );
          targetState.inCriticalReview = false;
          try {
            persist(pi, ctx);
          } catch {}
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
        ctx.ui.notify(
          `Quest-journal: active quest '${state.active}' has unsaved changes before session switch.`,
          "warning",
        );
      }
    }),
  );
}

export function installShutdownSave(pi: ExtensionAPI) {
  pi.on("session_shutdown", async (event: any, ctx: any) => {
    if (event.reason !== "quit") return;
    if (!state.active) return;
    if (ctx?.hasUI && !compactionReady()) {
      ctx.ui.notify(
        `Quest-journal: quest '${state.active}' has unsaved changes.`,
        "warning",
      );
    }
  });
}

export function installFileWatch(pi: ExtensionAPI) {
  pi.on("tool_result", async (event: any, ctx: any) => {
    if (
      event.isError ||
      event.error ||
      (event.details &&
        (event.details.error || event.details.success === false))
    ) {
      return;
    }

    if (
      state.activeTransaction &&
      state.activeTransaction.phase === "resume-delivered"
    ) {
      state.activeTransaction = null;
    }

    if (
      event.toolName === "ask_questions" ||
      (typeof event.toolName === "string" &&
        event.toolName.toLowerCase().includes("ask_question"))
    ) {
      handleAskQuestionsResult(pi, event, ctx);
      return;
    }

    if (event.toolName !== "write" && event.toolName !== "edit") {
      if (
        event.toolName === "bash" ||
        event.toolName === "user_bash" ||
        event.toolName === "subagent" ||
        (typeof event.toolName === "string" &&
          (event.toolName.startsWith("bg_run") ||
            event.toolName.startsWith("fusion_") ||
            event.toolName === "doc_to_md"))
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
    } else if (
      !state.active &&
      norm.startsWith(`${QUEST_CURRENT_DIR}/`) &&
      norm.endsWith("quest.md")
    ) {
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
        if (typeof raw === "string") {
          const sid = (() => {
            try {
              return getSessionId(getActiveContext(ctx));
            } catch {
              return "default";
            }
          })();
          const tHash = createHash("sha256")
            .update(raw)
            .digest("hex")
            .slice(0, 12);
          const tSlice = raw.trim().slice(0, 200);
          const tLen = raw.length;
          // always-on dialogue: every turn's user utterance, 200-char slice + hash, piSessionId for cross-log correlation
          tryLog("DIALOGUE", `user: ${tSlice.slice(0, 80)}`, {
            quest: state.activeDraft || state.active || "",
            dialogueRole: "user",
            dialogueSlice: tSlice,
            dialogueHash: tHash,
            dialogueLen: tLen,
            piSessionId: sid,
            opencodeSessionId: sid,
            hash: tHash,
          });
          if (!shouldCapturePrompt(raw)) {
            // synthetic already logged above, still count as dialogue, skip draft/active handling
            const hash = tHash;
            tryLog(
              "DRAFT_CONVERSATIONAL_IGNORED",
              `draft conversational ignored`,
              {
                quest: state.activeDraft || state.active || "",
                slug: state.activeDraft || state.active || "",
                hash,
                draftPromptsCount: state.draftPrompts?.length || 0,
                dialogueHash: hash,
              },
            );
            // still continue? synthetic should not enter draft/active branches
          } else {
            const trimmed = raw.trim().slice(0, PROMPT_MAX_CHARS);

            if (state.activeDraft) {
              const classification = classifyUserMessage(trimmed);
              tryLog(
                "CLASSIFICATION_RESULT",
                `classification ${classification}`,
                {
                  classification,
                  quest: state.activeDraft || "",
                },
              );
              if (
                classification !== UserMessageClassification.CONVERSATIONAL_ACK
              ) {
                const slice = trimmed.slice(0, 120);
                const hash = createHash("sha256")
                  .update(trimmed)
                  .digest("hex")
                  .slice(0, 12);
                tryLog("USER_PROMPT", `user prompt`, {
                  classification,
                  quest: state.activeDraft || "",
                  slice,
                  hash,
                  intentHash: hash,
                });
              }
              if (classification === UserMessageClassification.CONFIRMATION) {
                const { isDraftReviewValid } = await import(
                  "../critical_agent/policy.ts"
                );
                const { getCustomSubagentRunner, isSubagentToolRegistered } =
                  await import("../critical_agent/index.ts");
                const hasReviewer = Boolean(getCustomSubagentRunner()) ||
                  isSubagentToolRegistered(pi, ctx);
                const valid = hasReviewer ? isDraftReviewValid(state) : true;
                if (valid) {
                  const { promoteDraft } = await import(
                    "../commands/promote.ts"
                  );
                  const res = await promoteDraft(state.activeDraft, ctx, pi);
                  if (res.success) {
                    logUserInteraction(
                      "QUEST_CREATED",
                      `draft '${state.active}' promoted after user go`,
                      { quest: state.active || "" },
                    );
                  } else {
                    logUserInteraction(
                      "GATE_BLOCKED",
                      res.message || "draft promotion blocked",
                      { quest: state.activeDraft || "" },
                    );
                    sendInternalAgentMessage(
                      pi,
                      res.message ||
                        "Draft promotion blocked: reviewer approval required before presenting plan.",
                      "steer",
                    );
                  }
                } else {
                  // Change D: user "go" overrides missing reviewer approval
                  logUserInteraction(
                    "CONFIRMATION_REJECTED",
                    "user confirmed before draft reviewer approval",
                    { quest: state.activeDraft || "" },
                  );
                  // Force-promote: user's explicit "go" is an override
                  const { promoteDraft } = await import(
                    "../commands/promote.ts"
                  );
                  const res = await promoteDraft(state.activeDraft, ctx, pi, { force: true });
                  if (res.success) {
                    logUserInteraction(
                      "QUEST_CREATED",
                      `draft '${state.active}' force-promoted after user go`,
                      { quest: state.active || "" },
                    );
                    sendInternalAgentMessage(
                      pi,
                      `✅ Draft '${state.activeDraft}' promoted to current quest (${res.qid || "new"}) despite pending/absent reviewer approval (user override).`,
                      "steer",
                    );
                  } else {
                    logUserInteraction(
                      "GATE_BLOCKED",
                      res.message || "draft force-promotion blocked",
                      { quest: state.activeDraft || "" },
                    );
                    sendInternalAgentMessage(
                      pi,
                      res.message || "Draft promotion failed.",
                      "steer",
                    );
                  }
                }
                persist(pi, ctx);
                updateUIStatus(ctx);
              } else if (
                classification ===
                  UserMessageClassification.REFINEMENT_OR_REQUIREMENT ||
                classification ===
                  UserMessageClassification.QUESTION_OR_DISCUSSION
              ) {
                if (!Array.isArray(state.draftPrompts)) state.draftPrompts = [];
                if (!state.draftPrompts.includes(trimmed)) {
                  state.draftPrompts.push(trimmed);
                  if (state.draftPrompts.length > PROMPT_MAX_COUNT) {
                    state.draftPrompts = [
                      state.draftPrompts[0],
                      ...state.draftPrompts.slice(-(PROMPT_MAX_COUNT - 1)),
                    ];
                  }
                  try {
                    const qid = state.questId;
                    if (qid) {
                      const jPath = join(
                        questDirPath(qid),
                        "draft-prompts.jsonl",
                      );
                      await mkdir(dirname(jPath), { recursive: true });
                      const rec = JSON.stringify({
                        ts: Date.now(),
                        hash: createHash("sha256")
                          .update(trimmed)
                          .digest("hex")
                          .slice(0, 12),
                        slice: trimmed.slice(0, 200),
                        len: trimmed.length,
                      }) + "\n";
                      await appendFile(jPath, rec, "utf8");
                    }
                  } catch {}
                }
                const appended = await appendToFutureDraft(
                  state.activeDraft,
                  trimmed,
                );
                try {
                  const { readFutureDraft, resolveFutureDraftPath } =
                    await import("../paths.ts");
                  const full = await readFutureDraft(state.activeDraft);
                  state.draftLastSavedHash = createHash("sha256")
                    .update(full)
                    .digest("hex")
                    .slice(0, 12);
                  // 49: correct activeDraft if disk slug differs
                  try {
                    const resolved = await resolveFutureDraftPath(
                      state.activeDraft,
                    );
                    const base = resolved
                      .split("/")
                      .pop()
                      ?.replace(/\.md$/, "");
                    if (base && base !== state.activeDraft) {
                      const old = state.activeDraft;
                      state.activeDraft = base;
                      {
                        const { tryLog } = await import("../logging.ts");
                        tryLog(
                          "DRAFT_SLUG_CORRECTED",
                          `draft slug corrected ${old} -> ${base}`,
                          { quest: base, slug: base },
                        );
                      }
                    }
                  } catch {}
                  persist(pi, ctx);
                } catch {
                  try {
                    state.draftLastSavedHash = createHash("sha256")
                      .update(trimmed)
                      .digest("hex")
                      .slice(0, 12);
                    persist(pi, ctx);
                  } catch {}
                }
                const hash = state.draftLastSavedHash ||
                  createHash("sha256")
                    .update(trimmed)
                    .digest("hex")
                    .slice(0, 12);
                if (appended) {
                  tryLog("DRAFT_APPENDED", `draft appended`, {
                    quest: state.activeDraft || "",
                    slug: state.activeDraft,
                    hash,
                    draftPromptsCount: state.draftPrompts.length,
                  });
                } else {
                  tryLog("DRAFT_APPEND_DEDUPED", `draft append deduped`, {
                    quest: state.activeDraft || "",
                    slug: state.activeDraft,
                    hash,
                    draftPromptsCount: state.draftPrompts.length,
                  });
                }
                logUserInteraction(
                  "USER_REFINEMENT_RECEIVED",
                  `draft requirement accumulated for '${state.activeDraft}'`,
                  { quest: state.activeDraft || "" },
                );
                try {
                  persist(pi, ctx);
                } catch {}
                updateUIStatus(ctx);
                // Change C: true-teardown of running reviewer on draft revision
                try {
                  const slug = state.activeDraft;
                  if (slug) {
                    const { findActiveReviewForQuest, cancelActiveReview, clearPendingReview, reviewPromiseByKey } = await import(
                      "../critical_agent/tracker.ts"
                    );
                    const active = findActiveReviewForQuest(slug);
                    if (active?.kind === "plan_review" && active.reviewId) {
                      // 1. Cancel the in-flight reviewer (emits cancel event via pi_adapter wiring)
                      cancelActiveReview(active.reviewId, "draft_revised", ctx);
                      tryLog(
                        "REVIEW_CANCELLED_DRAFT_REVISED",
                        `cancelled review ${active.reviewId} for draft revision`,
                        { quest: slug, reviewId: active.reviewId },
                      );
                      // 2. Belt-and-suspenders: clear all stacking guards
                      try {
                        const { removeReviewActiveFile } = await import(
                          "../utils/mutex.ts"
                        );
                        const { getActiveContext, getState: getStateFn } = await import(
                          "../state.ts"
                        );
                        const c2 = getActiveContext(ctx);
                        const s2 = getStateFn(c2);
                        const questId = s2.questId || "";
                        if (questId) removeReviewActiveFile(questId);
                        // Compute old singleKey: ${questId}:plan_review:draft:${slug}:${oldHash}
                        const oldBoundary = active.snapshot?.boundaryKey || "";
                        const oldHash = oldBoundary.split(":").pop() || "";
                        if (questId && oldHash) {
                          reviewPromiseByKey.delete(`${questId}:plan_review:draft:${slug}:${oldHash}`);
                        }
                        clearPendingReview(slug, "plan_review");
                      } catch {}
                    }
                    // Note: Fresh review is NOT launched on user prompt;
                    // draft reviews are only triggered by draft-file edits (handlers.ts).
                  }
                } catch {}
                // 53: auto-trigger when 7 evidences (perfection) even with 1 requirement; 54: log check; 55: only if plan drafted
                const dpLen = state.draftPrompts?.length || 0;
                const evidence = state.currentReceipt?.evidenceCount || 0;
                const hasActionablePlanDraft = await (async () => {
                  try {
                    const slug = state.activeDraft;
                    if (!slug) return false;
                    const { readFutureDraft } = await import("../paths.ts");
                    const c = await readFutureDraft(slug);
                    const { isActionablePlanContent } = await import(
                      "../critical_agent/policy.ts"
                    );
                    return isActionablePlanContent(c);
                  } catch {
                    return false;
                  }
                })();
                const { isDraftReviewValid } =
                  (await import("../critical_agent/policy.ts")) as {
                    isDraftReviewValid: (s: unknown) => boolean;
                  };
                const valid = (() => {
                  try {
                    return isDraftReviewValid(state);
                  } catch {
                    return false;
                  }
                })();
                tryLog(
                  "DRAFT_AUTO_REVIEW_CHECK",
                  `check dpLen=${dpLen} evidence=${evidence} valid=${valid} hasPlan=${hasActionablePlanDraft}`,
                  {
                    quest: state.activeDraft || "",
                    dpLen,
                    evidence,
                    isDraftReviewValid: valid,
                    hasActionablePlanDraft,
                  },
                );
                if (!hasActionablePlanDraft && dpLen >= 1 && evidence >= 7) {
                  tryLog(
                    "PLAN_NOT_DRAFTED_YET",
                    `plan not drafted yet — author the plan by editing the draft file`,
                    { quest: state.activeDraft || "" },
                  );
                  try {
                    const { sendInternalAgentMessage } = await import(
                      "../messaging.ts"
                    );
                    sendInternalAgentMessage(
                      pi,
                      `📝 Plan not yet drafted in \`.pi/quest/future/${state.activeDraft}.md\` — author the plan by editing that file's \`## Implementation Plan\` section directly (goal, 2–3 stages, findings). \`quest_update_state\` cannot touch a draft before reviewer APPROVE; saving a substantive plan sends it for review automatically.`,
                      "steer",
                    );
                  } catch {}
                }
                // Retain threshold constants for observability and backward compatibility (#53)
                const canAutoReviewDespitePlaceholder = dpLen >= 1 &&
                  evidence >= 7;
                if (
                  (dpLen >= 2 || (dpLen >= 1 && evidence >= 7)) &&
                  (hasActionablePlanDraft || canAutoReviewDespitePlaceholder)
                ) {
                  // Note: User prompt does NOT boot up draft reviewer.
                  // Draft reviews are strictly triggered by draft-file edits (handlers.ts).
                }
              }
              const hash = createHash("sha256")
                .update(trimmed)
                .digest("hex")
                .slice(0, 12);
              tryLog(
                "DRAFT_CONVERSATIONAL_IGNORED",
                `draft conversational ignored`,
                {
                  quest: state.activeDraft || "",
                  slug: state.activeDraft,
                  hash,
                  draftPromptsCount: state.draftPrompts?.length || 0,
                },
              );
              // CONVERSATIONAL_ACK ignored while drafting
            } else if (state.active) {
              if (!Array.isArray(state.refinements)) state.refinements = [];
              if (!Array.isArray(state.prompts)) state.prompts = [];

              const isOriginal = state.prompts.length > 0 &&
                state.prompts[0] === trimmed;
              const isLatestRefinement = state.refinements.length > 0 &&
                state.refinements[state.refinements.length - 1] === trimmed;

              if (!isOriginal && !isLatestRefinement) {
                const classification = classifyUserMessage(trimmed);
                tryLog(
                  "CLASSIFICATION_RESULT",
                  `classification ${classification}`,
                  { classification, quest: state.active || "" },
                );
                if (
                  classification !==
                    UserMessageClassification.CONVERSATIONAL_ACK
                ) {
                  const slice = trimmed.slice(0, 120);
                  const hash = createHash("sha256")
                    .update(trimmed)
                    .digest("hex")
                    .slice(0, 12);
                  tryLog("USER_PROMPT", `user prompt`, {
                    classification,
                    quest: state.active || "",
                    slice,
                    hash,
                    intentHash: hash,
                  });
                }

                if (classification === UserMessageClassification.CONFIRMATION) {
                  logUserInteraction(
                    "CONFIRMATION_RECEIVED",
                    "user confirmation received",
                    { quest: state.active || "" },
                  );
                  acceptRootConfirmation(pi, ctx);
                } else if (
                  classification ===
                    UserMessageClassification.REFINEMENT_OR_REQUIREMENT
                ) {
                  logUserInteraction(
                    "USER_REFINEMENT_RECEIVED",
                    "user refinement received",
                    { quest: state.active || "" },
                  );
                  state.refinements.push(trimmed);
                  state.prompts.push(trimmed);
                  if (state.prompts.length > PROMPT_MAX_COUNT) {
                    state.prompts = [
                      state.prompts[0],
                      ...state.prompts.slice(-(PROMPT_MAX_COUNT - 1)),
                    ];
                  }
                  if (state.refinements.length > PROMPT_MAX_COUNT) {
                    state.refinements = state.refinements.slice(
                      -PROMPT_MAX_COUNT,
                    );
                  }
                  triggerReassessment(
                    state,
                    `User refinement received: "${trimmed.slice(0, 100)}..."`,
                    trimmed,
                  );
                  persist(pi, ctx);
                  updateUIStatus(ctx);
                }
              }
            } else if (state.pendingRootQuest) {
              const classification = classifyUserMessage(trimmed);
              tryLog(
                "CLASSIFICATION_RESULT",
                `classification ${classification}`,
                { classification, quest: state.active || "" },
              );
              if (
                classification !==
                  UserMessageClassification.CONVERSATIONAL_ACK
              ) {
                const slice = trimmed.slice(0, 120);
                const hash = createHash("sha256")
                  .update(trimmed)
                  .digest("hex")
                  .slice(0, 12);
                tryLog("USER_PROMPT", `user prompt`, {
                  classification,
                  quest: state.active || "",
                  slice,
                  hash,
                  intentHash: hash,
                });
              }
              if (
                classification ===
                  UserMessageClassification.REFINEMENT_OR_REQUIREMENT
              ) {
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
              const { listActiveQuestRecords, listQuestFiles } = await import(
                "../paths.ts"
              );
              const { FUTURE_DIR } = await import("../constants.ts");
              let activated = false;
              try {
                const activeRecords = await listActiveQuestRecords();
                for (const r of activeRecords) {
                  if (
                    (r.name.length >= 3 &&
                      trimmed.toLowerCase().includes(r.name.toLowerCase())) ||
                    trimmed.toLowerCase().includes(r.qid.toLowerCase())
                  ) {
                    activated = await ensureRootQuestForPrompt(
                      pi,
                      ctx,
                      trimmed,
                    );
                    break;
                  }
                }
                if (!activated) {
                  const futureFiles = await listQuestFiles(FUTURE_DIR);
                  for (const f of futureFiles) {
                    const s = f.replace(/\.md$/, "");
                    if (
                      s.length >= 3 &&
                      trimmed.toLowerCase().includes(s.toLowerCase())
                    ) {
                      activated = await ensureRootQuestForPrompt(
                        pi,
                        ctx,
                        trimmed,
                      );
                      break;
                    }
                  }
                }
              } catch {}
              if (activated) {
                // existing quest resumed, no draft
              } else {
                if (!state.questId) {
                  const { ensureQuestId } = await import("../state.ts");
                  ensureQuestId(ctx);
                }
                const slug = generateSlugFromPrompt(trimmed, 45);
                await createFutureDraftFromPrompt(slug, trimmed);
                try {
                  const { readFutureDraft } = await import("../paths.ts");
                  const c = await readFutureDraft(slug);
                  state.draftLastSavedHash = createHash("sha256")
                    .update(c)
                    .digest("hex")
                    .slice(0, 12);
                } catch {
                  try {
                    state.draftLastSavedHash = createHash("sha256")
                      .update(trimmed)
                      .digest("hex")
                      .slice(0, 12);
                  } catch {}
                }
                state.activeDraft = slug;
                state.draftPrompts = [trimmed];
                state.draftCreatedAt = Date.now();
                state.pendingRootQuest = true;
                state.pendingRootRequest = trimmed;
                // 08: durable verbatim log for t=0 — mirrors refinement branch 307-310
                try {
                  const qid0 = state.questId;
                  if (qid0) {
                    const jPath0 = join(
                      questDirPath(qid0),
                      "draft-prompts.jsonl",
                    );
                    await mkdir(dirname(jPath0), { recursive: true });
                    const rec0 = JSON.stringify({
                      ts: Date.now(),
                      hash: createHash("sha256")
                        .update(trimmed)
                        .digest("hex")
                        .slice(0, 12),
                      slice: trimmed.slice(0, 200),
                      len: trimmed.length,
                    }) + "\n";
                    await appendFile(jPath0, rec0, "utf8");
                  }
                } catch {}
                if (!Array.isArray(state.prompts)) state.prompts = [];
                if (!state.prompts.includes(trimmed)) {
                  state.prompts.push(trimmed);
                }
                const { logQuestTransition } = await import("../logging.ts");
                logQuestTransition(
                  "QUEST_DETECTED",
                  `draft auto-detected for '${slug}'`,
                  { quest: slug },
                );
                logQuestTransition(
                  "QUEST_CREATED",
                  `draft auto-created '${slug}'`,
                  { quest: slug },
                );
                logUserInteraction(
                  "QUEST_CREATED",
                  `auto-drafted '${slug}' from prompt`,
                  { quest: slug },
                );
                persist(pi, ctx);
                updateUIStatus(ctx);
                sendInternalAgentMessage(
                  pi,
                  `📝 **Draft auto-created**: \`.pi/quest/future/${slug}.md\` — accumulating requirements while you talk. Requirements stay in draft (not yet part of active quest). When ready, the reviewer will validate compliance before the plan is presented; then say "go" to promote.`,
                  "steer",
                );
                // Note: Reviewer is NOT booted up on draft creation;
                // draft reviews are only triggered by draft-file edits (handlers.ts).
              }
            }
          } // close else { shouldCapturePrompt }
        } // close if typeof raw === string

        drainPendingResumesAndNotifications(pi, ctx);
        // Phase 22: orphan awaitingReview re-queue + turn-stop steer (A: plan_review/final_acceptance only) — 3-case CRITICAL_REVIEW_ORPHAN_CLEARED
        try {
          const c = getActiveContext(ctx);
          const targetSessionId = getSessionId(c);
          const targetState = sessionStates.get(targetSessionId) ?? getState(c);
          const aw = targetState.awaitingReview;
          // case A: reviewer disabled
          let reviewerDisabled = false;
          try {
            const { getCustomSubagentRunner, isSubagentToolRegistered } =
              await import("../critical_agent/index.ts");
            reviewerDisabled = !Boolean(getCustomSubagentRunner()) &&
              !isSubagentToolRegistered(pi, ctx);
          } catch {}
          if (aw && reviewerDisabled) {
            tryLog(
              "CRITICAL_REVIEW_ORPHAN_CLEARED",
              `orphan reviewer disabled`,
              {
                quest: targetState.active || state.active || "",
                reason: "reviewer_disabled",
                reviewId: aw.reviewId,
                triggerReason: aw.triggerReason,
              },
            );
            targetState.awaitingReview = null;
            targetState.inCriticalReview = false;
            try {
              persist(pi, ctx);
            } catch {}
          } else if (
            aw &&
            (aw.kind === "plan_review" || aw.kind === "final_acceptance")
          ) {
            const { getActiveReviews } = await import(
              "../critical_agent/tracker.ts"
            );
            const hasActive = getActiveReviews().has(aw.reviewId);
            if (!hasActive) {
              tryLog(
                "CRITICAL_REVIEW_ORPHAN_CLEARED",
                `orphan awaiting pending requeue`,
                {
                  quest: targetState.active || state.active || "",
                  reason: "orphan_awaiting_pending_requeue",
                  reviewId: aw.reviewId,
                  triggerReason: aw.triggerReason,
                },
              );
              // Orphan: re-queue as pending coalesced if not already pending
              try {
                const { getPendingReview, setPendingReview } = await import(
                  "../critical_agent/tracker.ts"
                );
                if (!getPendingReview(targetState.active || "", aw.kind)) {
                  setPendingReview(
                    targetState.active || targetState.questId || "quest",
                    {
                      questSlug: targetState.active || targetState.questId ||
                        "quest",
                      kind: aw.kind,
                      triggerReason: aw.triggerReason,
                      planVersion: targetState.planVersion || 1,
                      stateHash: targetState.lastSavedHash ||
                        (targetState.saveGeneration
                          ? targetState.saveGeneration.hash
                          : null),
                      boundaryKey: targetState.lastPlanReviewBoundaryKey ||
                        null,
                      saveCount: targetState.saveCount || 0,
                      requestedAt: Date.now(),
                    },
                  );
                }
              } catch {}
            }
          }
        } catch {}

        const guidelineFp = getGuidelinesFingerprint();
        const awarenessBlock = buildSessionAwarenessBlock(ctx);
        const resumeContext = await loadActiveQuestResumeContext();
        const pressureKey = `${state.saveGeneration?.hash || ""}:${
          state.researchRound || 1
        }:${state.pendingRootQuest ? "pending" : "active"}:${guidelineFp}`;
        const cached = getCachedWorkflow(
          state.saveGeneration?.hash || "",
          pressureKey,
        );
        let workflowInstructions = cached;
        if (!workflowInstructions) {
          const isSteadyState = !state.pendingRootQuest &&
            !state.researchRequired &&
            !state.reassessmentRequired &&
            (() => {
              try {
                return compactionReady();
              } catch {
                return false;
              }
            })();
          workflowInstructions =
            isSteadyState && (state.researchRound || 1) > 1
              ? getCompactWorkflowInstructions(resumeContext)
              : getFullWorkflowInstructions(resumeContext);
          setCachedWorkflow(
            state.saveGeneration?.hash || "",
            pressureKey,
            workflowInstructions,
          );
        }

        // 40: skill trigger — when no active quest but draft/pending exists, hint quest_journal (top, imperative, like vim ftplugin); 52: not while REASSESSMENT_PENDING blocks quest_update_state
        let skillHint = "";
        let steerMsg = "";
        if (!state.active && !state.reassessmentRequired) {
          if (state.activeDraft) {
            skillHint = `Skill: quest_journal — DRAFT ACTIVE for '${state.activeDraft}'. Research requirements and author your proposal & implementation plan in \`.pi/quest/future/${state.activeDraft}.md\` (using edit, write, or quest_update_state). Saving the plan triggers the adversarial plan reviewer automatically. Code implementation is blocked until reviewer APPROVE and draft promotion.\n`;
            steerMsg = `📝 Draft active for '${state.activeDraft}'. Author proposal & implementation plan in \`.pi/quest/future/${state.activeDraft}.md\` (via edit, write, or quest_update_state).`;
          } else if (state.pendingRootQuest) {
            skillHint = `Skill: quest_journal — CALL quest_update_state on turn 1 with research findings (goal, plan, findings). Do not run bash loops without establishing durable quest.\n`;
            steerMsg = "📝 Skill quest_journal: establish durable quest via quest_update_state now";
          }
        }
        // steer so execution.log grep-able within turn0
        if (steerMsg) {
          try {
            sendInternalAgentMessage(
              pi,
              steerMsg,
              "steer",
            );
          } catch {}
        }
        if (event && typeof event.systemPrompt === "string") {
          return {
            systemPrompt:
              `${event.systemPrompt}\n\n${skillHint}${awarenessBlock}${workflowInstructions}`,
          };
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
      const isSteadyState = !state.pendingRootQuest &&
        !state.researchRequired &&
        !state.reassessmentRequired &&
        (() => {
          try {
            return compactionReady();
          } catch {
            return false;
          }
        })() &&
        (state.researchRound || 1) > 1;
      if (
        set.has("quest_mark_saved") ||
        set.has("quest_update_state") ||
        state.active ||
        state.pendingRootQuest
      ) {
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
          "Never write .pi/quest/current/**/quest.md via bash (cat > quest.md / echo > quest.md) — use quest_update_state to initialize the durable quest with research findings. Direct bash writes to quest.md are blocked before execution and must be replaced by quest_update_state.",
          "Top-level Quest Completion: When root quest is done, prompt user via `ask_questions`: refine, archive & auto-compact, archive without auto-compact, or manual mode.",
        ];
      }
      return [];
    });
  }
}
