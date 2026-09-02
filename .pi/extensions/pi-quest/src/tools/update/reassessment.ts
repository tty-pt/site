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
	const conclusionMissing = isPlaceholderOrEmpty(conclusionVal);
	const evidenceInsufficient = !evidenceCheck.sufficient;

	if (!conclusionMissing && validation.valid && !evidenceInsufficient) {
		return { valid: true, note: "" };
	}

	// Everything below is the REJECTION path. Reassessment completion requires the
	// agent to satisfy the whole contract; the previous sequential early-returns
	// surfaced only one reason per turn, so the agent never assembled a valid payload.
	// Set the reassessment-incomplete flags the same way regardless of which condition
	// failed, then report ALL failing fields in a single aggregated message.
	st.reassessmentRequired = true;
	st.researchRequired = true;
	st.researchComplete = false;
	st.planConfidence = "low";

	const missingSections = (validation.missingSections || []).join(", ");
	const parts: string[] = [];
	if (conclusionMissing) {
		parts.push(
			'reassessmentConclusion is required -- provide a non-empty reassessmentConclusion stating what your fresh investigation established about the contradiction',
		);
	}
	if (missingSections) {
		parts.push(`the replacement epistemic state is invalid or missing: [${missingSections}]`);
	}
	if (validation.confidenceIssue) {
		parts.push(validation.confidenceIssue);
	}
	if (evidenceInsufficient) {
		parts.push(
			`a fresh investigation is required: run a read/code-search NOW (after the trigger). ${evidenceCheck.reason}`,
		);
	}
	if (parts.length === 0) {
		parts.push("reassessment prerequisites were not met");
	}

	const message =
		`Reassessment cannot be completed yet. Complete ALL of the following, then retry reassessmentComplete:\n• ` +
		parts.join("\n• ") +
		`\nAlso ensure <quest>.md epistemic sections are valid; resolve any Files Modified save-verification first.`;

	reportAgentError(pi, ctx, message, {
		code: evidenceInsufficient ? QuestErrorCode.REASSESSMENT_EVIDENCE_REQUIRED : QuestErrorCode.REASSESSMENT_REQUIRED,
		requiredNextAction:
			"Run one read/code-search in this state, update <quest>.md, then retry reassessmentComplete with the assembled payload above.",
		details: {
			Quest: targetName,
			MissingConclusion: conclusionMissing,
			Missing: missingSections,
			ConfidenceIssue: validation.confidenceIssue || "",
			EvidenceReason: evidenceCheck.reason || "",
		},
	});

	return {
		valid: false,
		note: ` (Note: reassessmentComplete refused -- ${parts.join("; ")})`,
	};
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
