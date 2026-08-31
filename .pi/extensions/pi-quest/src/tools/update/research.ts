import { syncImplementationPermission } from "../../gates.ts";
import { logResearchTransition } from "../../logging.ts";
import { spliceMarkdownSections } from "../../markdown.ts";
import { reportAgentError } from "../../messaging.ts";
import { formatInvestigationEvidenceSummary, hasSufficientInvestigation, startResearchEpoch } from "../../research.ts";
import { isRootQuest, state } from "../../state.ts";
import { QuestErrorCode } from "../../types.ts";
import type { ExtensionAPI, ExtensionContext } from "../../types.ts";
import { validateResearchPrerequisites } from "../../validation.ts";

export function handleResearchCompletion(
	params: any,
	content: string,
	updates: Map<string, string>,
	targetName: string,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): string {
	if (params.researchComplete === false) {
		state.researchComplete = false;
		state.researchRequired = true;
		startResearchEpoch(state, "research");
		syncImplementationPermission(state);
		return "";
	}

	if (params.researchComplete !== true) return "";

	if (state.reassessmentRequired) {
		return " (Note: researchComplete ignored because reassessment is pending; call with reassessmentComplete: true and reassessmentConclusion to resolve)";
	}

	const provisionalMarkdown = spliceMarkdownSections(content, updates);
	const validation = validateResearchPrerequisites(provisionalMarkdown, params.planConfidence || state.planConfidence, params.allowLowConfidence === true, params.planConfidenceReason);
	const evidenceCheck = hasSufficientInvestigation(state, "research");

	if (!validation.valid) {
		state.researchComplete = false;
		state.researchRequired = true;
		syncImplementationPermission(state);
		logResearchTransition("RESEARCH_REJECTED", "research completion rejected: missing or placeholder sections", {
			quest: targetName,
			reason: `missing: ${validation.missingSections.join(", ")}`,
			round: state.researchRound,
		});
		reportAgentError(
			pi,
			ctx,
			`Research completion refused: Missing or placeholder sections: [${validation.missingSections.join(", ")}]${validation.confidenceIssue ? `; ${validation.confidenceIssue}` : ""}`,
			{
				code: QuestErrorCode.RESEARCH_REQUIRED,
				requiredNextAction: "Update the missing sections with verified discoveries and execution plan, then retry researchComplete.",
				details: { Quest: targetName, Missing: validation.missingSections.join(", ") },
			},
		);
		return ` (Note: researchComplete refused -- missing or placeholder: [${validation.missingSections.join(", ")}]${validation.confidenceIssue ? `; ${validation.confidenceIssue}` : ""})`;
	}

	if (!evidenceCheck.sufficient) {
		state.researchComplete = false;
		state.researchRequired = true;
		syncImplementationPermission(state);
		logResearchTransition("RESEARCH_REJECTED", `research completion rejected: ${evidenceCheck.reason}`, {
			quest: targetName,
			reason: evidenceCheck.reason,
			round: state.researchRound,
			evidence: evidenceCheck.receipt?.evidenceCount || 0,
		});
		reportAgentError(
			pi,
			ctx,
			`The research record is not sufficient to complete this research round. The extension has not observed enough fresh investigation since Research Round ${state.researchRound || 1} began. (${evidenceCheck.reason})`,
			{
				code: QuestErrorCode.RESEARCH_EVIDENCE_REQUIRED,
				requiredNextAction: "Perform targeted investigation of the relevant architecture/code paths using read/search tools, then update the quest state with the conclusions and retry researchComplete.",
				details: { Quest: targetName, Reason: evidenceCheck.reason },
			},
		);
		return ` (Note: researchComplete refused -- ${evidenceCheck.reason})`;
	}

	if (params.researchComplete === true && validation.valid && evidenceCheck.sufficient) {
		logResearchTransition("RESEARCH_COMPLETED", `research complete`, {
			quest: targetName,
			round: state.researchRound,
			planVersion: state.planVersion,
		});
	}

	state.researchComplete = true;
	state.researchRequired = false;
	state.lastResearchAt = Date.now();
	const isAlreadyConfirmed = Array.isArray(state.confirmedQuests) && state.confirmedQuests.includes(targetName);
	state.awaitingUserConfirmation = isRootQuest(state) && !isAlreadyConfirmed;
	if (evidenceCheck.receipt) {
		state.lastCompletedReceipt = { ...evidenceCheck.receipt, completedAt: Date.now() };
	}
	syncImplementationPermission(state);
	const evidenceSummary = evidenceCheck.receipt ? formatInvestigationEvidenceSummary(evidenceCheck.receipt) : "";
	return ` Research marked complete.${evidenceSummary ? ` [Evidence: ${evidenceSummary}]` : ""}`;
}

export function applyEpistemicMetadataToUpdates(updates: Map<string, string>, targetState?: any): void {
	const s = targetState || state;
	updates.set("plan version", String(s.planVersion || 1));
	updates.set("research round", String(s.researchRound || 1));
	updates.set("reassessment version", String(s.reassessmentVersion || 0));
	updates.set("resolved reassessment version", String(s.resolvedReassessmentVersion || 0));
	if (s.lastResearchAt) updates.set("last research at", String(s.lastResearchAt));
	if (s.lastPlanRevisionAt) updates.set("last plan revision at", String(s.lastPlanRevisionAt));
	if (s.reassessmentRequired) {
		updates.set("reassessment status", `REQUIRED (v${s.reassessmentVersion || 1}) - ${s.reassessmentReason || "contradiction detected"}`);
	} else {
		updates.set("reassessment status", `RESOLVED (v${s.resolvedReassessmentVersion || 0})`);
	}
	if (s.reassessmentEvidence) updates.set("reassessment evidence", s.reassessmentEvidence);
}
