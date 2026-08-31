import { parseMarkdownSections } from "../../markdown.ts";
import { ConsistencyAuditResult } from "../../types.ts";
import { isPlaceholderOrEmpty } from "../../utils.ts";
import { extractLines, getSecBody } from "../helpers.ts";
import { checkCompletedEmpty, checkFilesModified, checkNextAction, checkPlanVersion, checkStatusAndRemaining } from "./checks.ts";

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

  checkNextAction({ completedBody, reassessmentBody, nextActionBody }, issues);
  checkFilesModified({ completedBody, reassessmentBody, filesModifiedBody }, options, hasFilesModified, issues);
  checkCompletedEmpty(hasCompleted, hasReassessment, reassessmentLines, issues);
  checkPlanVersion(planVersionBody, planRevisionsBody, issues);
  checkStatusAndRemaining({ uncertaintiesBody, assumptionsBody, testStatusBody, filesModifiedBody, completedBody, remainingBody, currentStatusBody }, hasFilesModified, hasTestStatus, hasCompleted, hasRemaining, options, issues);

  return { consistent: issues.length === 0, issues, warnings };
}
