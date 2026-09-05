import { createHash } from "node:crypto";
import { basename } from "node:path";
import {
  acceptRootConfirmation,
  classifyUserMessage,
  handleAskQuestionsResult,
} from "../classification.ts";
import {
  getRetryDeliverAs,
  getRetryMaxTurns,
  isSemanticSummaryEnabled,
  isThoughtLoggingEnabled,
} from "../config.ts";
import {
  advanceSteerTurnCounter,
  checkAndTriggerDeferredCompaction,
  compactionReady,
  createOrGetCompactionTransaction,
  dispatchCompactionResume,
  drainPendingResumesAndNotifications,
  handleCompactionCompleted,
  requestPeriodicCheckpoint,
  retryPendingResume,
} from "../compaction.ts";
import { checkAndTriggerDirectionReview } from "../critical_agent.ts";
import {
  findActiveReviewForQuest,
  getActiveReviews,
  getPendingReview,
} from "../critical_agent/tracker.ts";
import {
  PROMPT_MAX_CHARS,
  PROMPT_MAX_COUNT,
  QUEST_CURRENT_DIR,
  SUBSTANTIVE_TURNS_PER_DIRECTION_REVIEW,
} from "../constants.ts";
import { ensureRootQuestForPrompt } from "../lifecycle.ts";
import {
  logCompactionTransition,
  logContinuationAnomaly,
  logEvent,
  logImplementationOutcome,
  logResumeTransition,
  logToolActivity,
  logTurnBoundary,
  logUserInteraction,
  normalizeLogPath,
  sanitizeLogString,
  tryLog,
} from "../logging.ts";
import { getWorkflowInstructions } from "../markdown.ts";
import {
  logDebug,
  logError,
  reportAgentError,
  sendInternalAgentMessage,
  shouldCapturePrompt,
} from "../messaging.ts";
import { questPath, shouldStartPersistentQuest } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadActiveQuestResumeContext } from "../reconstruction.ts";
import {
  recordObservedInvestigation,
  triggerReassessment,
} from "../research.ts";
import {
  getActiveContext,
  getSessionId,
  getState,
  sessionStartMap,
  sessionStates,
  state,
} from "../state.ts";
import { reconcilePendingSubquestResume } from "../subquest.ts";
import {
  ExtensionAPI,
  ExtensionContext,
  QuestErrorCode,
  UserMessageClassification,
} from "../types.ts";
import { buildSessionAwarenessBlock, updateUIStatus } from "../ui.ts";
import { classifyToolCall, normalizePath } from "../utils.ts";
import { syncImplementationPermission } from "../gates.ts";
import { isDraftRevisionOutstanding } from "../critical_agent/snapshot.ts";
import {
  analyzeTurnToolResults,
  applyTurnEndStateTransitions,
  classifyActivityPhase,
  detectBashToolFailure,
} from "./turn_analysis.ts";

