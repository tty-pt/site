import {
	CriticalReviewFinding,
	CriticalReviewOriginalRequestCheck,
	CriticalReviewSelfCritique,
	CriticalReviewSeverity,
	CriticalReviewVerdict,
} from "../../types.ts";

export function parseCriticalReviewResponse(responseText: string): {
	verdict: CriticalReviewVerdict;
	severity: CriticalReviewSeverity;
	findings: CriticalReviewFinding[];
	requiredActions: string[];
	originalRequestCheck: CriticalReviewOriginalRequestCheck;
	selfCritique?: CriticalReviewSelfCritique;
	parseError?: string;
} {
	if (!responseText || typeof responseText !== "string" || !responseText.trim()) {
		return {
			verdict: "UNCERTAIN",
			severity: "MAJOR",
			findings: [{ issue: "Empty reviewer response", evidence: "No output received from critical reviewer" }],
			requiredActions: ["Re-run critical review"],
			originalRequestCheck: { satisfied: [], unsatisfied: [] },
			parseError: "Empty response",
		};
	}

	const lines = responseText.split(/\r?\n/);
	let verdict: CriticalReviewVerdict = "UNCERTAIN";
	let severity: CriticalReviewSeverity = "NONE";
	const findings: CriticalReviewFinding[] = [];
	const requiredActions: string[] = [];
	const satisfied: string[] = [];
	const unsatisfied: string[] = [];
	const promptComplianceItems: Array<{ requirement: string; planHandling?: string; status: "SATISFIED" | "UNSATISFIED" | "UNCERTAIN" | "YES" | "NO" }> = [];

	let initialJudgment: CriticalReviewVerdict = "UNCERTAIN";
	let revisedJudgment: CriticalReviewVerdict = "UNCERTAIN";
	const critiquePoints: string[] = [];

	const normalizeVerdict = (raw: string): CriticalReviewVerdict => {
		const upper = raw.toUpperCase().trim();
		if (upper === "APPROVE") return "APPROVE";
		if (upper === "PASS") return "PASS";
		if (upper === "REVISE") return "REVISE";
		if (upper === "FAIL") return "FAIL";
		if (upper === "UNCERTAIN") return "UNCERTAIN";
		return "UNCERTAIN";
	};

	for (let i = lines.length - 1; i >= 0; i--) {
		const line = lines[i].trim();
		const verdictMatch = line.match(/^VERDICT:\s*(APPROVE|PASS|REVISE|FAIL|UNCERTAIN)\b/i);
		if (verdictMatch) {
			verdict = normalizeVerdict(verdictMatch[1]);
			break;
		}
	}

	for (let i = lines.length - 1; i >= 0; i--) {
		const line = lines[i].trim();
		const sevMatch = line.match(/^SEVERITY:\s*(NONE|MINOR|MAJOR|CRITICAL)\b/i);
		if (sevMatch) {
			severity = sevMatch[1].toUpperCase() as CriticalReviewSeverity;
			break;
		}
	}

	let inPass1 = false;
	let inPass2 = false;
	let inReqCheck = false;
	let inFindings = false;
	let inActions = false;
	let currentIssue = "";
	let currentEvidence = "";

	for (const line of lines) {
		const trimmed = line.trim();

		if (trimmed.startsWith("PASS 1") || trimmed.startsWith("PASS 1 (") || trimmed.startsWith("PASS 1:")) {
			inPass1 = true; inPass2 = false; inReqCheck = false; inFindings = false; inActions = false;
			continue;
		}
		if (trimmed.startsWith("PASS 2") || trimmed.startsWith("PASS 2 (") || trimmed.startsWith("PASS 2:")) {
			inPass1 = false; inPass2 = true; inReqCheck = false; inFindings = false; inActions = false;
			continue;
		}
		if (
			trimmed.startsWith("PROMPT-COMPLIANCE:") ||
			trimmed.startsWith("PROMPT COMPLIANCE:") ||
			trimmed.startsWith("ORIGINAL-REQUEST CHECK:") ||
			trimmed.startsWith("ORIGINAL REQUEST CHECK:")
		) {
			inPass1 = false; inPass2 = false; inReqCheck = true; inFindings = false; inActions = false;
			continue;
		}
		if (trimmed.startsWith("FINDINGS:")) {
			inPass1 = false; inPass2 = false; inReqCheck = false; inFindings = true; inActions = false;
			continue;
		}
		if (
			trimmed.startsWith("REQUIRED REVISIONS:") ||
			trimmed.startsWith("REQUIRED REVISION:") ||
			trimmed.startsWith("REQUIRED ACTIONS:") ||
			trimmed.startsWith("REQUIRED ACTION:")
		) {
			if (currentIssue) {
				findings.push({ issue: currentIssue, evidence: currentEvidence || "(none specified)" });
				currentIssue = "";
				currentEvidence = "";
			}
			inPass1 = false; inPass2 = false; inReqCheck = false; inFindings = false; inActions = true;
			continue;
		}

		if (inPass1) {
			const m = trimmed.match(/Provisional Judgment:\s*(APPROVE|PASS|REVISE|FAIL|UNCERTAIN)\b/i);
			if (m) {
				initialJudgment = normalizeVerdict(m[1]);
			}
		}

		if (inPass2) {
			const m = trimmed.match(/Revised Judgment:\s*(APPROVE|PASS|REVISE|FAIL|UNCERTAIN)\b/i);
			if (m) {
				revisedJudgment = normalizeVerdict(m[1]);
			} else if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
				critiquePoints.push(trimmed.slice(2).trim());
			}
		}

		if (inReqCheck) {
			const statusMatch = trimmed.match(/(?:Status|Satisfied):\s*(SATISFIED|UNSATISFIED|UNCERTAIN|YES|NO)\b/i);
			if (statusMatch) {
				const statusVal = statusMatch[1].toUpperCase();
				const reqMatch = trimmed.match(/Requirement:\s*(.*?)(?=\s*->|\s*(?:Status|Satisfied):|$)/i);
				const planMatch = trimmed.match(/Plan Handling:\s*(.*?)(?=\s*->|\s*(?:Status|Satisfied):|$)/i);
				const reqText = reqMatch ? reqMatch[1].trim() : trimmed;
				const planHandling = planMatch ? planMatch[1].trim() : undefined;

				const isSat = statusVal === "SATISFIED" || statusVal === "YES";
				if (isSat) {
					satisfied.push(reqText);
				} else {
					unsatisfied.push(reqText);
				}
				promptComplianceItems.push({
					requirement: reqText,
					planHandling,
					status: statusVal as "SATISFIED" | "UNSATISFIED" | "UNCERTAIN" | "YES" | "NO",
				});
			}
		}

		if (inFindings) {
			if (trimmed.startsWith("- Issue:") || trimmed.startsWith("- issue:") || trimmed.startsWith("- Problem:") || trimmed.startsWith("- problem:")) {
				if (currentIssue) {
					findings.push({ issue: currentIssue, evidence: currentEvidence || "(none specified)" });
					currentIssue = "";
					currentEvidence = "";
				}
				currentIssue = trimmed.replace(/^-\s*(?:Issue|Problem):\s*/i, "").trim();
			} else if (trimmed.startsWith("Evidence:") || trimmed.startsWith("evidence:")) {
				currentEvidence = trimmed.replace(/^Evidence:\s*/i, "").trim();
			} else if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
				if (currentIssue) {
					findings.push({ issue: currentIssue, evidence: currentEvidence || "(none specified)" });
					currentIssue = "";
					currentEvidence = "";
				}
				currentIssue = trimmed.slice(2).trim();
			} else if (currentIssue && !currentEvidence) {
				currentIssue += " " + trimmed;
			} else if (currentEvidence) {
				currentEvidence += " " + trimmed;
			}
		}

		if (inActions) {
			if (trimmed.startsWith("- ") || trimmed.startsWith("* ") || /^\d+\.\s*/.test(trimmed)) {
				const act = trimmed.replace(/^[-*]\s*/, "").replace(/^\d+\.\s*/, "").trim();
				if (act) requiredActions.push(act);
			} else if (requiredActions.length > 0 && trimmed) {
				requiredActions[requiredActions.length - 1] += " " + trimmed;
			}
		}
	}

	if (currentIssue) {
		findings.push({ issue: currentIssue, evidence: currentEvidence || "(none specified)" });
	}

	if ((verdict === "REVISE" || verdict === "FAIL") && severity === "NONE") {
		severity = "MAJOR";
	}
	if ((verdict === "APPROVE" || verdict === "PASS") && severity !== "NONE" && severity !== "MINOR") {
		severity = "NONE";
	}

	const selfCritique: CriticalReviewSelfCritique = {
		initialJudgment,
		critique: critiquePoints,
		revisedJudgment,
	};

	return {
		verdict,
		severity,
		findings,
		requiredActions,
		originalRequestCheck: { satisfied, unsatisfied, items: promptComplianceItems },
		selfCritique,
	};
}
