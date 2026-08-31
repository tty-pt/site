import { syncImplementationPermission } from "../../gates.ts";
import { logReassessmentTransition } from "../../logging.ts";
import { spliceMarkdownSections } from "../../markdown.ts";
import { reportAgentError } from "../../messaging.ts";
import { formatInvestigationEvidenceSummary, hasSufficientInvestigation } from "../../research.ts";
import { state as st } from "../../state.ts";
import { QuestErrorCode } from "../../types.ts";
import type { ExtensionAPI, ExtensionContext } from "../../types.ts";
import { isPlaceholderOrEmpty } from "../../utils.ts";
import { validateResearchPrerequisites } from "../../validation.ts";

export function validateReassessmentPrerequisites(
	conclusionVal: any,
	validation: any,
	evidenceCheck: any,
	targetName: string,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): { valid: boolean; note: string } {
	if (isPlaceholderOrEmpty(conclusionVal)) {
		st.reassessmentRequired = true;
		st.researchRequired = true;
		st.researchComplete = false;
		st.planConfidence = "low";
		reportAgentError(
			pi,
			ctx,
			"Reassessment cannot be completed yet. A non-empty reassessmentConclusion is required stating what fresh investigation established about the contradiction.",
			{
				code: QuestErrorCode.REASSESSMENT_REQUIRED,
				requiredNextAction: "Provide a detailed reassessmentConclusion stating what was investigated and what was disproved or confirmed, then retry reassessmentComplete.",
				details: { Quest: targetName },
			},
		);
		return {
			valid: false,
			note: " (Note: reassessmentComplete refused -- requires a non-empty reassessmentConclusion stating what fresh investigation established about the contradiction)",
		};
	}

	if (!validation.valid) {
		st.reassessmentRequired = true;
		st.researchRequired = true;
		st.researchComplete = false;
		st.planConfidence = "low";
		reportAgentError(
			pi,
			ctx,
			`Reassessment cannot be completed yet. Replacement epistemic state invalid or missing: [${validation.missingSections.join(", ")}]${validation.confidenceIssue ? `; ${validation.confidenceIssue}` : ""}`,
			{
				code: QuestErrorCode.REASSESSMENT_REQUIRED,
				requiredNextAction: "Update the missing or invalid sections in the quest state, then retry reassessmentComplete.",
				details: { Quest: targetName, Missing: validation.missingSections.join(", ") },
			},
		);
		return {
			valid: false,
			note: ` (Note: reassessmentComplete refused -- replacement epistemic state invalid or missing: [${validation.missingSections.join(", ")}]${validation.confidenceIssue ? `; ${validation.confidenceIssue}` : ""})`,
		};
	}

	if (!evidenceCheck.sufficient) {
		st.reassessmentRequired = true;
		st.researchRequired = true;
		st.researchComplete = false;
		st.planConfidence = "low";
		reportAgentError(
			pi,
			ctx,
			`Reassessment cannot be completed yet. A contradiction triggered a fresh reassessment epoch, but the extension has not observed the required investigation after that trigger. (${evidenceCheck.reason})`,
			{
				code: QuestErrorCode.REASSESSMENT_EVIDENCE_REQUIRED,
				requiredNextAction: "Investigate the contradiction and its underlying assumptions using read/search tools, then record the established facts and revised/revalidated plan before completing reassessment.",
				details: { Quest: targetName, Reason: evidenceCheck.reason },
			},
		);
		return {
			valid: false,
			note: ` (Note: reassessmentComplete refused -- ${evidenceCheck.reason})`,
		};
	}

	return { valid: true, note: "" };
}

export function resolveReassessmentState(targetName: string, evidenceCheck: any): void {
	st.resolvedReassessmentVersion = st.reassessmentVersion || 1;
	st.reassessmentRequired = false;
	st.reassessmentReason = null;
	st.reassessmentEvidence = null;
	st.lastReassessmentPromptAt = 0;
	st.lastReassessmentReason = null;
	st.consecutiveFailures = 0;
	st.researchRequired = false;
	st.researchComplete = true;
	st.lastResearchAt = Date.now();
	st.awaitingUserConfirmation = false;
	if (st.activeTransaction && (st.activeTransaction.phase === "failed" || st.activeTransaction.phase === "inconsistent")) {
		st.activeTransaction = null;
		st.activeCompactionId = null;
	}
	if (!Array.isArray(st.confirmedQuests)) st.confirmedQuests = [];
	if (!st.confirmedQuests.includes(targetName)) st.confirmedQuests.push(targetName);
	if (evidenceCheck.receipt) {
		st.lastCompletedReceipt = { ...evidenceCheck.receipt, completedAt: Date.now() };
	}
	syncImplementationPermission(st);
}

export function handleReassessmentCompletion(
	params: any,
	content: string,
	updates: Map<string, string>,
	existingSections: Map<string, any>,
	targetName: string,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): string {
	const conclusionVal = params.reassessmentConclusion || updates.get("latest reassessment") || (existingSections.get("latest reassessment")?.body);
	const provisionalMarkdown = spliceMarkdownSections(content, updates);
	const validation = validateResearchPrerequisites(provisionalMarkdown, params.planConfidence || st.planConfidence, params.allowLowConfidence === true, params.planConfidenceReason);
	const evidenceCheck = hasSufficientInvestigation(st, "reassessment");

	const checkResult = validateReassessmentPrerequisites(conclusionVal, validation, evidenceCheck, targetName, pi, ctx);
	if (!checkResult.valid) {
		logReassessmentTransition("REASSESSMENT_REJECTED", "reassessment completion rejected", {
			quest: targetName,
			reason: checkResult.note,
			version: st.reassessmentVersion,
		});
		return checkResult.note;
	}

	resolveReassessmentState(targetName, evidenceCheck);
	logReassessmentTransition("REASSESSMENT_COMPLETED", "reassessment complete and resolved", {
		quest: targetName,
		version: st.reassessmentVersion,
		round: st.researchRound,
		planVersion: st.planVersion,
	});
	const evidenceSummary = evidenceCheck.receipt ? formatInvestigationEvidenceSummary(evidenceCheck.receipt) : "";
	return ` Reassessment marked complete and resolved.${evidenceSummary ? ` [Evidence: ${evidenceSummary}]` : ""}`;
}