export async function handleTurnStart(
  event: any,
  _ctx: ExtensionContext,
): Promise<void> {
  if (state.pickerCancelled) return;
  if (!state.active && !state.pendingRootQuest && !state.activeDraft) return;

  // B4: stale inCriticalReview recovery — clear orphaned flag if no active review exists
  if (state.inCriticalReview) {
    try {
      const active = findActiveReviewForQuest(
        state.active || state.questId || "",
      );
      const anyActive = [...getActiveReviews().values()].some((r) =>
        r.status === "starting" || r.status === "running"
      );
      if (!active && !anyActive) {
        tryLog(
          "CRITICAL_REVIEW_STALE_CLEARED",
          "stale inCriticalReview cleared (no active review)",
          { quest: state.active || "" },
        );
        state.inCriticalReview = false;
      }
    } catch {}
  }
  const turnIndex = typeof event?.turnIndex === "number"
    ? event.turnIndex
    : (state.currentTurn || 0) + 1;
  state.currentTurn = turnIndex;
  state.currentTurnCorrelationId = `turn_${turnIndex}_${
    Date.now().toString(36)
  }_${Math.random().toString(36).slice(2, 6)}`;

  const intent = state.prompts && state.prompts.length > 0
    ? state.prompts[state.prompts.length - 1]
    : state.pendingRootRequest || state.active || "";

  // Fix 2: Sync implementationAllowed before logging so TURN_START always reflects fresh value
  syncImplementationPermission(state, _ctx);

  // Check draft-revision-pending state: active draft with outstanding revision needed
  const _lastReviewNeedsRevision = Boolean(
    !state.lastPlanReviewApproval &&
      state.lastCriticalReview?.kind === "plan_review" &&
      (state.lastCriticalReview?.verdict === "REVISE" ||
        state.lastCriticalReview?.verdict === "FAIL" ||
        state.lastCriticalReview?.verdict === "UNCERTAIN"),
  );
  const _draftRevisionPending = !!(
    state.activeDraft && !state.lastPlanReviewApproval &&
    (isDraftRevisionOutstanding(state) || _lastReviewNeedsRevision)
  );
  const _isAwaitingReview = Boolean(
    state.awaitingReview &&
      (state.awaitingReview.kind === "plan_review" ||
        state.awaitingReview.kind === "final_acceptance"),
  );
  const activeGate = state.reassessmentRequired
    ? "REASSESSMENT_PENDING"
    : _isAwaitingReview
    ? "AWAITING_REVIEW"
    : _draftRevisionPending
    ? "DRAFT_REVISION_PENDING"
    : state.activeDraft
    ? "DRAFT_PENDING"
    : state.researchRequired || !state.researchComplete
    ? "RESEARCH_PENDING"
    : state.awaitingUserConfirmation
    ? "CONFIRMATION_PENDING"
    : "IMPLEMENTATION_ALLOWED";
  const phase = state.reassessmentRequired
    ? "reassessment"
    : _isAwaitingReview
    ? "awaiting_review"
    : _draftRevisionPending
    ? "draft_revision"
    : state.activeDraft
    ? "drafting"
    : state.researchRequired || !state.researchComplete
    ? "research"
    : state.awaitingUserConfirmation
    ? "confirmation"
    : "implementation";
  const ctxSessionId = getSessionId(getActiveContext(_ctx));
  if (!sessionStartMap.has(ctxSessionId)) {
    sessionStartMap.set(ctxSessionId, Date.now());
  }
  if (sessionStartMap.size > 100) {
    const first = sessionStartMap.keys().next().value;
    if (first) sessionStartMap.delete(first);
  }
  const elapsedMs = Date.now() -
    (sessionStartMap.get(ctxSessionId) || Date.now());
  const intentHash = createHash("sha256")
    .update(intent || "", "utf8")
    .digest("hex")
    .slice(0, 12);
  const intentLen = (intent || "").length;
  const slice = sanitizeLogString(intent, 80);

  // INITIAL_PROMPT once per run
  if (!state.initialPromptLogged) {
    state.initialPromptLogged = true;
    logEvent("INITIAL_PROMPT", "initial prompt captured", {
      quest: state.active || "",
      hash: intentHash,
      intentLen,
      ref: "run/initial-prompt.txt",
      opencodeSessionId: ctxSessionId,
      elapsedMs,
    });
  }

  logTurnBoundary(
    "TURN_START",
    `agent turn started — phase ${phase} gate ${activeGate} plan v${
      state.planVersion || 1
    } round ${state.researchRound || 1}`,
    {
      quest: state.active || state.activeDraft || "",
      turn: state.currentTurn,
      correlationId: state.currentTurnCorrelationId,
      intentHash,
      intentLen,
      slice,
      intent: slice,
      phase,
      activeGate,
      planVersion: state.planVersion || 1,
      round: state.researchRound || 1,
      implementationAllowed: Boolean(state.implementationAllowed),
      elapsedMs,
      opencodeSessionId: ctxSessionId,
      piSessionId: ctxSessionId,
    },
  );

  // inference-free semantic snapshot — always 1 line/turn for readability (no tokens), human sentence
  const prevKey = state._lastSemanticKey;
  const curKey = `${phase}:${state.planVersion || 1}:${activeGate}`;
  const isChange = prevKey !== curKey;
  state._lastSemanticKey = curKey;
  const readable = `phase ${phase} gate ${activeGate} plan v${
    state.planVersion || 1
  } round ${state.researchRound || 1}${
    isChange && prevKey ? ` (changed from ${prevKey})` : ""
  }`;
  logEvent(
    "SEMANTIC_SNAPSHOT",
    isChange && prevKey
      ? `${prevKey}→${curKey} — ${readable}`
      : `${curKey} — ${readable}`,
    {
      quest: state.active || state.activeDraft || "",
      from: prevKey || curKey,
      to: curKey,
      planVersion: state.planVersion || 1,
      activeGate,
      elapsedMs,
      opencodeSessionId: ctxSessionId,
      piSessionId: ctxSessionId,
    },
  );
}

function turnTerminalText(event: any): string {
  try {
    const cand = event?.response ??
      event?.output ??
      (Array.isArray(event?.messages)
        ? (event.messages[event.messages.length - 1]?.content ??
          event.messages[event.messages.length - 1]?.text)
        : undefined);
    if (typeof cand === "string") return cand;
    if (Array.isArray(cand)) {
      return cand
        .map((x: any) =>
          typeof x === "string" ? x : (x?.text ?? x?.content ?? "")
        )
        .join(" ");
    }
    if (cand && typeof cand === "object") {
      const o = cand as {
        content?: unknown;
        text?: unknown;
        message?: unknown;
      };
      return String(o.content ?? o.text ?? o.message ?? "");
    }
    return "";
  } catch {
    return "";
  }
}

