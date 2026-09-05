import { readFile, writeFile } from "node:fs/promises";
import { logEvent } from "../logging.ts";
import { parseMarkdownSections, spliceMarkdownSections } from "../markdown.ts";
import { fileExists, questPath } from "../paths.ts";
import { state } from "../state.ts";
import { QuestErrorCode } from "../types.ts";
import { reportAgentError } from "../messaging.ts";
import type { ExtensionAPI, ExtensionContext } from "../types.ts";
import { submitReviewRebuttal } from "../critical_agent/policy.ts";

export async function executeRebutTool(
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
) {
  const rebuttal = (params?.rebuttal || params?.text || params?.message || "")
    .trim();
  const questName = (params?.questName || state.active || "").trim();
  if (!rebuttal || rebuttal.length < 10) {
    reportAgentError(
      pi,
      ctx,
      "quest_rebut requires a substantive rebuttal text (at least 10 chars) presenting evidence that addresses the reviewer's findings.",
      {
        code: QuestErrorCode.PLAN_REVIEW_REQUIRED,
        requiredNextAction:
          'Call quest_rebut with { rebuttal: "...evidence-based response..." } addressing each reviewer finding with file:line citations.',
      },
    );
    return {
      content: [{
        type: "text",
        text: "Rebuttal too short — provide substantive evidence.",
      }],
      details: { error: "rebuttal_too_short" },
    };
  }
  const targetQuest = questName || state.active || state.questId || "quest";
  const lastReview = state.lastCriticalReview;
  const findingsSummary =
    lastReview?.findings?.map((f: any) => f.issue).join("; ") ||
    "prior review findings";
  const verdictBefore = lastReview?.verdict || "REVISE";

  // Persist dialogue to state
  if (!Array.isArray(state.reviewDialogue)) state.reviewDialogue = [];
  const round = state.reviewDialogue.length + 1;
  const entry: any = {
    round,
    timestamp: Date.now(),
    reviewerFindings: findingsSummary,
    implementerRebuttal: rebuttal,
    verdictBefore,
  };
  state.reviewDialogue.push(entry);

  // Persist to quest.md as ## Review Dialogue
  try {
    const qPath = state.questId
      ? questPath(state.questId)
      : questPath(targetQuest);
    if (await fileExists(qPath)) {
      const content = await readFile(qPath, "utf8");
      const sections = parseMarkdownSections(content);
      const existing = sections.get("review dialogue")?.body || "";
      const newBody = existing
        ? `${existing.trim()}\n\n### Round ${round} — ${
          new Date().toISOString()
        }\n**Reviewer (${verdictBefore}):** ${
          findingsSummary.slice(0, 500)
        }\n\n**Implementer rebuttal:** ${rebuttal.slice(0, 1500)}\n`
        : `### Round ${round} — ${
          new Date().toISOString()
        }\n**Reviewer (${verdictBefore}):** ${
          findingsSummary.slice(0, 500)
        }\n\n**Implementer rebuttal:** ${rebuttal.slice(0, 1500)}\n`;
      const updates = new Map<string, string>();
      updates.set("review dialogue", newBody);
      const updated = spliceMarkdownSections(content, updates);
      await writeFile(qPath, updated, "utf8");
    }
  } catch {}

  logEvent(
    "REVIEW_REBUTTAL_SUBMITTED",
    `review rebuttal submitted round ${round}`,
    { quest: targetQuest, round, verdictBefore },
  );

  try {
    const result: any = await submitReviewRebuttal(pi, ctx, rebuttal, {
      questSlug: targetQuest,
    });
    const verdictAfter = result?.review?.verdict || result?.verdict ||
      "UNKNOWN";
    entry.verdictAfter = verdictAfter;
    entry.reviewerResponse = result?.review?.findings?.map((f: any) =>
      f.issue
    ).join("; ") || "";
    entry.resolved = verdictAfter === "APPROVE" || verdictAfter === "PASS";

    // Update quest.md with reviewer response
    try {
      const qPath = state.questId
        ? questPath(state.questId)
        : questPath(targetQuest);
      if (await fileExists(qPath)) {
        const content = await readFile(qPath, "utf8");
        const sections = parseMarkdownSections(content);
        const existing = sections.get("review dialogue")?.body || "";
        const appended =
          `${existing.trim()}\n\n**Reviewer response (${verdictAfter}):** ${
            entry.reviewerResponse.slice(0, 1000) || verdictAfter
          }\n`;
        const updates = new Map<string, string>();
        updates.set("review dialogue", appended);
        const updated = spliceMarkdownSections(content, updates);
        await writeFile(qPath, updated, "utf8");
      }
    } catch {}

    // Verdict reversal honored — if APPROVE, clear reassessment gate
    if (verdictAfter === "APPROVE" || verdictAfter === "PASS") {
      if (state.reassessmentRequired) {
        // Check if the reassessment was triggered by this review
        state.reassessmentRequired = false;
        state.reassessmentReason = null;
        state.reassessmentEvidence = null;
        state.researchRequired = false;
        state.researchComplete = true;
        const { syncImplementationPermission } = await import("../gates.ts");
        syncImplementationPermission(state, ctx);
      }
      logEvent(
        "REVIEW_REBUTTAL_VERDICT_REVERSED",
        `rebuttal round ${round} reversed verdict to ${verdictAfter}`,
        { quest: targetQuest, round },
      );
      return {
        content: [{
          type: "text",
          text:
            `Rebuttal round ${round} succeeded — reviewer reversed to ${verdictAfter}. Gate reopened. Dialogue persisted to ## Review Dialogue.`,
        }],
        details: { round, verdictBefore, verdictAfter, resolved: true },
      };
    }

    return {
      content: [{
        type: "text",
        text:
          `Rebuttal round ${round} submitted. Reviewer verdict: ${verdictAfter} (was ${verdictBefore}). ${
            verdictAfter === "REVISE" || verdictAfter === "FAIL"
              ? "Reviewer upheld findings — revise plan or escalate via quest_ask_human."
              : "See ## Review Dialogue for transcript."
          }`,
      }],
      details: { round, verdictBefore, verdictAfter, resolved: entry.resolved },
    };
  } catch (e: any) {
    reportAgentError(pi, ctx, `quest_rebut failed: ${e?.message || e}`, {
      code: QuestErrorCode.CRITICAL_REVIEW_ERROR,
      requiredNextAction:
        "Retry quest_rebut with substantive evidence, or escalate via quest_ask_human.",
    });
    return {
      content: [{ type: "text", text: `Rebuttal failed: ${e?.message || e}` }],
      details: { error: String(e) },
    };
  }
}
