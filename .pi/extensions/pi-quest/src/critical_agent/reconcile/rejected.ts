import { readFile, writeFile } from "node:fs/promises";
import { QuestErrorCode } from "../../constants.ts";
import { logCriticalReviewTransition } from "../../logging.ts";
import {
  parseMarkdownSections,
  spliceMarkdownSections,
} from "../../markdown.ts";
import {
  createAgentObligation,
  queueAgentObligation,
} from "../../obligations.ts";
import { sendInternalAgentMessage } from "../../messaging.ts";
import {
  fileExists,
  questPath,
  resolveQuestRecordBySlug,
} from "../../paths.ts";
import { persist } from "../../persistence.ts";
import { triggerReassessment } from "../../research.ts";
import { isReviewSnapshotCurrent } from "../snapshot.ts";
import { ExtensionAPI, ExtensionContext, ReviewSnapshot } from "../../types.ts";

export async function handleRejectedVerdict(
  snapshot: ReviewSnapshot,
  parsedResult: any,
  targetState: any,
  correlationId: string,
  reviewState: any,
  isPlanReviewKind: boolean,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
): Promise<{ success: boolean; available: boolean; review: any }> {
  const slug = snapshot.questId;
  const questId = targetState.questId || slug;
  const sessionId = snapshot.sessionId;
  const failEventType = isPlanReviewKind
    ? "PLAN_REVIEW_FAILED"
    : "CRITICAL_REVIEW_FAILED";
  logCriticalReviewTransition(
    failEventType,
    `critical review failed: ${
      parsedResult.findings.map((f: any) => f.issue).join("; ") ||
      "issues found"
    }`,
    {
      quest: slug,
      questId,
      sessionId,
      reviewId: correlationId,
      childSessionId: parsedResult.childSessionId,
      parentSessionId: sessionId,
      reviewKind: snapshot.reviewKind,
      severity: parsedResult.severity,
      verdict: parsedResult.verdict,
      durationMs: parsedResult.durationMs,
      reviewedVersion: snapshot.planVersion,
    },
  );
  logCriticalReviewTransition(
    "REMEDIATION_REQUIRED",
    `remediation required: ${
      parsedResult.requiredActions.join("; ") || "fix findings"
    }`,
    {
      quest: slug,
      questId,
      sessionId,
      reviewId: correlationId,
      childSessionId: parsedResult.childSessionId,
      parentSessionId: sessionId,
      reviewKind: snapshot.reviewKind,
      severity: parsedResult.severity,
      requiredAction: parsedResult.requiredActions.join("; "),
      reviewedVersion: snapshot.planVersion,
    },
  );

  if (isPlanReviewKind) targetState.lastPlanReviewApproval = null;

  const findingsSummary = parsedResult.findings.map((f: any) =>
    `- ${f.issue}${f.evidence ? `\n  Evidence: ${f.evidence}` : ""}`
  ).join("\n");
  const actionsSummary = parsedResult.requiredActions.map((a: any) => `- ${a}`)
    .join("\n");
  const complianceSummary = parsedResult.originalRequestCheck?.items?.length
    ? parsedResult.originalRequestCheck.items.map((i: any) =>
      `- ${i.requirement} -> Status: ${i.status}${
        i.planHandling ? ` (Plan: ${i.planHandling})` : ""
      }`
    ).join("\n")
    : "";

  const draftFile =
    `\`.pi/quest/future/${targetState.activeDraft ?? slug}.md\``;
  const errorMsg = isPlanReviewKind
    ? `[Quest Journal] ADVERSARIAL PLAN REVIEW REJECTED (VERDICT: REVISE)\n\nSeverity: ${parsedResult.severity}\n\nFindings:\n${
      findingsSummary || "(Unspecified plan finding)"
    }\n\nRequired Revisions:\n${
      actionsSummary ||
      "Address reviewer findings and revise the execution plan."
    }${
      complianceSummary
        ? `\n\nPrompt Compliance Check:\n${complianceSummary}`
        : ""
    }\n\nThe plan draft is NOT approved. Revise ${draftFile} directly (the \`## Implementation Plan\` section) to address these findings, then save — do NOT use \`quest_update_state\` for a draft plan before APPROVE. Saving a substantive plan triggers re-review automatically.`
    : `[Quest Journal] CRITICAL REVIEW FAILED\n\nSeverity: ${parsedResult.severity}\n\nFinding:\n${
      findingsSummary || "(Unspecified critical finding)"
    }\n\nRequired action:\n${
      actionsSummary ||
      "Investigate findings, fix deficiencies, and re-verify before proceeding."
    }\n\nDo not consider the affected work complete until this is resolved and verified.`;

  // Eager staleness filter: if already superseded, don't queue obligation at all
  if (!isReviewSnapshotCurrent(snapshot, targetState).current) {
    // Stale result – obligation suppressed entirely (no queue, no agent message)
  } else {
    const obl = createAgentObligation(targetState, {
      kind: "critical_review",
      code: isPlanReviewKind
        ? QuestErrorCode.PLAN_REVIEW_REQUIRED
        : QuestErrorCode.CRITICAL_REVIEW_FAILED,
      message: errorMsg,
      deliverAs: "followUp",
      requiredNextAction: actionsSummary ||
        "Revise plan/implementation to address critical review findings.",
      correlationId,
      isCurrent: (s) => isReviewSnapshotCurrent(snapshot, s).current,
    });
    queueAgentObligation(targetState, obl);
    sendInternalAgentMessage(
      pi,
      errorMsg,
      "followUp",
      isPlanReviewKind ? "plan_review_failed" : "critical_review_failed",
      correlationId,
      { triggerTurn: true, display: true },
    );
  }

  if (
    !isPlanReviewKind &&
    (parsedResult.severity === "CRITICAL" || parsedResult.severity === "MAJOR")
  ) {
    triggerReassessment(
      targetState,
      `Critical Review Failed: ${
        parsedResult.findings.map((f: any) => f.issue).join("; ")
      }`,
      findingsSummary,
    );
  }

  if (parsedResult.requiredActions && parsedResult.requiredActions.length > 0) {
    try {
      let qPath = questPath(questId);
      if (!(await fileExists(qPath))) {
        const rec = await resolveQuestRecordBySlug(slug);
        if (rec) qPath = rec.path;
      }
      if (await fileExists(qPath)) {
        const currentContent = await readFile(qPath, "utf8");
        const sections = parseMarkdownSections(currentContent);
        const remainingSec = sections.get("remaining work") ||
          sections.get("remaining tasks");
        const existingRemaining = remainingSec ? remainingSec.body.trim() : "";
        const newItems = parsedResult.requiredActions
          .filter((a: any) => !existingRemaining.includes(a))
          .map((a: any) => (a.startsWith("- [") ? a : `- [ ] ${a}`))
          .join("\n");
        if (newItems) {
          const combinedRemaining =
            existingRemaining && existingRemaining !== "-"
              ? `${existingRemaining}\n${newItems}`
              : newItems;
          const updates = new Map<string, string>();
          updates.set("remaining work", combinedRemaining);
          const updatedContent = spliceMarkdownSections(
            currentContent,
            updates,
          );
          await writeFile(qPath, updatedContent, "utf8");
        }
      }
    } catch {}
  }

  targetState.inCriticalReview = false;
  if (targetState.awaitingReview?.reviewId === correlationId) {
    targetState.awaitingReview = null;
  }
  persist(pi, ctx);
  return { success: false, available: true, review: reviewState };
}