function handleTurnContinuationRetry(
  pi: ExtensionAPI,
  ctx: ExtensionContext,
  event: any,
  analysis: ReturnType<typeof analyzeTurnToolResults>,
): void {
  if (!state.active && !state.activeDraft) return;
  // A retry re-entry must not fire twice for the same turn.
  const turnIndex = state.currentTurn || 0;
  if (state.retryLastStalledTurn === turnIndex) return;

  const toolResults: any[] = Array.isArray(event.toolResults)
    ? event.toolResults
    : [];
  const terminalText = turnTerminalText(event).trim();

  // A truncated/interrupted turn: the assistant message carries an explicit
  // truncation/interruption stop-reason, no tool calls ran, and the response
  // produced no terminal text (i.e. it was cut off before completing output or
  // a tool call). Requiring an explicit stop-reason keeps clean empty turns
  // (stopReason undefined) from being mistaken for truncation.
  const stop = event?.message?.stopReason ?? event?.stopReason;
  const truncated = stop === "length" || stop === "error" || stop === "aborted";
  const stalled = truncated && toolResults.length === 0 &&
    terminalText.length === 0;
  if (!stalled) {
    // A real, substantive/incomplete-but-produced turn resets the budget.
    state.retryTurnsUsed = 0;
    state.retryLastStalledTurn = null;
    return;
  }

  const maxTurns = getRetryMaxTurns(state);
  if (maxTurns <= 0) {
    state.retryTurnsUsed = 0;
    state.retryLastStalledTurn = null;
    return;
  }

  const used = state.retryTurnsUsed || 0;
  const qp = questPath(state.active || state.activeDraft || "");
  if (used >= maxTurns || analysis.meaningfulFailureDetected) {
    logContinuationAnomaly(
      "TURN_RETRY_EXHAUSTED",
      `turn retry budget exhausted (${used}/${maxTurns}) — truncation not resolved`,
      {
        quest: state.active || "",
        turn: turnIndex,
        attempt: used,
        max: maxTurns,
      },
    );
    logEvent(
      "TURN_RETRY_EXHAUSTED",
      `turn retry budget exhausted (${used}/${maxTurns})`,
      {
        quest: state.active || state.activeDraft || "",
        turn: turnIndex,
        attempt: used,
        max: maxTurns,
      },
    );
    reportAgentError(
      pi,
      ctx,
      `[Quest Journal] A turn ended without producing any tool calls or response text (repeated ${used} consecutive retr${
        used === 1 ? "y" : "ies"
      }). This likely indicates a truncated or interrupted response.\n\nThe durable quest state remains authoritative.`,
      {
        code: QuestErrorCode.CONTINUATION_FAILURE,
        requiredNextAction:
          `Read ${qp}, reconcile working memory from the durable quest state if necessary, and continue from the recorded Exact Next Action.`,
        details: { TurnsRetried: used, MaxRetries: maxTurns },
      },
    );
    state.retryTurnsUsed = 0;
    state.retryLastStalledTurn = null;
    return;
  }

  const next = used + 1;
  state.retryTurnsUsed = next;
  state.retryLastStalledTurn = turnIndex;
  logContinuationAnomaly(
    "TURN_RETRY",
    `truncated/stalled turn detected — auto-retrying continuation (${next}/${maxTurns})`,
    {
      quest: state.active || "",
      turn: turnIndex,
      attempt: next,
      max: maxTurns,
    },
  );
  logEvent("TURN_RETRY_ATTEMPTED", `truncated turn retry ${next}/${maxTurns}`, {
    quest: state.active || state.activeDraft || "",
    turn: turnIndex,
    attempt: next,
    max: maxTurns,
  });
  const directive = `⚡ **Turn Continuation Retry (${next}/${maxTurns})**:
The previous turn ended without producing any output or tool calls — it appears to have been truncated or interrupted mid-completion.

Read \`${qp}\` to recover the durable quest state, then continue directly from the recorded **Exact Next Action**.

Do not restart research or re-read sources unnecessarily; use the durable quest state as the single source of truth.`;
  sendInternalAgentMessage(pi, directive, getRetryDeliverAs(state));
  persist(pi, ctx);
}

