import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { getSubquestCompactThreshold } from "../compaction.ts";
import { calculateCurrentTokens } from "../context.ts";
import { logEvent, logSubquestTransition, tryLog } from "../logging.ts";
import { QUEST_TEMPLATE } from "../markdown.ts";
import { sendInternalAgentMessage } from "../messaging.ts";
import { fileExists, questDirPath, questPath, slugify } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadExistingQuestEpistemicState } from "../reconstruction.ts";
import { generateQuestId, state } from "../state.ts";
import {
  applyLoadedSubquestState,
  buildSubquestProtocolInstructions,
  linkSubQuestInParent,
  pushSubquestToStack,
} from "../subquest.ts";
import { syncImplementationPermission } from "../gates.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";
import { updateUIStatus } from "../ui.ts";
import { formatSubquestResponse } from "./formatting.ts";
import { validateSubquestParams } from "./validation.ts";
import { isDraftReviewValid } from "../critical_agent/policy.ts";
import { isAutonomousSubquestDuringDraftingEnabled } from "../config.ts";
import {
  getCustomSubagentRunner,
  isSubagentToolRegistered,
} from "../critical_agent/index.ts";

export async function ensureSubquestFileExists(
  name: string,
  goal: string,
  parentName: string,
): Promise<{ path: string; isExisting: boolean }> {
  const qid = state.questId || generateQuestId();
  state.questId = qid;
  await mkdir(questDirPath(qid), { recursive: true });
  const childPath = join(questDirPath(qid), `${slugify(name)}.md`);
  let isExisting = false;
  if (await fileExists(childPath)) {
    isExisting = true;
  } else {
    await writeFile(
      childPath,
      QUEST_TEMPLATE(name, goal, parentName, "", [], qid),
      "utf8",
    );
  }
  return { path: childPath, isExisting };
}

export function handleSubquestLaunchCompactionOrPrompt(
  name: string,
  goal: string,
  parentName: string,
  path: string,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): void {
  const subLaunchThreshold = getSubquestCompactThreshold();
  const tokens = calculateCurrentTokens(ctx);

  if (
    subLaunchThreshold > 0 && tokens !== null && tokens >= subLaunchThreshold
  ) {
    state.subquestLaunchCompactionPending = true;
    state.pendingSubquestResume = name;
    logSubquestTransition(
      "SUBQUEST_START",
      `subquest launch with compaction: ${name}`,
      {
        quest: parentName,
        subquest: name,
        child: name,
        parent: parentName,
      },
    );
  } else {
    state.pendingSubquestResume = null;
    const enterMsg = buildSubquestProtocolInstructions(
      name,
      goal,
      parentName,
      path,
    );
    sendInternalAgentMessage(pi, enterMsg, "followUp");
  }
}

