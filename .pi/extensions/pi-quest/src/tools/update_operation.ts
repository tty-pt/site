import { mkdir, readFile, stat, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { QUEST_CURRENT_DIR } from "../constants.ts";
import { canImplement, syncImplementationPermission } from "../gates.ts";
import {
	logEvent,
	logReassessmentTransition,
	logResearchTransition,
	logStateUpdateTransition,
} from "../logging.ts";
import { ensureQuestIdInContent, parseMarkdownSections, QUEST_TEMPLATE, spliceMarkdownSections } from "../markdown.ts";
import { logError, reportAgentError } from "../messaging.ts";
import { fileExists, listActiveQuestRecords, questDirPath, questPath, resolveQuestRecordBySlug, slugify } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { formatInvestigationEvidenceSummary, hasSufficientInvestigation, startResearchEpoch } from "../research.ts";
import { ensureQuestId, getState, isRootQuest, state } from "../state.ts";
import { checkAndEmitGateContinuationSteer } from "../tool_gating.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode } from "../types.ts";
import { isPlaceholderOrEmpty } from "../utils.ts";
import { validateResearchPrerequisites } from "../validation.ts";
import { checkAndTriggerDirectionReview } from "../critical_agent.ts";
import { formatMarkSavedResponse, formatUpdateStateResponse } from "./formatting.ts";

async function resolveMarkTargetName(params: any): Promise<string | null> {
	const direct = params?.name ? slugify(params.name) : params?.questName ? slugify(params.questName) : state.active;
	if (direct) return direct;

	const records = await listActiveQuestRecords();
	if (records.length >= 1) {
		return records[0].name;
	}
	return null;
}

export async function executeMarkTool(params: any, pi: ExtensionAPI, ctx: ExtensionContext) {
	const targetName = await resolveMarkTargetName(params);

	if (!targetName) {
		return {
			content: [{ type: "text", text: "Error: No active quest is set. Pass the quest name or use quest_update_state({ name: '...' })." }],
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
			details: { error: "file_missing_or_unreadable", path: questPath(targetName), success: false },
		};
	}

	return formatMarkSavedResponse(targetName, res);
}

export function populateCoreEpistemicUpdates(params: any, updates: Map<string, string>): void {
	if (params.goal) updates.set("goal", params.goal);
	if (params.status) updates.set("current status", params.status);

	if (params.understanding || params.currentUnderstanding) {
		const val = params.understanding || params.currentUnderstanding;
		const text = Array.isArray(val) ? val.map((u: string) => (u.startsWith("- ") ? u : `- ${u}`)).join("\n") : String(val);
		updates.set("current understanding", text);
	}

	if (params.assumptions || params.keyAssumptions) {
		const val = params.assumptions || params.keyAssumptions;
		const text = Array.isArray(val) ? val.map((a: string) => (a.startsWith("- [") || a.startsWith("- ") ? a : `- [ ] ${a}`)).join("\n") : String(val);
		updates.set("key assumptions", text);
	}

	if (params.openQuestions || params.uncertainties) {
		const val = params.openQuestions || params.uncertainties;
		const text = Array.isArray(val) ? val.map((q: string) => (q.startsWith("- [") || q.startsWith("- ") ? q : `- [ ] ${q}`)).join("\n") : String(val);
		updates.set("open questions & uncertainties", text);
	}

	const findingsList = params.findings || params.researchFindings || params.importantFindings;
	if (Array.isArray(findingsList) && findingsList.length > 0) {
		const findingsText = findingsList.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
		updates.set("research findings", findingsText);
		updates.set("in-depth analysis & findings", findingsText);
	}
}

export function populatePlanAndReassessmentUpdates(params: any, updates: Map<string, string>, targetState?: any): void {
	const s = targetState || state;
	if (params.plan || params.executionPlan) {
		const val = params.plan || params.executionPlan;
		const text = Array.isArray(val) ? val.map((p: string, i: number) => (/^\d+\./.test(p) ? p : `${i + 1}. ${p}`)).join("\n") : String(val);
		updates.set("plan", text);
		updates.set("detailed multi-stage execution plan", text);
	}

	if (params.planConfidence) {
		const confStr = String(params.planConfidence);
		const reasonStr = params.planConfidenceReason ? `\nReason:\n${params.planConfidenceReason}` : "";
		updates.set("plan confidence", `${confStr}${reasonStr}`);
		const lowerConf = confStr.toLowerCase();
		if (lowerConf.includes("high")) s.planConfidence = "high";
		else if (lowerConf.includes("medium")) s.planConfidence = "medium";
		else if (lowerConf.includes("low")) s.planConfidence = "low";
	}

	if (params.planRevisions || params.revisions) {
		const val = params.planRevisions || params.revisions;
		const text = Array.isArray(val) ? val.map((r: string) => (r.startsWith("- ") ? r : `- ${r}`)).join("\n") : String(val);
		updates.set("plan revisions", text);
		if (text.trim() !== (s.lastPlanRevisionsText || "").trim()) {
			s.planVersion = Math.max(typeof params.planVersion === "number" ? params.planVersion : 0, (s.planVersion || 1) + 1);
			s.lastPlanRevisionAt = Date.now();
			s.lastPlanRevisionsText = text.trim();
		}
	} else if (typeof params.planVersion === "number" && params.planVersion > (s.planVersion || 1)) {
		s.planVersion = params.planVersion;
		s.lastPlanRevisionAt = Date.now();
	}

	if (params.rejectedApproaches) {
		const val = params.rejectedApproaches;
		const text = Array.isArray(val) ? val.map((r: string) => (r.startsWith("- ") ? r : `- ${r}`)).join("\n") : String(val);
		updates.set("rejected approaches", text);
	}

	if (params.reassessmentConclusion) updates.set("latest reassessment", params.reassessmentConclusion);
	if (Array.isArray(params.decisions) && params.decisions.length > 0) {
		updates.set("decisions made", params.decisions.map((d: string) => (d.startsWith("- ") ? d : `- ${d}`)).join("\n"));
	}
	if (Array.isArray(params.constraints) && params.constraints.length > 0) {
		updates.set("constraints & rules", params.constraints.map((c: string) => (c.startsWith("- ") ? c : `- ${c}`)).join("\n"));
	}
}

export function populateProgressAndArtifactUpdates(params: any, updates: Map<string, string>): void {
	if (Array.isArray(params.filesExamined) && params.filesExamined.length > 0) {
		updates.set("files examined", params.filesExamined.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n"));
	}
	if (params.completed) {
		const val = params.completed;
		updates.set("completed", Array.isArray(val) ? val.map((c: string) => (c.startsWith("- ") ? c : `- ${c}`)).join("\n") : String(val));
	}
	if (params.inProgress) {
		const val = params.inProgress;
		updates.set("in progress", Array.isArray(val) ? val.map((ip: string) => (ip.startsWith("- ") ? ip : `- ${ip}`)).join("\n") : String(val));
	}

	const filesTouchedList = params.filesTouched || params.filesModified;
	if (Array.isArray(filesTouchedList) && filesTouchedList.length > 0) {
		const filesText = filesTouchedList.map((f: string) => (f.startsWith("- ") ? f : `- ${f}`)).join("\n");
		updates.set("files touched", filesText);
		updates.set("files modified", filesText);
	} else if (typeof filesTouchedList === "string" && filesTouchedList.trim()) {
		updates.set("files touched", filesTouchedList.trim());
		updates.set("files modified", filesTouchedList.trim());
	}

	if (params.testStatus) {
		updates.set("test / build status", typeof params.testStatus === "string" ? params.testStatus : JSON.stringify(params.testStatus));
	}
	if (Array.isArray(params.remaining) && params.remaining.length > 0) {
		updates.set("remaining work", params.remaining.map((r: string) => (r.startsWith("- [") ? r : `- [ ] ${r}`)).join("\n"));
	}
	const snapshot = params.executionSnapshot || params.snapshot;
	if (snapshot) updates.set("execution snapshot", snapshot);

	const nextStep = params.nextStep || params.nextAction || params.exactNextAction;
	if (nextStep) {
		updates.set("exact next action", nextStep);
		updates.set("next recommended step", nextStep);
	}
	const resumeContext = params.resumeContext || params.resumePrompt;
	if (resumeContext) updates.set("resume prompt", resumeContext);
}

export function populateEpistemicUpdates(params: any, updates: Map<string, string>, targetState?: any): void {
	populateCoreEpistemicUpdates(params, updates);
	populatePlanAndReassessmentUpdates(params, updates, targetState);
	populateProgressAndArtifactUpdates(params, updates);
}

export function validateReassessmentPrerequisites(
	conclusionVal: any,
	validation: any,
	evidenceCheck: any,
	targetName: string,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): { valid: boolean; note: string } {
	if (isPlaceholderOrEmpty(conclusionVal)) {
		state.reassessmentRequired = true;
		state.researchRequired = true;
		state.researchComplete = false;
		state.planConfidence = "low";
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
		state.reassessmentRequired = true;
		state.researchRequired = true;
		state.researchComplete = false;
		state.planConfidence = "low";
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
		state.reassessmentRequired = true;
		state.researchRequired = true;
		state.researchComplete = false;
		state.planConfidence = "low";
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
	state.resolvedReassessmentVersion = state.reassessmentVersion || 1;
	state.reassessmentRequired = false;
	state.reassessmentReason = null;
	state.reassessmentEvidence = null;
	state.lastReassessmentPromptAt = 0;
	state.lastReassessmentReason = null;
	state.consecutiveFailures = 0;
	state.researchRequired = false;
	state.researchComplete = true;
	state.lastResearchAt = Date.now();
	state.awaitingUserConfirmation = false;
	if (state.activeTransaction && (state.activeTransaction.phase === "failed" || state.activeTransaction.phase === "inconsistent")) {
		state.activeTransaction = null;
		state.activeCompactionId = null;
	}
	if (!Array.isArray(state.confirmedQuests)) state.confirmedQuests = [];
	if (!state.confirmedQuests.includes(targetName)) state.confirmedQuests.push(targetName);
	if (evidenceCheck.receipt) {
		state.lastCompletedReceipt = { ...evidenceCheck.receipt, completedAt: Date.now() };
	}
	syncImplementationPermission(state);
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
	const validation = validateResearchPrerequisites(provisionalMarkdown, params.planConfidence || state.planConfidence, params.allowLowConfidence === true, params.planConfidenceReason);
	const evidenceCheck = hasSufficientInvestigation(state, "reassessment");

	const checkResult = validateReassessmentPrerequisites(conclusionVal, validation, evidenceCheck, targetName, pi, ctx);
	if (!checkResult.valid) {
		logReassessmentTransition("REASSESSMENT_REJECTED", "reassessment completion rejected", {
			quest: targetName,
			reason: checkResult.note,
			version: state.reassessmentVersion,
		});
		return checkResult.note;
	}

	resolveReassessmentState(targetName, evidenceCheck);
	logReassessmentTransition("REASSESSMENT_COMPLETED", "reassessment complete and resolved", {
		quest: targetName,
		version: state.reassessmentVersion,
		round: state.researchRound,
		planVersion: state.planVersion,
	});
	const evidenceSummary = evidenceCheck.receipt ? formatInvestigationEvidenceSummary(evidenceCheck.receipt) : "";
	return ` Reassessment marked complete and resolved.${evidenceSummary ? ` [Evidence: ${evidenceSummary}]` : ""}`;
}

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

function syncQuestIdentity(targetName: string, pi: ExtensionAPI, ctx: ExtensionContext): void {
	const originalReq = state.pendingRootRequest || (state.prompts && state.prompts.length > 0 ? state.prompts[0] : "");
	if (!state.active || state.pendingRootQuest || targetName !== state.active) {
		state.active = targetName;
		state.pendingRootQuest = false;
		state.questIdentityEstablished = true;
		state.pendingRootRequest = null;
		if (!Array.isArray(state.stack) || state.stack.length === 0 || !state.stack.includes(targetName)) {
			state.stack = [targetName];
		}
		if (originalReq && (!state.prompts || state.prompts.length === 0)) {
			state.prompts = [originalReq];
		}
		persist(pi, ctx);
	}
}

export function resolveUpdateTarget(params: any): { targetName: string } | { errorResponse: any } {
	const rawName = (params?.name || params?.questName || "").trim();
	const targetName = slugify(rawName || state.active || "");
	if (!targetName) {
		return {
			errorResponse: {
				content: [
					{
						type: "text",
						text: "Error: No active quest to update and no quest name provided. Please specify a concise semantic quest name (e.g. name: 'persistent-agent-research').",
					},
				],
				details: { error: "no_active_quest" },
			},
		};
	}
	return { targetName };
}

export async function ensureQuestFileExists(targetName: string, goal = ""): Promise<string> {
	const rec = await resolveQuestRecordBySlug(targetName);
	if (rec) {
		return rec.path;
	}
	const qId = state.questId || ensureQuestId();
	await mkdir(questDirPath(qId), { recursive: true });
	let path = questPath(qId);
	if (state.active && state.active !== targetName && !isRootQuest(state)) {
		path = join(questDirPath(qId), `${slugify(targetName)}.md`);
	}
	const originalReq = state.pendingRootRequest || (state.prompts && state.prompts.length > 0 ? state.prompts[0] : "");
	if (!(await fileExists(path))) {
		await writeFile(path, QUEST_TEMPLATE(targetName, goal, "", originalReq, state.refinements || [], qId), "utf8");
	}
	return path;
}

export function capturePreUpdateGateSnapshot(ctx: ExtensionContext) {
	return {
		wasImplementable: canImplement(state, ctx),
		wasReassessmentPending: !!state.reassessmentRequired,
		wasResearchPending: !!state.researchRequired || !state.researchComplete,
		wasAwaitingConfirmation: !!state.awaitingUserConfirmation,
	};
}

export function constructUpdatedMarkdown(
	content: string,
	params: any,
	targetName: string,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): { updatedMarkdown: string; researchTransitionNote: string; reassessmentTransitionNote: string } {
	const targetState = getState(ctx);
	const existingSections = parseMarkdownSections(content);
	const updates = new Map<string, string>();

	populateEpistemicUpdates(params, updates, targetState);

	let reassessmentTransitionNote = "";
	if (params?.reassessmentComplete === true) {
		reassessmentTransitionNote = handleReassessmentCompletion(params, content, updates, existingSections, targetName, pi, ctx);
	}

	const researchTransitionNote = handleResearchCompletion(params, content, updates, targetName, pi, ctx);

	applyEpistemicMetadataToUpdates(updates, targetState);

	let updatedMarkdown = spliceMarkdownSections(content, updates);
	if (targetState.questId) {
		updatedMarkdown = ensureQuestIdInContent(updatedMarkdown, targetState.questId);
	}
	return { updatedMarkdown, researchTransitionNote, reassessmentTransitionNote };
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

export async function executeUpdateStateTool(params: any, pi: ExtensionAPI, ctx: ExtensionContext) {
	const targetResolution = resolveUpdateTarget(params);
	if ("errorResponse" in targetResolution) {
		return targetResolution.errorResponse;
	}
	const { targetName } = targetResolution;

	syncQuestIdentity(targetName, pi, ctx);
	const path = await ensureQuestFileExists(targetName, params?.goal || "");

	try {
		const content = await readFile(path, "utf8");
		const preSnapshot = capturePreUpdateGateSnapshot(ctx);

		const { updatedMarkdown, researchTransitionNote, reassessmentTransitionNote } = constructUpdatedMarkdown(
			content,
			params,
			targetName,
			pi,
			ctx,
		);

		await writeFile(path, updatedMarkdown, "utf8");
		const saveRes = await verifyAndMarkSaved(pi, ctx, targetName);
		persist(pi, ctx);

		logStateUpdateTransition("STATE_UPDATE_ACCEPTED", `state updated for ${targetName}`, {
			quest: targetName,
			gen: saveRes.count,
			status: params?.status,
		});

		notifyGateTransitions(targetName, preSnapshot, pi, ctx);

		const targetState = getState(ctx);
		if (targetState.researchComplete && !targetState.reassessmentRequired && !targetState.researchRequired && !targetState.awaitingUserConfirmation) {
			const hasMajorMilestone =
				params?.reassessmentComplete === true ||
				Boolean(params?.planRevisions || params?.revisions) ||
				(Array.isArray(params?.decisions) && params.decisions.length > 0) ||
				Boolean(params?.completed) ||
				params?.researchComplete === true;

			if (hasMajorMilestone) {
				const reason = params?.reassessmentComplete === true
					? "reassessment_resolved"
					: params?.planRevisions || params?.revisions
					? "plan_revision"
					: Array.isArray(params?.decisions) && params.decisions.length > 0
					? "architectural_decisions"
					: params?.completed
					? "phase_completed"
					: "plan_established";
				await checkAndTriggerDirectionReview(pi, ctx, reason);
			}
		}

		return formatUpdateStateResponse(
			targetName,
			path,
			params,
			saveRes,
			state.planVersion || 1,
			researchTransitionNote,
			reassessmentTransitionNote,
			state.reassessmentRequired,
			state.researchComplete,
		);
	} catch (err: any) {
		logError(`Failed to update quest state at ${path}`, err, ctx, QuestErrorCode.PERSISTENCE_FAILURE);
		reportAgentError(
			pi,
			ctx,
			`Failed to update quest state at ${path}: ${err?.message || err}`,
			{
				code: QuestErrorCode.STATE_RECONSTRUCTION_FAILURE,
				requiredNextAction: "Inspect file permissions and ensure the quest file markdown contains valid section headers.",
				details: { Quest: targetName, Path: path },
			},
		);
		return {
			content: [{ type: "text", text: `Error updating quest state: ${err?.message || err}` }],
			details: { error: "update_failed", message: String(err) },
		};
	}
}