export async function handleTurnEnd(
  pi: ExtensionAPI,
  ctx: ExtensionContext,
  event: any,
): Promise<void> {
  if (state.pickerCancelled) return;

  drainPendingResumesAndNotifications(pi, ctx);

  if (state.compactionPending) return;

  if (state.archiveCompactionPending) {
    checkAndTriggerDeferredCompaction(pi, ctx);
    return;
  }

  if (!state.active && !state.pendingRootQuest && !state.activeDraft) return;

  if (typeof event?.turnIndex === "number") {
    state.currentTurn = event.turnIndex;
  }
  if (!state.currentTurnCorrelationId) {
    state.currentTurnCorrelationId = `turn_${state.currentTurn || 1}_${
      Date.now().toString(36)
    }`;
  }

  const toolResults: any[] = Array.isArray(event.toolResults)
    ? event.toolResults
    : [];
  const analysis = analyzeTurnToolResults(
    toolResults,
    state.active || state.activeDraft || "",
  );
  applyTurnEndStateTransitions(state, analysis, pi, ctx);

  const readsCount = toolResults.filter((tr: any) => {
    const name = (tr?.toolName || tr?.name || "").toLowerCase();
    return (
      name === "read" ||
      name === "doc_to_md" ||
      name === "memory_read" ||
      name === "memory_status"
    );
  }).length;

  const searchesCount = toolResults.filter((tr: any) => {
    const name = (tr?.toolName || tr?.name || "").toLowerCase();
    return (
      name === "search_graph" ||
      name === "query_graph" ||
      name === "trace_path" ||
      name === "get_code_snippet" ||
      name === "search_code" ||
      name === "get_graph_schema" ||
      name === "get_architecture" ||
      name === "web_search" ||
      name === "source_check" ||
      name === "memory_search" ||
      name === "fetch_content" ||
      name === "get_search_content"
    );
  }).length;

  const writesCount = toolResults.filter((tr: any) => {
    const name = (tr?.toolName || tr?.name || "").toLowerCase();
    return (
      name === "edit" ||
      name === "write" ||
      name === "user_edit" ||
      name === "user_write"
    );
  }).length;

  const commandsCount = toolResults.filter((tr: any) => {
    const name = (tr?.toolName || tr?.name || "").toLowerCase();
    return name === "bash" || name === "user_bash" || name.startsWith("bg_run");
  }).length;

  const mutationsCount = toolResults.filter((tr: any) => {
    const name = (tr?.toolName || tr?.name || "").toLowerCase();
    return (
      name === "edit" ||
      name === "write" ||
      name === "bash" ||
      name === "user_bash"
    );
  }).length;
  const failuresCount = analysis.failureCount;

  let turnConsequence: string | undefined = undefined;
  if (analysis.meaningfulFailureDetected) {
    turnConsequence = "TRIGGERED_REASSESSMENT";
  } else if (analysis.didUpdateQuestThisTurn) {
    turnConsequence = "CHECKPOINT_SAVED";
  }

  const turnElapsedMs = (() => {
    try {
      const sid = getSessionId(getActiveContext(ctx));
      const st = sessionStartMap.get(sid);
      return st ? Date.now() - st : undefined;
    } catch {
      return undefined;
    }
  })();
  const turnOpSess = (() => {
    try {
      return getSessionId(getActiveContext(ctx));
    } catch {
      return undefined;
    }
  })();
  logTurnBoundary(
    "TURN_END",
    `turn completed — phase ${
      state.reassessmentRequired
        ? "reassessment"
        : state.researchRequired || !state.researchComplete
        ? "research"
        : state.awaitingUserConfirmation
        ? "confirmation"
        : "implementation"
    } gate ${
      state.reassessmentRequired
        ? "REASSESSMENT_PENDING"
        : state.researchRequired || !state.researchComplete
        ? "RESEARCH_PENDING"
        : state.awaitingUserConfirmation
        ? "CONFIRMATION_PENDING"
        : "IMPLEMENTATION_ALLOWED"
    } tools ${toolResults.length}`,
    {
      quest: state.active || "",
      turn: state.currentTurn,
      correlationId: state.currentTurnCorrelationId,
      substantive: analysis.isSubstantiveTurn,
      toolsUsed: toolResults.length,
      reads: readsCount,
      searches: searchesCount,
      writes: writesCount,
      commands: commandsCount,
      mutations: mutationsCount,
      failures: failuresCount,
      filesModified: Array.isArray(state.sessionModifiedFiles) &&
          state.sessionModifiedFiles.length > 0
        ? state.sessionModifiedFiles.slice(-5).join(",")
        : undefined,
      consequence: turnConsequence,
      activeGate: state.reassessmentRequired
        ? "REASSESSMENT_PENDING"
        : state.researchRequired || !state.researchComplete
        ? "RESEARCH_PENDING"
        : state.awaitingUserConfirmation
        ? "CONFIRMATION_PENDING"
        : "IMPLEMENTATION_ALLOWED",
      categories: analysis.failureCategories.length > 0
        ? analysis.failureCategories.join(",")
        : undefined,
      questDirty: Boolean(state.dirty),
      implementationAllowed: Boolean(state.implementationAllowed),
      elapsedMs: turnElapsedMs,
      opencodeSessionId: turnOpSess,
    },
  );

  // always-on semantic STEP_SUMMARY 1/turn (no tokens) — human sentence of what turn did; thought as proxy if enabled or harness provides text
  try {
    const curGate = state.reassessmentRequired
      ? "REASSESSMENT_PENDING"
      : state.researchRequired || !state.researchComplete
      ? "RESEARCH_PENDING"
      : state.awaitingUserConfirmation
      ? "CONFIRMATION_PENDING"
      : "IMPLEMENTATION_ALLOWED";
    const curPhase = state.reassessmentRequired
      ? "reassessing"
      : state.researchRequired || !state.researchComplete
      ? "research"
      : state.awaitingUserConfirmation
      ? "confirmation"
      : "implementation";
    const semanticEnabled = isSemanticSummaryEnabled(state);
    const intent = curGate === "RESEARCH_PENDING"
      ? "research"
      : curGate === "CONFIRMATION_PENDING"
      ? "awaiting-review"
      : curGate === "REASSESSMENT_PENDING"
      ? "revising"
      : curPhase;
    const summaryLine = `${intent} planVersion${
      state.planVersion || 1
    } gate=${curGate} tools=${toolResults.length} reads=${readsCount} writes=${writesCount} failures=${failuresCount}${
      turnConsequence ? ` consequence=${turnConsequence}` : ""
    }`;
    // unconditional: execution.log must be semantically legible without flag
    tryLog("STEP_SUMMARY", summaryLine.slice(0, 300), {
      quest: state.active || state.activeDraft || "",
      intent,
      planVersion: state.planVersion || 1,
      activeGate: curGate,
      elapsedMs: turnElapsedMs,
      opencodeSessionId: turnOpSess,
      piSessionId: turnOpSess,
    });
    // verbose variant behind flag still emitted but deduped above? keep single line
    if (!semanticEnabled) {
      /* already logged */
    }
    // thought proxy: harness text if available, else toolResult summary slice
    const shouldLogThought = isThoughtLoggingEnabled(state) ||
      Boolean(event?.response || event?.output || event?.messages);
    if (shouldLogThought) {
      let lastThought: string | undefined = state.lastThought;
      // Guard existing lastThought if it is the "[object Object]" placeholder
      if (
        lastThought === "[object Object]" ||
        (typeof lastThought === "string" &&
          lastThought.trim() === "[object Object]")
      ) {
        lastThought = undefined;
      }
      try {
        const cand = event?.response ??
          event?.output ??
          (Array.isArray(event?.messages)
            ? (event.messages[event.messages.length - 1]?.content ??
              event.messages[event.messages.length - 1]?.text)
            : undefined);
        if (
          typeof cand === "string" &&
          cand.trim() &&
          cand.trim() !== "[object Object]"
        ) {
          lastThought = cand;
        } else if (Array.isArray(cand)) {
          const arrStr = cand
            .map((x: any) =>
              typeof x === "string" ? x : (x?.text ?? x?.content ?? "")
            )
            .join(" ");
          if (arrStr.trim() && arrStr.trim() !== "[object Object]") {
            lastThought = arrStr.slice(0, 500);
          }
        } else if (cand && typeof cand === "object") {
          const o = cand as {
            content?: unknown;
            text?: unknown;
            message?: unknown;
          };
          const raw = o.content ?? o.text ?? o.message ?? "";
          if (
            typeof raw === "string" &&
            raw.trim() &&
            raw.trim() !== "[object Object]"
          ) {
            lastThought = raw;
          } else if (Array.isArray(raw)) {
            const arrStr = raw
              .map((x: any) =>
                typeof x === "string" ? x : (x?.text ?? x?.content ?? "")
              )
              .join(" ");
            if (arrStr.trim() && arrStr.trim() !== "[object Object]") {
              lastThought = arrStr.slice(0, 500);
            }
          } else if (raw && typeof raw === "object") {
            // Handle nested content: {content: {text:"..."}} or {content:[{text:"..."}]}
            const nestedRaw = raw as { text?: unknown; content?: unknown };
            const nested = nestedRaw.text ??
              (Array.isArray(nestedRaw.content)
                ? nestedRaw.content
                  .map((x: any) =>
                    typeof x === "string" ? x : x?.text || x?.content || ""
                  )
                  .join(" ")
                : (nestedRaw.content ?? ""));
            if (
              typeof nested === "string" &&
              nested.trim() &&
              nested.trim() !== "[object Object]"
            ) {
              lastThought = nested;
            } else {
              const js = JSON.stringify(cand);
              if (js && js !== "{}" && js !== '"[object Object]"') {
                // try to extract text from JSON wrapper
                try {
                  const parsed = JSON.parse(js);
                  if (Array.isArray(parsed)) {
                    const t = parsed.map((x: any) => x?.text ?? "").join(" ");
                    if (t.trim()) lastThought = t.slice(0, 500);
                    else lastThought = js.slice(0, 500);
                  } else lastThought = js.slice(0, 500);
                } catch {
                  lastThought = js.slice(0, 500);
                }
              }
            }
          } else if (raw && String(raw).trim() !== "[object Object]") {
            lastThought = String(raw);
          } else {
            // cand itself is {type:"text",text:"..."} without content wrapper
            const direct = (cand as { text?: unknown }).text ?? "";
            if (
              typeof direct === "string" &&
              direct.trim() &&
              direct.trim() !== "[object Object]"
            ) {
              lastThought = direct.slice(0, 500);
            } else {
              const js2 = JSON.stringify(cand);
              try {
                const parsed2 = JSON.parse(js2);
                if (Array.isArray(parsed2)) {
                  const t2 = parsed2.map((x: any) => x?.text ?? "").join(" ");
                  if (t2.trim()) lastThought = t2.slice(0, 500);
                }
              } catch {}
            }
          }
        }
      } catch {}
      if (!lastThought && toolResults.length > 0) {
        // proxy: last tool's truncated output as thought slice — extract plain text from content arrays
        try {
          const last = toolResults[toolResults.length - 1];
          const lastObj = last as {
            content?: unknown;
            output?: unknown;
            text?: unknown;
            toolName?: unknown;
          };
          let proxyRaw: any = lastObj?.content ??
            lastObj?.output ??
            lastObj?.text ??
            last?.toolName ??
            "";
          let proxyStr: string;
          if (typeof proxyRaw === "string") proxyStr = proxyRaw;
          else if (Array.isArray(proxyRaw)) {
            proxyStr = proxyRaw
              .map((x: any) =>
                typeof x === "string" ? x : (x?.text ??
                  x?.content ??
                  (typeof x === "object" ? JSON.stringify(x) : String(x)))
              )
              .join(" ");
          } else if (proxyRaw && typeof proxyRaw === "object") {
            // {type:"text",text:"..."} or {content:"..."}
            const pr = proxyRaw as { text?: string; content?: unknown };
            proxyStr = pr.text ??
              (typeof pr.content === "string"
                ? pr.content
                : JSON.stringify(proxyRaw));
            if (Array.isArray(pr.content)) {
              proxyStr = pr.content
                .map((x: any) =>
                  typeof x === "string" ? x : (x?.text ?? x?.content ?? "")
                )
                .join(" ");
            }
          } else proxyStr = String(proxyRaw);
          // strip outer JSON wrapper if still like [{"type":"text","text":"..."}]
          if (proxyStr.trim().startsWith("[") && proxyStr.includes('"text"')) {
            try {
              const arr = JSON.parse(proxyStr);
              if (Array.isArray(arr)) {
                proxyStr = arr
                  .map((x: any) => x?.text ?? x?.content ?? "")
                  .join(" ");
              }
            } catch {}
          }
          if (proxyStr.trim() && proxyStr.trim() !== "[object Object]") {
            lastThought = proxyStr.slice(0, 500);
          }
        } catch {}
      }
      if (
        lastThought &&
        lastThought.trim() &&
        lastThought.trim() !== "[object Object]"
      ) {
        const th = createHash("sha256")
          .update(lastThought, "utf8")
          .digest("hex")
          .slice(0, 12);
        tryLog(
          "AGENT_THOUGHT",
          `thought ${th} ${sanitizeLogString(lastThought, 80)}`,
          {
            quest: state.active || state.activeDraft || "",
            thoughtHash: th,
            thoughtLen: lastThought.length,
            thoughtSlice: sanitizeLogString(lastThought, 200),
            elapsedMs: turnElapsedMs,
            opencodeSessionId: turnOpSess,
            piSessionId: turnOpSess,
          },
        );
      }
    }
  } catch {}

  if (state.consecutiveFailures === 3) {
    logContinuationAnomaly(
      "REPEATED_FAILURE",
      `consecutive failures reached threshold (count=3)`,
      {
        quest: state.active || "",
        count: 3,
      },
    );
  }
  handleTurnContinuationRetry(pi, ctx, event, analysis);
  if (
    (state.substantiveTurnsSinceCheckpoint || 0) >=
      SUBSTANTIVE_TURNS_PER_DIRECTION_REVIEW
  ) {
    logContinuationAnomaly(
      "NO_PROGRESS",
      `turns without state checkpoint reached threshold (turns=${state.substantiveTurnsSinceCheckpoint})`,
      {
        quest: state.active || "",
        turns: state.substantiveTurnsSinceCheckpoint,
      },
    );
    // Plan-block throttle at handler layer to avoid even entering review launch path
    const hasActivePlan = (state.active &&
      findActiveReviewForQuest(state.active)?.kind === "plan_review") ||
      [...getActiveReviews().values()].some(
        (r) =>
          r.kind === "plan_review" &&
          (r.status === "starting" || r.status === "running"),
      );
    const hasPendingPlan = state.active
      ? !!getPendingReview(state.active, "plan_review")
      : false;
    if (hasActivePlan || hasPendingPlan) {
      logEvent(
        "DIRECTION_REVIEW_THROTTLED",
        `direction review throttled at handler (plan_review active/pending)`,
        {
          quest: state.active || "",
          triggerReason: "no_progress",
          reason: hasActivePlan ? "plan_review_active" : "plan_review_pending",
        },
      );
      state.substantiveTurnsSinceCheckpoint = 0;
    } else {
      await checkAndTriggerDirectionReview(pi, ctx, "no_progress");
    }
  }

  advanceSteerTurnCounter();
  requestPeriodicCheckpoint(pi, ctx, false);
  checkAndTriggerDeferredCompaction(pi, ctx);
}

