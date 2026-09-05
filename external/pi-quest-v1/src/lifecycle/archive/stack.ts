import { syncImplementationPermission } from "../../gates.ts";
import { persist, verifyAndMarkSaved } from "../../persistence.ts";
import {
  fileExists,
  questPath,
  resolveQuestRecordBySlug,
} from "../../paths.ts";
import { loadExistingQuestEpistemicState } from "../../reconstruction.ts";
import { startResearchEpoch } from "../../research.ts";
import { getState, state } from "../../state.ts";
import { logSubquestTransition } from "../../logging.ts";
import { markSubQuestCompletedInParent } from "../../subquest.ts";
import type { ExtensionAPI, ExtensionContext } from "../../types.ts";

export async function popArchivedAndFindNextActive(
  stack: string[],
  archivedName: string,
  parentSlug: string | null,
  s: any,
): Promise<{ nextActive: string | null; stack: string[] }> {
  const idx = stack.lastIndexOf(archivedName);
  if (idx >= 0) stack.splice(idx, 1);

  let nextActive: string | null = null;
  while (stack.length > 0) {
    const candidate = stack[stack.length - 1];
    const candRecord = await resolveQuestRecordBySlug(candidate);
    if (candRecord && (await fileExists(candRecord.path))) {
      nextActive = candidate;
      s.questId = candRecord.qid;
      break;
    } else if (await fileExists(questPath(candidate))) {
      nextActive = candidate;
      s.questId = candidate;
      break;
    }
    stack.pop();
  }

  if (!nextActive && parentSlug) {
    const parentRecord = await resolveQuestRecordBySlug(parentSlug);
    if (parentRecord && (await fileExists(parentRecord.path))) {
      nextActive = parentSlug;
      s.questId = parentRecord.qid;
      stack.push(parentSlug);
    } else if (await fileExists(questPath(parentSlug))) {
      nextActive = parentSlug;
      s.questId = parentSlug;
      stack.push(parentSlug);
    }
  }

  return { nextActive, stack };
}

export async function hydrateNextActive(
  nextActive: string | null,
  stack: string[],
  s: any,
  archivedName: string,
  parentSlug: string | null,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): Promise<void> {
  if (parentSlug) {
    await markSubQuestCompletedInParent(parentSlug, archivedName, ctx);
  }

  if (s.active === archivedName || state.active === archivedName) {
    s.active = nextActive;
    s.stack = stack;
    state.active = nextActive;
    state.stack = stack;
    if (nextActive) {
      const parentLoaded = await loadExistingQuestEpistemicState(
        s.questId || nextActive,
      );
      s.prompts = parentLoaded.originalRequest
        ? [parentLoaded.originalRequest]
        : [];
      s.refinements = parentLoaded.refinements;
      s.researchRound = parentLoaded.researchRound;
      s.researchComplete = parentLoaded.researchComplete;
      s.researchRequired = parentLoaded.researchRequired;
      s.reassessmentRequired = parentLoaded.reassessmentRequired;
      s.reassessmentReason = parentLoaded.reassessmentReason;
      s.reassessmentEvidence = parentLoaded.reassessmentEvidence;
      s.reassessmentVersion = parentLoaded.reassessmentVersion;
      s.resolvedReassessmentVersion = parentLoaded.resolvedReassessmentVersion;
      s.planVersion = parentLoaded.planVersion;
      s.planConfidence = parentLoaded.planConfidence;
      s.lastPlanRevisionsText = parentLoaded.lastPlanRevisionsText;
      s.lastResearchAt = parentLoaded.lastResearchAt ?? Date.now();
      s.lastPlanRevisionAt = parentLoaded.lastPlanRevisionAt ?? Date.now();
      s.awaitingUserConfirmation = false;
      if (parentLoaded.reassessmentRequired) {
        startResearchEpoch(s, "reassessment");
      } else if (parentLoaded.researchComplete) {
        s.currentReceipt = null;
        s.lastCompletedReceipt = {
          epoch: 0,
          epochType: "historical",
          startedAt: parentLoaded.lastResearchAt || Date.now(),
          completedAt: parentLoaded.lastResearchAt || Date.now(),
          toolCalls: 0,
          readTargets: [],
          searchTargets: [],
          commands: [],
          evidenceCount: 0,
          isHistorical: true,
        };
      } else {
        startResearchEpoch(s, "research");
      }
      syncImplementationPermission(s);
    } else {
      s.prompts = [];
      s.refinements = [];
      s.researchRequired = false;
      s.researchComplete = false;
      s.reassessmentRequired = false;
      s.reassessmentReason = null;
      s.reassessmentEvidence = null;
      s.reassessmentVersion = 0;
      s.resolvedReassessmentVersion = 0;
      s.awaitingUserConfirmation = false;
      syncImplementationPermission(s);
    }
    if (state !== s) Object.assign(state, s);
  } else {
    s.stack = stack;
    state.stack = stack;
  }

  if (nextActive) {
    await verifyAndMarkSaved(pi, ctx, nextActive);
    persist(pi, ctx);
    logSubquestTransition(
      "SUBQUEST_RETURN",
      `returned to quest '${nextActive}' (LIFO stack)`,
      { quest: nextActive, parent: nextActive, child: archivedName },
    );
  } else {
    persist(pi, ctx);
  }
}
