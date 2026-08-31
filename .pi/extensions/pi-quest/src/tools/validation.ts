import { reportAgentError } from "../messaging.ts";
import { slugify } from "../paths.ts";
import { state } from "../state.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode } from "../types.ts";

export function validateSubquestParams(
	params: any,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
): { name: string; goal: string; parentName: string; switchNow: boolean } | null {
	const goal = (params?.goal || params?.name || "").trim();
	const name = slugify(params?.name || goal || "");
	const parentName = slugify(params?.parentName || state.active || "");
	const switchNow = params?.switchNow !== false;

	if (!name) {
		reportAgentError(
			pi,
			ctx,
			"Sub-quest creation failed: Sub-quest name or goal description is required.",
			{
				code: QuestErrorCode.SUBQUEST_FAILURE,
				requiredNextAction: "Provide a descriptive sub-quest name or goal when calling quest_subquest.",
			},
		);
		return null;
	}
	if (!goal) {
		reportAgentError(
			pi,
			ctx,
			"Sub-quest creation failed: Sub-quest goal description is required.",
			{
				code: QuestErrorCode.SUBQUEST_FAILURE,
				requiredNextAction: "Provide a non-empty goal description when calling quest_subquest.",
			},
		);
		return null;
	}
	return { name, goal, parentName, switchNow };
}

export const QUEST_MARK_SAVED_SCHEMA = {
	type: "object",
	properties: {
		name: {
			type: "string",
			description: "Optional quest name/slug if setting or marking for the first time.",
		},
	},
	additionalProperties: false,
};

export const QUEST_UPDATE_STATE_SCHEMA = {
	type: "object",
	properties: {
		name: {
			type: "string",
			description: "Quest name/slug. Defaults to currently active quest.",
		},
		goal: {
			type: "string",
			description: "Quest goal description.",
		},
		status: {
			type: "string",
			description: "Current status description (e.g. 'Phase 1 Complete - tests passing').",
		},
		findings: {
			type: "array",
			items: { type: "string" },
			description: "Key findings or architectural discoveries.",
		},
		understanding: {
			type: "string",
			description: "Core architectural facts and verified execution paths.",
		},
		assumptions: {
			type: "array",
			items: { type: "string" },
			description: "Key assumptions supporting the approach.",
		},
		openQuestions: {
			type: "array",
			items: { type: "string" },
			description: "Material uncertainties to investigate.",
		},
		plan: {
			type: "array",
			items: { type: "string" },
			description: "Multi-stage execution plan steps.",
		},
		planConfidence: {
			type: "string",
			enum: ["low", "medium", "high"],
			description: "Confidence level in the current plan.",
		},
		planConfidenceReason: {
			type: "string",
			description: "Justification for the confidence level (verified execution paths, validated assumptions, remaining uncertainties).",
		},
		reassessmentConclusion: {
			type: "string",
			description: "What the fresh investigation established about the triggering contradiction, whether the previous assumption was validated/invalidated/reformulated, and whether the current plan survived or changed.",
		},
		planRevisions: {
			type: "array",
			items: { type: "string" },
			description: "Record of plan revisions with invalidating evidence.",
		},
		rejectedApproaches: {
			type: "array",
			items: { type: "string" },
			description: "Disproved hypotheses or abandoned approaches.",
		},
		decisions: {
			type: "array",
			items: { type: "string" },
			description: "Key architectural decisions made.",
		},
		constraints: {
			type: "array",
			items: { type: "string" },
			description: "Constraints and rules to adhere to.",
		},
		filesExamined: {
			type: "array",
			items: { type: "string" },
			description: "List of files examined during research.",
		},
		completed: {
			type: "array",
			items: { type: "string" },
			description: "List of completed tasks / steps.",
		},
		inProgress: {
			type: "array",
			items: { type: "string" },
			description: "List of tasks currently in progress.",
		},
		filesTouched: {
			type: "array",
			items: { type: "string" },
			description: "List of files modified or examined.",
		},
		filesModified: {
			type: "array",
			items: { type: "string" },
			description: "List of files modified.",
		},
		testStatus: {
			type: "string",
			description: "Current build and test status.",
		},
		remaining: {
			type: "array",
			items: { type: "string" },
			description: "List of remaining tasks / checklist items.",
		},
		executionSnapshot: {
			type: "string",
			description: "Comprehensive execution snapshot containing objective, completed, in progress, discoveries, decisions, files, test status, remaining work, and exact next action.",
		},
		exactNextAction: {
			type: "string",
			description: "Concrete next action to be performed immediately by a fresh agent.",
		},
		nextAction: {
			type: "string",
			description: "Next recommended action or step.",
		},
		nextStep: {
			type: "string",
			description: "Next recommended action or step.",
		},
		resumeContext: {
			type: "string",
			description: "Concise briefing giving the next agent iteration complete context.",
		},
		researchComplete: {
			type: "boolean",
			description: "Whether initial research cycle is complete.",
		},
		reassessmentComplete: {
			type: "boolean",
			description: "Whether pending reassessment has been completed and resolved.",
		},
		allowLowConfidence: {
			type: "boolean",
			description: "Allow completing research even if plan confidence is low (requires justification).",
		},
		planVersion: {
			type: "number",
			description: "Version number of the current plan.",
		},
	},
	additionalProperties: false,
};