export async function handleToolResult(
  event: any,
  _ctx: ExtensionContext,
  pi?: ExtensionAPI,
): Promise<void> {
  if (!state.active && !state.pendingRootQuest && !state.activeDraft) return;
  const toolName = event?.toolName || event?.name || "";
  const toolInput = event?.input || event?.args || {};
  const toolOutput = event?.content || event?.output || "";
  const rawIsError = Boolean(
    event?.isError ||
      event?.error ||
      (event?.details &&
        (event?.details?.error || event?.details?.success === false)),
  );
  const normName = (toolName || "").toLowerCase().trim();
  // 41: bash cat > quest.md gate-blocked while PROVISIONAL_RESEARCH_PENDING must be GATE_BLOCKED not TOOL_FAILURE
  const rawCmdForGate = typeof toolInput === "string"
    ? toolInput
    : toolInput?.command || toolInput?.cmd || "";
  const isQuestWriteBlocked = rawIsError &&
    (normName === "bash" || normName === "user_bash") &&
    /quest\.md|\.pi\/quest\/(current|future)/.test(rawCmdForGate);
  // 51: draft_not_approved while REASSESSMENT_PENDING is GATE_BLOCKED not TOOL_FAILURE
  const isCoalescenceGateBlocked = Boolean(
    event?.details?.gateBlocked &&
      event?.details?.code === "REVIEW_COALESCENCE_PENDING",
  );
  // Whitelist rg/grep exit 1 (no matches) — not an error, still counts as investigation
  // 47: read future/current *.md ENOENT while pendingRootQuest/activeDraft is investigation, not failure
  const readPathForWhitelist = typeof toolInput?.path === "string"
    ? toolInput.path
    : (toolInput as { file?: string })?.file || "";
  const isFutureReadENOENT =
    (normName === "read" || normName === "doc_to_md") &&
    /\.pi\/quest\/(current|future)\/.*\.md/.test(readPathForWhitelist) &&
    /ENOENT|no such file/i.test(
      String(
        event?.error?.message ||
          event?.message ||
          event?.details?.error ||
          toolOutput ||
          "",
      ),
    ) &&
    Boolean(state.pendingRootQuest || state.activeDraft);
  let effectiveIsError = rawIsError;
  if (isCoalescenceGateBlocked) {
    effectiveIsError = false;
  } else if (isFutureReadENOENT) {
    effectiveIsError = false;
  } else if (isQuestWriteBlocked) {
    effectiveIsError = false;
  } else if ((normName === "bash" || normName === "user_bash") && rawIsError) {
    const bashFailure = detectBashToolFailure(event);
    if (!bashFailure.hasFailure) {
      effectiveIsError = false;
    }
  }
  recordObservedInvestigation(
    state,
    toolName,
    toolInput,
    toolOutput,
    effectiveIsError,
  );

  // F3(i): auto re-review on draft-file edit
  if (
    pi &&
    state.activeDraft &&
    !effectiveIsError
  ) {
    const normTool = (toolName || "").toLowerCase().trim();
    if (
      normTool === "edit" ||
      normTool === "write" ||
      normTool === "user_edit" ||
      normTool === "user_write" ||
      normTool === "replace_file_content" ||
      normTool === "write_to_file"
    ) {
      const toolPath = typeof toolInput?.path === "string"
        ? toolInput.path
        : typeof toolInput?.file === "string"
        ? toolInput.file
        : typeof toolInput?.targetFile === "string"
        ? toolInput.targetFile
        : typeof toolInput?.TargetFile === "string"
        ? toolInput.TargetFile
        : "";
      if (toolPath.replace(/\\/g, "/").endsWith(`future/${state.activeDraft}.md`)) {
        try {
          const slug = state.activeDraft;
          tryLog("DRAFT_PLAN_EDITED", `draft file edited: ${toolPath}`, { quest: slug });
          // If a review is running for this draft, cancel it so the fresh one runs immediately
          try {
            const {
              findActiveReviewForQuest,
              cancelActiveReview,
              clearPendingReview,
              reviewPromiseByKey,
            } = await import("../critical_agent/tracker.ts");
            const active = findActiveReviewForQuest(slug);
            if (active?.kind === "plan_review" && active.reviewId) {
              cancelActiveReview(active.reviewId, "draft_revised", _ctx);
              tryLog(
                "REVIEW_CANCELLED_DRAFT_REVISED",
                `cancelled review ${active.reviewId} for draft revision`,
                { quest: slug, reviewId: active.reviewId },
              );
              const { removeReviewActiveFile } = await import(
                "../utils/mutex.ts"
              );
              const questId = state.questId || "";
              if (questId) removeReviewActiveFile(questId);
              const oldBoundary = active.snapshot?.boundaryKey || "";
              const oldHash = oldBoundary.split(":").pop() || "";
              if (questId && oldHash) {
                reviewPromiseByKey.delete(
                  `${questId}:plan_review:draft:${slug}:${oldHash}`,
                );
              }
              clearPendingReview(slug, "plan_review");
            }
          } catch {}
          const { checkAndTriggerPlanReview } = await import("../critical_agent/policy.ts");
          checkAndTriggerPlanReview(pi, _ctx).catch(() => {});
        } catch {}
      }
    }
  }

  const activeQuest = state.active || "";

  let isFailure = effectiveIsError;
  let failureReason: string | undefined = undefined;

  // 54: enrich no_active_quest with params keys for diagnostics
  if (normName.includes("quest_update_state") && effectiveIsError) {
    try {
      const keys = Object.keys(toolInput || {}).join(",");
      if (!keys) {
        failureReason = `no_active_quest paramsKeys=[] raw=${
          JSON.stringify(toolInput).slice(0, 120)
        }`;
      }
    } catch {}
  }

  if (isCoalescenceGateBlocked) {
    isFailure = false;
  } else if (isFutureReadENOENT) {
    isFailure = false;
  } else if (isQuestWriteBlocked) {
    isFailure = false;
  } else if (normName === "bash" || normName === "user_bash") {
    const bashFailure = detectBashToolFailure(event);
    if (bashFailure.hasFailure) {
      isFailure = true;
      failureReason = bashFailure.reason;
    } else {
      // Whitelisted search no-match — ensure not treated as failure even if rawIsError was true
      isFailure = false;
    }
  } else if (effectiveIsError) {
    failureReason = event?.error?.message ||
      event?.message ||
      (event?.details && event.details.error) ||
      "tool execution error";
  }

  const operation = isFailure ? "failure" : "success";
  const phase = classifyActivityPhase(normName, toolInput, state, isFailure);

  let targetPath: string | undefined = undefined;
  let command: string | undefined = undefined;
  let query: string | undefined = undefined;
  let filesModified: string | undefined = undefined;
  let failureId: string | undefined = undefined;
  let consequence: string | undefined = undefined;
  let recoveryFor: string | undefined = undefined;

  if (
    normName === "read" ||
    normName === "edit" ||
    normName === "write" ||
    normName === "user_edit" ||
    normName === "user_write" ||
    normName === "doc_to_md"
  ) {
    targetPath = normalizeLogPath(
      typeof toolInput === "string"
        ? toolInput
        : toolInput?.path || toolInput?.file || "",
    );
    if (
      (normName === "edit" ||
        normName === "write" ||
        normName === "user_edit" ||
        normName === "user_write") &&
      !isFailure &&
      targetPath
    ) {
      filesModified = targetPath;
    }
  } else if (
    normName === "bash" ||
    normName === "user_bash" ||
    normName.startsWith("bg_run")
  ) {
    const rawCmd = typeof toolInput === "string"
      ? toolInput
      : toolInput?.command || toolInput?.cmd || "";
    command = sanitizeLogString(rawCmd, 150);
  } else if (
    normName === "search_graph" ||
    normName === "search_code" ||
    normName === "web_search" ||
    normName === "source_check"
  ) {
    query = sanitizeLogString(
      typeof toolInput === "string" ? toolInput : toolInput?.query ||
        toolInput?.name_pattern ||
        toolInput?.name ||
        toolInput?.pattern ||
        "",
    );
  }

  if (isFailure) {
    failureId = `fail_${state.currentTurn || 1}_${Date.now().toString(36)}_${
      Math.random().toString(36).slice(2, 6)
    }`;
    state.lastFailureId = failureId;
    consequence = "FAILURE_RECORDED";
  } else if (state.lastFailureId) {
    recoveryFor = state.lastFailureId;
  }

  logToolActivity(toolName, operation, {
    quest: activeQuest,
    phase,
    path: targetPath,
    command,
    query,
    turn: state.currentTurn,
    correlationId: state.currentTurnCorrelationId,
    filesModified,
    failureId,
    consequence,
    recoveryFor,
    reason: failureReason,
  });
}

