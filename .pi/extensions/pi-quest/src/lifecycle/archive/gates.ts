import { isCriticalReviewValidForCompletion, isSubagentAvailable, runCriticalReview } from "../../critical_agent.ts";
import { parseMarkdownSections } from "../../markdown.ts";
import type { ExtensionAPI, ExtensionContext } from "../../types.ts";

function checkOrdinaryCompletionConditions(markdownContent: string, s: any): { satisfied: boolean; reason?: string } {
	if (s.reassessmentRequired) {
		return { satisfied: false, reason: `Reassessment is pending (v${s.reassessmentVersion || 1}: ${s.reassessmentReason || "contradiction detected"}). Resolve reassessment before completing quest.` };
	}
	if (s.dirty) {
		return { satisfied: false, reason: "Active quest has unsaved changes. Save and mark saved before completing quest." };
	}
	if (markdownContent) {
		const sections = parseMarkdownSections(markdownContent);
		const remainingSec = sections.get("remaining work") || sections.get("remaining tasks") || sections.get("remaining");
		if (remainingSec && remainingSec.body) {
			const lines = remainingSec.body.split(/\r?\n/);
			const unchecked = lines.map((l: string) => l.trim()).filter((l: string) => {
				if (!l.startsWith("- [ ]") && !l.startsWith("* [ ]") && !l.startsWith("[ ]")) return false;
				const taskText = l.replace(/^[-*]?\s*\[\s*\]\s*/, "").trim();
				return taskText.length > 0 && !taskText.startsWith(">") && taskText !== "-" && !/^(none|n\/a|no remaining work|no remaining tasks)$/i.test(taskText);
			});
			if (unchecked.length > 0) {
				const sample = unchecked.slice(0, 3).map((u: string) => u.replace(/^[-*]?\s*\[\s*\]\s*/, "")).join("; ");
				return { satisfied: false, reason: `Unfinished tasks in Remaining Work: ${sample}${unchecked.length > 3 ? ` (and ${unchecked.length - 3} more)` : ""}` };
			}
		}
	}
	return { satisfied: true };
}

export async function runRootCompletionGates(
	questContent: string,
	s: any,
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	questName: string,
): Promise<{ blocked: boolean; message?: string }> {
	const ordinaryCheck = checkOrdinaryCompletionConditions(questContent, s);
	if (!ordinaryCheck.satisfied) {
		return { blocked: true, message: `Quest completion blocked by unmet completion conditions: ${ordinaryCheck.reason}` };
	}

	if (isSubagentAvailable(pi, ctx)) {
		const isValidPass = isCriticalReviewValidForCompletion(s);
		if (!isValidPass) {
			const reviewRes = await runCriticalReview(pi, ctx!, { kind: "final_acceptance", questSlug: questName });
			if (reviewRes.available && (!reviewRes.success || (reviewRes.review?.verdict !== "PASS" && reviewRes.review?.verdict !== "APPROVE"))) {
				const reason = reviewRes.review?.findings?.map((f: any) => f.issue).join("; ") || reviewRes.error || "Acceptance criteria unmet";
				return {
					blocked: true,
					message: `Final critical acceptance review failed (${reviewRes.review?.verdict || "ERROR"}${reviewRes.review?.severity ? ` / ${reviewRes.review.severity}` : ""}): ${reason}. Resolve findings before completing quest.`,
				};
			}
		}
		if (!isCriticalReviewValidForCompletion(s)) {
			return { blocked: true, message: "Final critical acceptance review PASS is missing or invalidated by subsequent state changes." };
		}
	}
	return { blocked: false };
}
