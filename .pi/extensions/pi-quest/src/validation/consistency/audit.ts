import { parseMarkdownSections } from "../../markdown.ts";
import { ConsistencyAuditResult } from "../../types.ts";
import { isPlaceholderOrEmpty } from "../../utils.ts";
import {
  extractLines,
  getSecBody,
  isModificationStatement,
} from "../helpers.ts";
import {
  checkCompletedEmpty,
  checkFilesModified,
  checkNextAction,
  checkPlanVersion,
  checkStatusAndRemaining,
} from "./checks.ts";

export function auditQuestConsistency(
  markdownContent: string,
  options?: { recentModifiedFiles?: string[]; strict?: boolean },
): ConsistencyAuditResult {
  const issues: string[] = [];
  const warnings: string[] = [];
  if (!markdownContent) return { consistent: true, issues, warnings };

  const sections = parseMarkdownSections(markdownContent);
  const getBody = (k: string) => getSecBody(sections, k);

  const completedBody = getBody("completed");
  const reassessmentBody = getBody("latest reassessment");
  const nextActionBody = getBody("exact next action");
  const remainingBody = getBody("remaining work");
  const filesModifiedBody = getBody("files modified");
  const testStatusBody = getBody("test / build status");
  const planVersionBody = getBody("plan version");
  const planRevisionsBody = getBody("plan revisions");
  const assumptionsBody = getBody("key assumptions");
  const uncertaintiesBody = getBody("open questions & uncertainties");
  const currentStatusBody = getBody("current status");

  const hasCompleted = !isPlaceholderOrEmpty(completedBody);
  const hasReassessment = !isPlaceholderOrEmpty(reassessmentBody);
  const hasFilesModified = !isPlaceholderOrEmpty(filesModifiedBody);
  const hasTestStatus = !isPlaceholderOrEmpty(testStatusBody);
  const hasRemaining = !isPlaceholderOrEmpty(remainingBody);

  const reassessmentLines = extractLines(reassessmentBody);
  const completedLines = extractLines(completedBody);
  const planVersionNumForFiles =
    Number.parseInt(planVersionBody.replace(/\D/g, ""), 10) || 1;
  // A quest is research-only when no Completed/Reassessment statement actually
  // describes a file modification (reads/reviews do not modify files). Uses the
  // verb-aware classifier, so a substantive read-only Completed section (all the
  // files merely examined) no longer defeats the research-only guard.
  const hasModificationStatement = [...completedLines, ...reassessmentLines]
    .some((l) => isModificationStatement(l));
  const isResearchOnly = planVersionNumForFiles === 1 &&
    !hasModificationStatement;

  checkNextAction({ completedBody, reassessmentBody, nextActionBody }, issues);
  if (!isResearchOnly) {
    checkFilesModified(
      { completedBody, reassessmentBody, filesModifiedBody },
      options,
      hasFilesModified,
      issues,
    );
  }
  checkCompletedEmpty(hasCompleted, hasReassessment, reassessmentLines, issues);
  checkPlanVersion(planVersionBody, planRevisionsBody, issues);
  checkStatusAndRemaining(
    {
      uncertaintiesBody,
      assumptionsBody,
      testStatusBody,
      filesModifiedBody,
      completedBody,
      remainingBody,
      currentStatusBody,
    },
    hasFilesModified,
    hasTestStatus,
    hasCompleted,
    hasRemaining,
    { ...options, isResearchOnly },
    issues,
  );

  return { consistent: issues.length === 0, issues, warnings };
}