export function recordCompactionFailureState(
  sessionState: any,
  errorMsg: string,
): void {
  sessionState.compactionPending = false;
  if (sessionState.activeTransaction) {
    sessionState.activeTransaction.phase = "failed";
    sessionState.activeTransaction.failedAt = Date.now();
    sessionState.activeTransaction.error = errorMsg;
  }
  sessionState.activeCompactionId = null;
  sessionState.archiveCompactionPending = null;
  sessionState.subquestLaunchCompactionPending = false;
  sessionState.preCompactionCheckpointPending = false;
  sessionState.preCompactionSaveRequestPending = false;
}

export function reportCompactionFailure(
  pi: ExtensionAPI,
  c: ExtensionContext | undefined,
  sessionState: any,
  errorMsg: string,
): void {
  logCompactionTransition("COMPACTION_FAILED", "compaction failed", {
    quest: sessionState.active || "",
    reason: errorMsg,
  });

  if (c?.hasUI) {
    c.ui.notify(`Session context compaction failed: ${errorMsg}`, "error");
  }

  reportAgentError(pi, c, `Context compaction failed: ${errorMsg}`, {
    code: QuestErrorCode.COMPACTION_FAILURE,
    requiredNextAction: sessionState.active
      ? `Read ${
        questPath(sessionState.questId) ||
        `.pi/quest/current/${sessionState.questId || "<qid>"}/quest.md`
      } and continue execution. Compaction will be re-attempted when context pressure warrants.`
      : "Review active memory and continue execution.",
    details: { ActiveQuest: sessionState.active || "(none)" },
  });
}