export async function executeSubquestTool(
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
) {
  // AQM: subquest blocked during drafting by default (off), configurable on
  if (!isAutonomousSubquestDuringDraftingEnabled(state)) {
    const hasReviewer = Boolean(getCustomSubagentRunner()) ||
      isSubagentToolRegistered(pi, ctx);
    const pendingDraft = Boolean(state.pendingRootQuest) ||
      Boolean(state.activeDraft);
    // Only block if draft is relevant to current parent/active (avoid stale draft from previous test/session pollution)
    const draftSlug = state.activeDraft || null;
    const activeSlug = state.active || null;
    // Stale draft check: if activeDraft exists but active is different and not pendingRootQuest, treat as stale
    const isRelevantDraft = !draftSlug || draftSlug === activeSlug ||
      Boolean(state.pendingRootQuest);
    const isDraftPending = pendingDraft &&
      (isRelevantDraft || Boolean(state.pendingRootQuest));
    const isDraftNotApproved = Boolean(draftSlug) && hasReviewer &&
      !isDraftReviewValid(state) && isDraftPending;
    const isResearchPendingDraft = Boolean(state.researchRequired) &&
      (state.planVersion || 1) === 1 && isDraftPending;
    if (isDraftPending && (isDraftNotApproved || isResearchPendingDraft)) {
      const slug = state.activeDraft || state.pendingRootRequest || "draft";
      tryLog("REVIEW_DEDUP_HIT", `subquest blocked while drafting`, {
        quest: slug,
        shard: "draft",
        reason: "subquest_while_draft_pending",
        hasReviewer,
        isDraftReviewValid: !isDraftNotApproved,
        autonomousFlag: false,
      });
      try {
        sendInternalAgentMessage(
          pi,
          `⚠️ Subquest '${
            params?.name || params?.goal || slug
          }' blocked — Autonomous Quest Management during drafting is off by default. Finish draft via quest_update_state and obtain plan_review APPROVE before creating subquests. Enable via PI_QUEST_AQM_SUBQUEST_DRAFT=1 or settings pi-quest.aqm.subquestDuringDrafting.enabled true to allow autonomous subquests while drafting.`,
          "steer",
        );
      } catch {}
      return {
        content: [{
          type: "text",
          text:
            `Subquest blocked while drafting: Autonomous Quest Management during drafting is off by default. Complete the draft via quest_update_state and await draft review APPROVE before creating subquests. Set PI_QUEST_AQM_SUBQUEST_DRAFT=1 to enable.`,
        }],
        details: {
          error: "subquest_while_draft_pending",
          shard: "draft",
          autonomousFlag: false,
          blocked: true,
        },
      };
    }
  }

  const parsed = validateSubquestParams(params, pi, ctx);
  if (!parsed) {
    const errorKey = !params?.name && !params?.goal
      ? "missing_name"
      : "missing_goal";
    logSubquestTransition(
      "SUBQUEST_FAILED",
      `subquest creation failed: ${errorKey}`,
      {
        reason: errorKey,
      },
    );
    return {
      content: [{
        type: "text",
        text: `Error: Sub-quest ${
          errorKey === "missing_name"
            ? "name or goal description"
            : "goal description"
        } is required.`,
      }],
      details: { error: errorKey },
    };
  }

  const { name, goal, parentName, switchNow } = parsed;
  const { path, isExisting } = await ensureSubquestFileExists(
    name,
    goal,
    parentName,
  );

  if (parentName) {
    await linkSubQuestInParent(parentName, name, goal, ctx);
    await verifyAndMarkSaved(pi, ctx, parentName);
  }

  if (switchNow) {
    pushSubquestToStack(state, parentName, name);
    const subLoaded = await loadExistingQuestEpistemicState(
      state.questId || name,
    );
    applyLoadedSubquestState(state, goal, false, subLoaded);
    syncImplementationPermission(state);

    await verifyAndMarkSaved(pi, ctx, name);
    handleSubquestLaunchCompactionOrPrompt(
      name,
      goal,
      parentName,
      path,
      pi,
      ctx,
    );
    logSubquestTransition("SUBQUEST_SWITCH", `switched to subquest '${name}'`, {
      quest: name,
      subquest: name,
      child: name,
      parent: parentName,
    });
    persist(pi, ctx);
    updateUIStatus(ctx);
  } else {
    logSubquestTransition("SUBQUEST_START", `subquest created: ${name}`, {
      quest: parentName,
      subquest: name,
      child: name,
      parent: parentName,
      switchNow: false,
    });
  }

  if (ctx.hasUI) {
    const msg = isExisting
      ? `Sub-quest '${name}' already exists at \`${path}\`.${
        parentName ? ` Verified link in parent '${parentName}'.` : ""
      }${switchNow ? " Switched active quest to this sub-quest." : ""}`
      : `Created sub-quest **${name}** at \`${path}\`${
        parentName ? ` (parent: **${parentName}**)` : ""
      }.${
        switchNow
          ? " Switched active quest to this sub-quest."
          : " Kept parent quest active; sub-quest added to tracker."
      }`;
    ctx.ui.notify(msg, "info");
  }

  return formatSubquestResponse(name, path, parentName, isExisting, switchNow);
}
