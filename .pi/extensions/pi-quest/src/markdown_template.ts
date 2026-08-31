export * from "./markdown/template/header.ts";
export * from "./markdown/template/epistemic.ts";
export * from "./markdown/template/metadata.ts";
export * from "./markdown/template/plan.ts";
export * from "./markdown/template/execution.ts";
export * from "./markdown/template/rules.ts";

import { buildTemplateHeader } from "./markdown/template/header.ts";
import { buildTemplateEpistemicSections } from "./markdown/template/epistemic.ts";
import { buildTemplateMetadataSections } from "./markdown/template/metadata.ts";
import { buildTemplatePlanSections } from "./markdown/template/plan.ts";
import { buildTemplateExecutionSnapshot, buildTemplateFooterSections } from "./markdown/template/execution.ts";

export function QUEST_TEMPLATE(name: string, goal = "", parent = "", originalRequest = "", refinements: string[] = [], questId = ""): string {
	return [
		...buildTemplateHeader(name, goal, parent, originalRequest, questId),
		...buildTemplateEpistemicSections(),
		...buildTemplateMetadataSections(),
		...buildTemplatePlanSections(),
		...buildTemplateExecutionSnapshot(goal),
		...buildTemplateFooterSections(refinements),
	].join("\n");
}