export async function handleCompactionFailureResume(
  pi: ExtensionAPI,
  c: ExtensionContext | undefined,
  sessionState: any,
): Promise<void> {
  if (sessionState.pendingSubquestResume) {
    const subquestStatus = await reconcilePendingSubquestResume(
      sessionState.pendingSubquestResume,
      sessionState,
      pi,
      c,
    );
    if (subquestStatus === "still-valid") {
      const childName = sessionState.pendingSubquestResume;
      dispatchCompactionResume(pi, {
        questName: childName,
        reason: "compaction-failure-fallback",
        ctx: c,
      });
      return;
    } else if (subquestStatus === "inconsistent") {
      return;
    }
  }
  if (sessionState.active) {
    dispatchCompactionResume(pi, {
      questName: sessionState.active,
      reason: "compaction-failure-fallback",
      ctx: c,
    });
  }
}

export async function handleCompactionFailure(
  pi: ExtensionAPI,
  ctx: ExtensionContext,
  event: any,
): Promise<void> {
  const c = getActiveContext(ctx);
  const targetSessionId = getSessionId(c);
  const sessionState = sessionStates.get(targetSessionId) ?? getState(c);
  if (!sessionState.active) {
    sessionState.activeTransaction = null;
    sessionState.activeCompactionId = null;
    sessionState.pendingResume = null;
    sessionState.archiveCompactionPending = null;
    sessionState.compactionPending = false;
    return;
  }
  const errorMsg = event?.error?.message ||
    event?.message ||
    "Session context compaction failed. Context has not been compacted.";

  recordCompactionFailureState(sessionState, errorMsg);
  reportCompactionFailure(pi, c, sessionState, errorMsg);
  await handleCompactionFailureResume(pi, c, sessionState);
}
