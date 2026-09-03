import { parseMarkdownSections } from "../../markdown.ts";
import {
  computePlanReviewBoundaryKey,
  isPlanReviewValidForState,
} from "../../critical_agent.ts";
import type { StoredState } from "../../types.ts";
import { isPlaceholderOrEmpty } from "../../utils.ts";

export function normalizePlanText(planText: any): string {
  if (!planText) return "";
  let text = "";
  if (Array.isArray(planText)) {
    text = planText
      .map((p: any) => String(p).trim())
      .filter((p: string) => p.length > 0)
      .map((p: string, i: number) => (/^\d+\./.test(p) ? p : `${i + 1}. ${p}`))
      .join("\n");
  } else {
    text = String(planText).trim();
  }
  if (isPlaceholderOrEmpty(text)) return "";
  return text
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter((l) => l.length > 0)
    .join("\n");
}

export function normalizePlanRevisionsText(revisionsText: any): string {
  if (!revisionsText) return "";
  let text = "";
  if (Array.isArray(revisionsText)) {
    text = revisionsText
      .map((r: any) => String(r).trim())
      .filter((r: string) => r.length > 0)
      .map((r: string) => (r.startsWith("- ") ? r : `- ${r}`))
      .join("\n");
  } else {
    text = String(revisionsText).trim();
  }
  if (isPlaceholderOrEmpty(text)) return "";
  return text
    .split(/\r?\n/)
    .map((l) => l.trim())
    .filter((l) => l.length > 0)
    .join("\n");
}

export interface PlanningStateSnapshot {
  planVersion: number;
  normalizedPlan: string;
  normalizedRevisions: string;
  researchComplete: boolean;
  researchRequired: boolean;
  reassessmentRequired: boolean;
  reassessmentVersion: number;
  isReviewValid: boolean;
  boundaryKey: string;
  hasActionablePlan: boolean;
}

export function capturePreUpdatePlanningSnapshot(
  targetName: string,
  existingContent: string,
  targetState: StoredState,
): PlanningStateSnapshot {
  const sections = parseMarkdownSections(existingContent);
  const planRaw = sections.get("plan")?.body ||
    sections.get("detailed multi-stage execution plan")?.body || "";
  const revisionsRaw = sections.get("plan revisions")?.body ||
    sections.get("plan revision history")?.body ||
    targetState.lastPlanRevisionsText || "";
  const normalizedPlan = normalizePlanText(planRaw);
  const normalizedRevisions = normalizePlanRevisionsText(revisionsRaw);
  const planVersion = targetState.planVersion || 1;
  const boundaryKey = computePlanReviewBoundaryKey(
    targetName,
    planVersion,
    normalizedPlan,
    normalizedRevisions,
  );
  const hasActionablePlan = normalizedPlan.length > 0;

  return {
    planVersion,
    normalizedPlan,
    normalizedRevisions,
    researchComplete: Boolean(targetState.researchComplete),
    researchRequired: Boolean(targetState.researchRequired),
    reassessmentRequired: Boolean(targetState.reassessmentRequired),
    reassessmentVersion: targetState.reassessmentVersion || 0,
    isReviewValid: isPlanReviewValidForState(targetState),
    boundaryKey,
    hasActionablePlan,
  };
}

export function capturePostUpdatePlanningSnapshot(
  targetName: string,
  updatedMarkdown: string,
  targetState: StoredState,
): PlanningStateSnapshot {
  const sections = parseMarkdownSections(updatedMarkdown);
  const planRaw = sections.get("plan")?.body ||
    sections.get("detailed multi-stage execution plan")?.body || "";
  const revisionsRaw = sections.get("plan revisions")?.body ||
    sections.get("plan revision history")?.body ||
    targetState.lastPlanRevisionsText || "";
  const normalizedPlan = normalizePlanText(planRaw);
  const normalizedRevisions = normalizePlanRevisionsText(revisionsRaw);
  const planVersion = targetState.planVersion || 1;
  const boundaryKey = computePlanReviewBoundaryKey(
    targetName,
    planVersion,
    normalizedPlan,
    normalizedRevisions,
  );
  const hasActionablePlan = normalizedPlan.length > 0;

  return {
    planVersion,
    normalizedPlan,
    normalizedRevisions,
    researchComplete: Boolean(targetState.researchComplete),
    researchRequired: Boolean(targetState.researchRequired),
    reassessmentRequired: Boolean(targetState.reassessmentRequired),
    reassessmentVersion: targetState.reassessmentVersion || 0,
    isReviewValid: isPlanReviewValidForState(targetState),
    boundaryKey,
    hasActionablePlan,
  };
}
