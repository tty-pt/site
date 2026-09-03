import { createHash } from "node:crypto";
import {
  copyFileSync,
  existsSync,
  mkdirSync,
  readFileSync,
  unlinkSync,
} from "node:fs";
import { copyFile, mkdir, readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { FUTURE_DIR } from "../../constants.ts";
import {
  canImplement,
  getLifecycleState,
  syncImplementationPermission,
} from "../../gates.ts";
import { populateEpistemicUpdates } from "../update_populators.ts";
import { logError, reportAgentError } from "../../messaging.ts";
import {
  logEvent,
  logQuestTransition,
  logStateUpdateTransition,
  tryLog,
} from "../../logging.ts";
import {
  getActiveContext,
  getSessionId,
  sessionStartMap,
} from "../../state.ts";
import { isSemanticSummaryEnabled } from "../../config.ts";
import {
  ensureQuestIdInContent,
  parseMarkdownSections,
  QUEST_TEMPLATE,
  spliceMarkdownSections,
} from "../../markdown.ts";
import {
  fileExists,
  listActiveQuestRecords,
  questDirPath,
  questPath,
  resolveQuestRecordBySlug,
  slugify,
} from "../../paths.ts";
import { syncMetaJson } from "../../markdown/template/metadata.ts";
import { persist, verifyAndMarkSaved } from "../../persistence.ts";
import { ensureQuestId, getState, isRootQuest, state } from "../../state.ts";
import { checkAndEmitGateContinuationSteer } from "../../tool_gating.ts";
import { QuestErrorCode } from "../../types.ts";
import type {
  ExtensionAPI,
  ExtensionContext,
  StoredState,
} from "../../types.ts";
import { withReviewFileLock } from "../../utils/mutex.ts";
import { handleReassessmentCompletion } from "./reassessment.ts";
import {
  applyEpistemicMetadataToUpdates,
  handleResearchCompletion,
} from "./research.ts";
import {
  capturePostUpdatePlanningSnapshot,
  capturePreUpdatePlanningSnapshot,
} from "./planning.ts";
import {
  checkAndTriggerDirectionReview,
  getCustomSubagentRunner,
  isDraftReviewValid,
  isPlanReviewValidForState,
  isSubagentToolRegistered,
  requestPlanReview,
} from "../../critical_agent.ts";
import {
  formatMarkSavedResponse,
  formatUpdateStateResponse,
} from "../formatting.ts";

async function resolveMarkTargetName(params: any): Promise<string | null> {
  const direct = params?.name
    ? slugify(params.name)
    : params?.questName
    ? slugify(params.questName)
    : state.active;
  if (direct) return direct;
  const records = await listActiveQuestRecords();
  if (records.length >= 1) {
    return records[0].name;
  }
  return null;
}

export async function executeMarkTool(
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
) {
  const targetName = await resolveMarkTargetName(params);
  if (!targetName) {
    return {
      content: [{
        type: "text",
        text:
          "Error: No active quest is set. Pass the quest name or use quest_update_state({ name: '...' }).",
      }],
      details: { error: "no_active_quest", success: false },
    };
  }
  if (!state.active) {
    state.active = targetName;
    if (!Array.isArray(state.stack)) state.stack = [targetName];
    else if (!state.stack.includes(targetName)) state.stack.push(targetName);
    persist(pi, ctx);
  }
  const res = await verifyAndMarkSaved(pi, ctx, targetName);
  if (!res.success) {
    return {
      content: [{ type: "text", text: `Error: ${res.error}` }],
      details: {
        error: "file_missing_or_unreadable",
        path: questPath(targetName),
        success: false,
      },
    };
  }
  return formatMarkSavedResponse(targetName, res);
}

function syncQuestIdentity(
  targetName: string,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): void {
  const originalReq = state.pendingRootRequest ||
    (state.prompts && state.prompts.length > 0 ? state.prompts[0] : "");
  const isNewIdentity = !state.active || state.pendingRootQuest ||
    targetName !== state.active;
  if (isNewIdentity) {
    state.active = targetName;
    state.pendingRootQuest = false;
    state.questIdentityEstablished = true;
    state.pendingRootRequest = null;
    if (
      !Array.isArray(state.stack) || state.stack.length === 0 ||
      !state.stack.includes(targetName)
    ) {
      state.stack = [targetName];
    }
    if (originalReq && (!state.prompts || state.prompts.length === 0)) {
      state.prompts = [originalReq];
    }
    persist(pi, ctx);
  }
  // Draft is considered realized once quest identity is established via quest_update_state
  if (state.activeDraft) {
    const draftSlug = state.activeDraft;
    state.activeDraft = null;
    state.draftPrompts = [];
    state.draftCreatedAt = null;
    state.draftLastSavedHash = null;
    persist(pi, ctx);
    try {
      const p = `${FUTURE_DIR}/${draftSlug}.md`;
      if (existsSync(p)) {
        let content: string | null = null;
        try {
          content = readFileSync(p, "utf8");
        } catch {}
        const hash = createHash("sha256").update(content || draftSlug).digest(
          "hex",
        ).slice(0, 12);
        // D null-qid edge: guard before building archDir (questDirPath(null)->"" yields ./future-archive)
        const qidForArch = state.questId as string | null;
        if (qidForArch) {
          const archDir = `.pi/quest/current/${qidForArch}/future-archive`;
          try {
            if (!existsSync(archDir)) mkdirSync(archDir, { recursive: true });
          } catch {}
          try {
            const dest = join(archDir, `${draftSlug}.md`);
            try {
              copyFileSync(p, dest);
            } catch {
              // fallback to async copyFile if sync fails (fire-and-forget)
              try {
                copyFile(p, dest).catch(() => {});
              } catch {}
            }
          } catch {}
        }
        tryLog("DRAFT_DISCARDED", `draft discarded ${draftSlug}`, {
          quest: state.active || draftSlug,
          slug: draftSlug,
          hash,
          reviewId: state.awaitingReview?.reviewId,
          boundaryKey: state.lastPlanReviewBoundaryKey ?? undefined,
          questId: state.questId || undefined,
          reason: "quest_update_state",
        });
      }
    } catch {}
    try {
      const p = `${FUTURE_DIR}/${draftSlug}.md`;
      if (existsSync(p)) unlinkSync(p);
    } catch {}
  }
}

export function resolveUpdateTarget(
  params: any,
): { targetName: string } | { errorResponse: any } {
  const rawName = (params?.name || params?.questName || "").trim();
  // Fallback to activeDraft slug when drafting and providesPlan (P1) — avoids no_active_quest extra turn
  const fallbackDraft = state.activeDraft as string | null;
  const providesPlan = Boolean(
    params?.plan || params?.findings || params?.understanding ||
      params?.assumptions || params?.goal || params?.planConfidence,
  );
  const fallback = providesPlan && fallbackDraft ? fallbackDraft : "";
  const targetName = slugify(rawName || state.active || fallback || "");
  if (!targetName) {
    return {
      errorResponse: {
        content: [
          {
            type: "text",
            text:
              "Error: No active quest to update and no quest name provided. Please specify a concise semantic quest name (e.g. name: 'persistent-agent-research').",
          },
        ],
        details: { error: "no_active_quest" },
      },
    };
  }
  return { targetName };
}

export async function ensureQuestFileExists(
  targetName: string,
  goal = "",
): Promise<string> {
  const rec = await resolveQuestRecordBySlug(targetName);
  if (rec) {
    return rec.path;
  }
  const qId = state.questId || ensureQuestId();
  await mkdir(questDirPath(qId), { recursive: true });
  let path = questPath(qId);
  if (!isRootQuest(state)) {
    const childPath = join(questDirPath(qId), `${slugify(targetName)}.md`);
    if (
      await fileExists(childPath) || state.active === targetName ||
      (Array.isArray(state.stack) && state.stack.includes(targetName) &&
        state.stack[0] !== targetName)
    ) {
      path = childPath;
    }
  }
  const originalReq = state.pendingRootRequest ||
    (state.prompts && state.prompts.length > 0 ? state.prompts[0] : "");
  if (!(await fileExists(path))) {
    await writeFile(
      path,
      QUEST_TEMPLATE(
        targetName,
        goal,
        "",
        originalReq,
        state.refinements || [],
        qId,
      ),
      "utf8",
    );
    if (path.endsWith("quest.md")) {
      try {
        await syncMetaJson(qId || "", state);
      } catch {}
    }
  }
  return path;
}

export function capturePreUpdateGateSnapshot(ctx: ExtensionContext, s = state) {
  return {
    wasImplementable: canImplement(s, ctx),
    wasReassessmentPending: !!s.reassessmentRequired,
    wasResearchPending: !!s.researchRequired || !s.researchComplete,
    wasAwaitingConfirmation: !!s.awaitingUserConfirmation,
  };
}

export function constructUpdatedMarkdown(
  content: string,
  params: any,
  targetName: string,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): {
  updatedMarkdown: string;
  researchTransitionNote: string;
  reassessmentTransitionNote: string;
} {
  const targetState = getState(ctx);
  const existingSections = parseMarkdownSections(content);
  const updates = new Map<string, string>();

  populateEpistemicUpdates(params, updates, targetState);

  let reassessmentTransitionNote = "";
  if (params?.reassessmentComplete === true) {
    reassessmentTransitionNote = handleReassessmentCompletion(
      params,
      content,
      updates,
      existingSections,
      targetName,
      pi,
      ctx,
    );
  }

  const researchTransitionNote = handleResearchCompletion(
    params,
    content,
    updates,
    targetName,
    pi,
    ctx,
  );

  applyEpistemicMetadataToUpdates(updates, targetState);

  let updatedMarkdown = spliceMarkdownSections(content, updates);
  if (targetState.questId) {
    updatedMarkdown = ensureQuestIdInContent(
      updatedMarkdown,
      targetState.questId,
    );
  }
  return {
    updatedMarkdown,
    researchTransitionNote,
    reassessmentTransitionNote,
  };
}

export function notifyGateTransitions(
  targetName: string,
  preSnapshot: ReturnType<typeof capturePreUpdateGateSnapshot>,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): void {
  const isImplementable = canImplement(state, ctx);
  if (!preSnapshot.wasImplementable && isImplementable) {
    checkAndEmitGateContinuationSteer(pi, ctx, targetName, {
      wasReassessmentPending: preSnapshot.wasReassessmentPending,
      wasResearchPending: preSnapshot.wasResearchPending,
      wasAwaitingConfirmation: preSnapshot.wasAwaitingConfirmation,
    });
  }
}

async function maybeTriggerPlanReview(
  prePlanning: ReturnType<typeof capturePreUpdatePlanningSnapshot>,
  postPlanning: ReturnType<typeof capturePostUpdatePlanningSnapshot>,
  targetState: StoredState,
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): Promise<string> {
  const hasReviewCapability = isSubagentToolRegistered(pi, ctx) ||
    Boolean(getCustomSubagentRunner());
  if (
    !isRootQuest(targetState) || !hasReviewCapability ||
    !postPlanning.hasActionablePlan
  ) return "";

  const isMaterialPlanChange =
    prePlanning.boundaryKey !== postPlanning.boundaryKey;
  const isInitialResearchCompletion = (params?.researchComplete === true ||
    (!prePlanning.researchComplete && targetState.researchComplete)) &&
    (!targetState.lastPlanReviewApproval &&
      !targetState.lastPlanReviewBoundaryKey);
  const isFirstPlanDraft = !prePlanning.hasActionablePlan &&
    postPlanning.hasActionablePlan &&
    (!targetState.lastPlanReviewApproval &&
      !targetState.lastPlanReviewBoundaryKey);

  const priorReviewRejected = targetState.lastCriticalReview &&
    (targetState.lastCriticalReview.verdict === "REVISE" ||
      targetState.lastCriticalReview.verdict === "FAIL" ||
      targetState.lastCriticalReview.verdict === "UNCERTAIN");
  const isRevisionAfterRejection = priorReviewRejected && isMaterialPlanChange;

  const alreadyApprovedForBoundary = isPlanReviewValidForState(targetState) &&
    targetState.lastPlanReviewBoundaryKey === postPlanning.boundaryKey;
  const alreadyRequestedForBoundary =
    targetState.lastPlanReviewBoundaryKey === postPlanning.boundaryKey;
  const firstPlanReviewFired = !!targetState.firstPlanReviewFired ||
    !!targetState.lastPlanReviewApproval;

  const shouldTriggerReview = !alreadyApprovedForBoundary &&
    !alreadyRequestedForBoundary &&
    (isInitialResearchCompletion || isFirstPlanDraft || isMaterialPlanChange ||
      isRevisionAfterRejection);

  if (!shouldTriggerReview) {
    if (firstPlanReviewFired && isMaterialPlanChange) {
      tryLog(
        "PLAN_REVIEW_SUPPRESSED_MATERIAL_CHANGE",
        `plan review suppressed material change`,
        {
          quest: targetState.active || "",
          questId: targetState.questId || undefined,
          preBoundaryKey: prePlanning.boundaryKey?.slice(0, 8),
          postBoundaryKey: postPlanning.boundaryKey?.slice(0, 8),
          boundaryKey: postPlanning.boundaryKey,
          planVersion: targetState.planVersion,
        },
      );
    } else if (alreadyRequestedForBoundary || alreadyApprovedForBoundary) {
      tryLog(
        "FIRST_PLAN_REVIEW_ALREADY_FIRED",
        `first plan review already fired`,
        {
          quest: targetState.active || "",
          shard: "root",
          boundaryKey: postPlanning.boundaryKey?.slice(0, 8),
        },
      );
    }
    return "";
  }

  targetState.lastPlanReviewBoundaryKey = postPlanning.boundaryKey;
  try {
    const qp = questPath(targetState.questId || state.questId);
    const cc = await readFile(qp, "utf8");
    if (!cc.includes(`[[review-v${targetState.planVersion}]]`)) {
      const e = `- [ ] [[review-v${targetState.planVersion}]] - review ${
        postPlanning.boundaryKey.slice(0, 8)
      }`;
      await writeFile(
        qp,
        cc.includes("## Sub-Quests")
          ? cc.replace(/(##\s*Sub-Quests[\s\S]*?)(\n##\s+|$)/i, `$1${e}\n$2`)
          : `${cc.trimEnd()}\n\n## Sub-Quests\n${e}\n`,
        "utf8",
      );
    }
  } catch {}
  const planRevRes = await requestPlanReview(
    pi,
    ctx,
    targetState.active || "",
    { boundaryKey: postPlanning.boundaryKey },
  );
  let planReviewNote = "";
  if (
    planRevRes?.review?.verdict === "APPROVE" ||
    planRevRes?.review?.verdict === "PASS"
  ) {
    targetState.researchComplete = true;
    targetState.researchRequired = false;
    const isAlreadyConfirmed = Array.isArray(targetState.confirmedQuests) &&
      targetState.confirmedQuests.includes(targetState.active || "");
    targetState.awaitingUserConfirmation = isRootQuest(targetState) &&
      !isAlreadyConfirmed;
    planReviewNote = " [Adversarial Plan Review: APPROVED]";
  } else if (planRevRes?.review?.verdict) {
    targetState.researchComplete = false;
    targetState.researchRequired = true;
    targetState.awaitingUserConfirmation = false;
    planReviewNote =
      ` [Adversarial Plan Review: ${planRevRes.review.verdict} - Findings delivered to main agent]`;
  } else if (
    planRevRes?.error && !planRevRes.inProgress && !planRevRes.skipped
  ) {
    targetState.researchComplete = false;
    targetState.researchRequired = true;
    targetState.awaitingUserConfirmation = false;
    planReviewNote = ` [Adversarial Plan Review: ERROR (${planRevRes.error})]`;
  }
  tryLog(
    "REQUIRE_CONFIRM_DECISION",
    `requireConfirm=${!!targetState.awaitingUserConfirmation}`,
    {
      quest: targetState.active || "",
      requireConfirm: !!targetState.awaitingUserConfirmation,
      boundaryKey: postPlanning.boundaryKey,
      planVersion: targetState.planVersion,
      verdict: planRevRes?.review?.verdict || planRevRes?.error || "none",
    },
  );
  syncImplementationPermission(targetState, ctx);
  persist(pi, ctx);
  return planReviewNote;
}

function logStateUpdate(
  params: any,
  targetName: string,
  saveRes: any,
  targetState: StoredState,
): void {
  const completedCount = Array.isArray(params.completed)
    ? params.completed.length
    : (params.completed ? 1 : undefined);
  const remainingCount = Array.isArray(params.remaining)
    ? params.remaining.length
    : (params.remaining ? 1 : undefined);
  const filesMod = Array.isArray(params.filesModified || params.filesTouched)
    ? (params.filesModified || params.filesTouched).join(",")
    : (typeof (params.filesModified || params.filesTouched) === "string"
      ? (params.filesModified || params.filesTouched)
      : undefined);

  logStateUpdateTransition(
    "STATE_UPDATE_ACCEPTED",
    `state updated for ${targetName}`,
    {
      quest: targetName,
      gen: saveRes.count,
      status: params?.status,
      planVersion: targetState.planVersion || 1,
      consequence: "STATE_UPDATED",
      recoveryFor: targetState.lastFailureId || undefined,
      filesModified: filesMod,
      completedTasks: completedCount,
      remainingTasks: remainingCount,
      testStatus: typeof params.testStatus === "string"
        ? params.testStatus
        : undefined,
      activeGate: targetState.reassessmentRequired
        ? "REASSESSMENT_PENDING"
        : (targetState.researchRequired || !targetState.researchComplete
          ? "RESEARCH_PENDING"
          : (targetState.awaitingUserConfirmation
            ? "CONFIRMATION_PENDING"
            : "IMPLEMENTATION_ALLOWED")),
    },
  );
}

async function maybeTriggerDirectionReview(
  targetState: StoredState,
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): Promise<void> {
  if (
    targetState.researchComplete && !targetState.reassessmentRequired &&
    !targetState.researchRequired && !targetState.awaitingUserConfirmation
  ) {
    const hasMajorMilestone = params?.reassessmentComplete === true ||
      Boolean(params?.planRevisions || params?.revisions) ||
      (Array.isArray(params?.decisions) && params.decisions.length > 0) ||
      Boolean(params?.completed);

    if (hasMajorMilestone) {
      const reason = params?.reassessmentComplete === true
        ? "reassessment_resolved"
        : params?.planRevisions || params?.revisions
        ? "plan_revision"
        : Array.isArray(params?.decisions) && params.decisions.length > 0
        ? "architectural_decisions"
        : "phase_completed";
      await checkAndTriggerDirectionReview(pi, ctx, reason);
    }
  }
}

export async function executeUpdateStateTool(
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
) {
  const targetResolution = resolveUpdateTarget(params);
  if ("errorResponse" in targetResolution) {
    return targetResolution.errorResponse;
  }
  const { targetName } = targetResolution;

  // #36 gate: quest_update_state must not realize activeDraft without reviewer APPROVE; 51: while REASSESSMENT_PENDING gateBlocked not TOOL_FAILURE; 54: enrich details
  // Recommendation A: allow first quest_update_state that provides plan/findings to create current/quest.md (draft → durable), review then gates implementation, not writing
  if (state.activeDraft) {
    const hasReviewer = Boolean(getCustomSubagentRunner()) ||
      isSubagentToolRegistered(pi, ctx);
    const isValid = (() => {
      try {
        return isDraftReviewValid(state);
      } catch {
        return false;
      }
    })();
    if (hasReviewer && !isValid) {
      const providesPlan = Boolean(
        params?.plan || params?.findings || params?.understanding ||
          params?.assumptions || params?.goal || params?.planConfidence,
      );
      const evidence = state.currentReceipt?.evidenceCount || 0;
      const allowsInitialDraftWrite = providesPlan && evidence >= 5;
      if (allowsInitialDraftWrite) {
        tryLog(
          "DRAFT_PROMOTION_ALLOWED",
          `draft promotion allowed despite review pending (initial write)`,
          { quest: state.activeDraft, evidence, providesPlan },
        );
      } else {
        const draftSlug = state.activeDraft;
        let hash = "unknown";
        try {
          const c = readFileSync(`${FUTURE_DIR}/${draftSlug}.md`, "utf8");
          hash = createHash("sha256").update(c).digest("hex").slice(0, 12);
        } catch {
          try {
            hash = createHash("sha256").update(draftSlug).digest("hex").slice(
              0,
              12,
            );
          } catch {}
        }
        const boundaryKey = `draft:${draftSlug}:${hash}`;
        const dpLen = state.draftPrompts?.length || 0;
        tryLog("REVIEW_DEDUP_HIT", `draft not yet reviewer-approved`, {
          quest: draftSlug,
          shard: "draft",
          reason: "draft_not_approved",
          hash,
          boundaryKey,
          dpLen,
          evidence,
          hasReviewer,
          isDraftReviewValid: isValid,
          reassessmentRequired: Boolean(state.reassessmentRequired),
        });
        const msg =
          `Draft '${draftSlug}' not yet reviewer-approved — present plan via future/${draftSlug}.md and await plan_review APPROVE (boundaryKey ${boundaryKey}) before promoting. Accumulate requirements or wait for auto-review; only promotion via 'go' after APPROVE may realize the draft.`;
        if (state.reassessmentRequired) {
          try {
            const { sendInternalAgentMessage } = await import(
              "../../messaging.ts"
            );
            sendInternalAgentMessage(
              pi,
              `⚠️ ${msg} — awaiting coalesced plan_review, do not retry quest_update_state.`,
              "steer",
            );
          } catch {}
          return {
            content: [{ type: "text", text: msg }],
            details: {
              error: "draft_not_approved",
              success: false,
              shard: "draft",
              boundaryKey,
              hash,
              quest: draftSlug,
              gateBlocked: true,
              code: "REVIEW_COALESCENCE_PENDING",
              dpLen,
              evidence,
            },
          };
        }
        // Agent-visible steer (AGENTS.md invariant 1) + tool result (both model-visible)
        try {
          const { sendInternalAgentMessage } = await import(
            "../../messaging.ts"
          );
          sendInternalAgentMessage(pi, `⚠️ ${msg}`, "steer");
        } catch {}
        return {
          content: [{ type: "text", text: msg }],
          details: {
            error: "draft_not_approved",
            success: false,
            shard: "draft",
            boundaryKey,
            hash,
            quest: draftSlug,
            dpLen,
            evidence,
          },
        };
      }
    }
  }

  syncQuestIdentity(targetName, pi, ctx);
  const path = await ensureQuestFileExists(targetName, params?.goal || "");

  try {
    const content = await readFile(path, "utf8");
    const preSnapshot = capturePreUpdateGateSnapshot(ctx);
    const targetState = getState(ctx);
    const prePlanning = capturePreUpdatePlanningSnapshot(
      targetName,
      content,
      targetState,
    );

    const {
      updatedMarkdown,
      researchTransitionNote,
      reassessmentTransitionNote,
    } = constructUpdatedMarkdown(
      content,
      params,
      targetName,
      pi,
      ctx,
    );

    // review+save: file lock for durable write+verify (P2) — re-entrant with persistence lock
    let saveRes: any;
    let postPlanning: ReturnType<typeof capturePostUpdatePlanningSnapshot>;
    await withReviewFileLock(
      targetState.questId || state.questId || targetName,
      async () => {
        await writeFile(path, updatedMarkdown, "utf8");
        if (path.endsWith("quest.md")) {
          await syncMetaJson(
            (targetState.questId || state.questId || "") as string,
            targetState,
          );
        }
        saveRes = await verifyAndMarkSaved(pi, ctx, targetName);
        persist(pi, ctx);
        postPlanning = capturePostUpdatePlanningSnapshot(
          targetName,
          updatedMarkdown,
          targetState,
        );
      },
    );
    // fallback if lock wrapper didn't set (should not happen)
    if (!postPlanning!) {
      postPlanning = capturePostUpdatePlanningSnapshot(
        targetName,
        updatedMarkdown,
        targetState,
      );
    }

    if (prePlanning.boundaryKey !== postPlanning.boundaryKey) {
      const activeGate = targetState.awaitingUserConfirmation
        ? "CONFIRMATION_PENDING"
        : targetState.reassessmentRequired
        ? "REASSESSMENT_PENDING"
        : targetState.researchRequired || !targetState.researchComplete
        ? "RESEARCH_PENDING"
        : "IMPLEMENTATION_ALLOWED";
      const sessId = getSessionId(getActiveContext(ctx));
      const startMs = sessionStartMap.get(sessId) || Date.now();
      tryLog(
        "SEMANTIC_SNAPSHOT",
        `${prePlanning.boundaryKey}→${postPlanning.boundaryKey}`,
        {
          quest: targetName,
          from: prePlanning.boundaryKey?.slice(0, 8),
          to: postPlanning.boundaryKey?.slice(0, 8),
          planVersion: targetState.planVersion,
          activeGate,
          elapsedMs: Date.now() - startMs,
          opencodeSessionId: sessId,
        },
      );
    }
    if (isSemanticSummaryEnabled(targetState)) {
      if (
        prePlanning.boundaryKey !== postPlanning.boundaryKey ||
        prePlanning.researchComplete !== targetState.researchComplete
      ) {
        const curGate = targetState.reassessmentRequired
          ? "REASSESSMENT_PENDING"
          : targetState.researchRequired || !targetState.researchComplete
          ? "RESEARCH_PENDING"
          : targetState.awaitingUserConfirmation
          ? "CONFIRMATION_PENDING"
          : "IMPLEMENTATION_ALLOWED";
        const lifecycle = getLifecycleState(targetState, undefined);
        const intentMap: Record<string, string> = {
          RESEARCH_PENDING: "research",
          ACTIVE_DIRTY: "plan-draft",
          AWAITING_REVIEW: "awaiting-review",
          REASSESSMENT_PENDING: "revising",
          IMPLEMENTATION_ALLOWED: "implementing",
          ACTIVE_CLEAN: "verifying",
        };
        const intent = intentMap[lifecycle as string] || "research";
        const line = `${intent} planVersion${
          targetState.planVersion || 1
        } gate=${curGate} prompts=${targetState.prompts?.length || 0} draft=${
          targetState.draftPrompts?.length || 0
        }`;
        tryLog("STEP_SUMMARY", line.slice(0, 120), {
          quest: targetName,
          intent,
          planVersion: targetState.planVersion || 1,
          activeGate: curGate,
          elapsedMs: Date.now() -
            (sessionStartMap.get(getSessionId(getActiveContext(ctx))) ||
              Date.now()),
          opencodeSessionId: getSessionId(getActiveContext(ctx)),
        });
      }
    }

    const planReviewNote = await maybeTriggerPlanReview(
      prePlanning,
      postPlanning,
      targetState,
      params,
      pi,
      ctx,
    );

    logStateUpdate(params, targetName, saveRes, targetState);
    notifyGateTransitions(targetName, preSnapshot, pi, ctx);
    await maybeTriggerDirectionReview(targetState, params, pi, ctx);

    return formatUpdateStateResponse(
      targetName,
      path,
      params,
      saveRes,
      state.planVersion || 1,
      `${researchTransitionNote}${planReviewNote}`,
      reassessmentTransitionNote,
      state.reassessmentRequired,
      state.researchComplete,
    );
  } catch (err: any) {
    logError(
      `Failed to update quest state at ${path}`,
      err,
      ctx,
      QuestErrorCode.PERSISTENCE_FAILURE,
    );
    reportAgentError(
      pi,
      ctx,
      `Failed to update quest state at ${path}: ${err?.message || err}`,
      {
        code: QuestErrorCode.STATE_RECONSTRUCTION_FAILURE,
        requiredNextAction:
          "Inspect file permissions and ensure the quest file markdown contains valid section headers.",
        details: { Quest: targetName, Path: path },
      },
    );
    return {
      content: [{
        type: "text",
        text: `Error updating quest state: ${err?.message || err}`,
      }],
      details: { error: "update_failed", message: String(err) },
    };
  }
}
