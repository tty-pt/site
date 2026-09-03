import { syncImplementationPermission } from "../gates.ts";
import { startResearchEpoch } from "../research.ts";
import { state } from "../state.ts";

export function applyLoadedEpistemicState(
  targetState: any,
  loaded: any,
  fallbackPrompt?: string,
): void {
  if (loaded && loaded.exists) {
    if (loaded.questId) targetState.questId = loaded.questId;
    targetState.prompts = loaded.originalRequest
      ? [loaded.originalRequest]
      : fallbackPrompt
      ? [fallbackPrompt]
      : [];
    targetState.refinements = loaded.refinements;
    targetState.researchRound = loaded.researchRound;
    targetState.researchComplete = loaded.researchComplete;
    targetState.researchRequired = loaded.researchRequired;
    targetState.planVersion = loaded.planVersion;
    targetState.planConfidence = loaded.planConfidence;
    targetState.lastPlanRevisionsText = loaded.lastPlanRevisionsText;
    targetState.reassessmentRequired = loaded.reassessmentRequired;
    targetState.reassessmentReason = loaded.reassessmentReason;
    targetState.reassessmentEvidence = loaded.reassessmentEvidence;
    targetState.reassessmentVersion = loaded.reassessmentVersion;
    targetState.resolvedReassessmentVersion =
      loaded.resolvedReassessmentVersion;
    targetState.lastResearchAt = loaded.lastResearchAt ?? Date.now();
    targetState.lastPlanRevisionAt = loaded.lastPlanRevisionAt ?? Date.now();
    targetState.awaitingUserConfirmation = !loaded.researchComplete;
    if (loaded.researchComplete) {
      targetState.currentReceipt = null;
      targetState.lastCompletedReceipt = {
        epoch: 0,
        epochType: "historical",
        startedAt: loaded.lastResearchAt || Date.now(),
        completedAt: loaded.lastResearchAt || Date.now(),
        toolCalls: 0,
        readTargets: [],
        searchTargets: [],
        commands: [],
        evidenceCount: 0,
        isHistorical: true,
      };
    } else {
      startResearchEpoch(
        targetState,
        targetState.reassessmentRequired ? "reassessment" : "research",
      );
    }
  } else {
    targetState.researchRound = 1;
    targetState.researchComplete = false;
    targetState.researchRequired = true;
    targetState.reassessmentRequired = false;
    targetState.reassessmentReason = null;
    targetState.reassessmentEvidence = null;
    targetState.reassessmentVersion = 0;
    targetState.resolvedReassessmentVersion = 0;
    targetState.lastPlanRevisionsText = null;
    targetState.planVersion = 1;
    targetState.planConfidence = "low";
    targetState.lastResearchAt = Date.now();
    targetState.lastPlanRevisionAt = Date.now();
    targetState.awaitingUserConfirmation = false;
    startResearchEpoch(targetState, "research");
  }
  syncImplementationPermission(targetState);
  if (state !== targetState) Object.assign(state, targetState);
}

export function resetEpistemicStateForNewQuest(targetState: any): void {
  targetState.researchRound = 1;
  targetState.researchComplete = false;
  targetState.researchRequired = true;
  targetState.reassessmentRequired = false;
  targetState.reassessmentReason = null;
  targetState.reassessmentEvidence = null;
  targetState.reassessmentVersion = 0;
  targetState.resolvedReassessmentVersion = 0;
  targetState.lastPlanRevisionsText = null;
  targetState.planVersion = 1;
  targetState.planConfidence = "low";
  targetState.lastResearchAt = Date.now();
  targetState.lastPlanRevisionAt = Date.now();
  targetState.awaitingUserConfirmation = false;
  targetState.consecutiveFailures = 0;
  targetState.substantiveTurnsSinceCheckpoint = 0;
  targetState.lastReassessmentPromptAt = 0;
  targetState.lastReassessmentReason = null;
  targetState.lastCheckpointPromptAt = 0;
  startResearchEpoch(targetState, "research");
  syncImplementationPermission(targetState);
}
