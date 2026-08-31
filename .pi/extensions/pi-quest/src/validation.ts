import { SECTION_ALIASES } from "./constants.ts";
import { parseMarkdownSections } from "./markdown.ts";
import { ConsistencyAuditResult, MarkdownSection } from "./types.ts";
import { isPlaceholderOrEmpty } from "./utils.ts";

export function validateResearchPrerequisites(
	markdownContent: string,
	planConfidence?: string,
	allowLowConfidence = false,
	planConfidenceReason?: string,
): {
	valid: boolean;
	missingSections: string[];
	confidenceIssue?: string;
} {
	const sections = parseMarkdownSections(markdownContent);
	const missingSections: string[] = [];

	const requiredEpistemicKeys: Array<{ key: string; label: string }> = [
		{ key: "current understanding", label: "Current Understanding" },
		{ key: "key assumptions", label: "Key Assumptions" },
		{ key: "research findings", label: "Research Findings" },
		{ key: "open questions & uncertainties", label: "Open Questions & Uncertainties" },
		{ key: "plan", label: "Plan" },
		{ key: "plan confidence", label: "Plan Confidence" },
		{ key: "exact next action", label: "Exact Next Action" },
	];

	for (const req of requiredEpistemicKeys) {
		const aliases = [req.key, ...(SECTION_ALIASES[req.key] || [])];
		let foundSec: MarkdownSection | undefined;
		for (const alias of aliases) {
			const s = sections.get(alias);
			if (s && !isPlaceholderOrEmpty(s.body)) {
				foundSec = s;
				break;
			}
		}
		if (!foundSec) {
			missingSections.push(req.label);
		}
	}

	let confidenceIssue: string | undefined;
	const confSec = sections.get("plan confidence") || sections.get("confidence");
	const confBody = confSec?.body || "";
	const confText = (planConfidence || confBody).toLowerCase();
	const hasLow = confText.includes("low");
	const hasMediumOrHigh = confText.includes("medium") || confText.includes("high");

	if (hasLow && !hasMediumOrHigh) {
		const reasonText = (planConfidenceReason || confBody).trim();
		const reasonSubstantive =
			reasonText.length > 0 &&
			(reasonText.includes("Reason:") || reasonText.includes("justif") || reasonText.includes("acceptable") || (planConfidenceReason && planConfidenceReason.trim().length > 5)) &&
			!isPlaceholderOrEmpty(reasonText);

		if (!allowLowConfidence || !reasonSubstantive) {
			confidenceIssue = "Plan confidence is 'low'. To complete research with low confidence, you must pass allowLowConfidence: true AND provide explicit justification in planConfidenceReason.";
		}
	}

	return {
		valid: missingSections.length === 0 && !confidenceIssue,
		missingSections,
		confidenceIssue,
	};
}

export function auditQuestConsistency(
	markdownContent: string,
	options?: {
		recentModifiedFiles?: string[];
		strict?: boolean;
	},
): ConsistencyAuditResult {
	const issues: string[] = [];
	const warnings: string[] = [];
	if (!markdownContent) {
		return { consistent: true, issues, warnings };
	}

	const sections = parseMarkdownSections(markdownContent);

	const getSecBody = (key: string): string => {
		const aliases = [key, ...(SECTION_ALIASES[key] || [])];
		for (const a of aliases) {
			const s = sections.get(a);
			if (s && s.body) return s.body;
		}
		return "";
	};

	const completedBody = getSecBody("completed");
	const reassessmentBody = getSecBody("latest reassessment");
	const nextActionBody = getSecBody("exact next action");
	const remainingBody = getSecBody("remaining work");
	const filesModifiedBody = getSecBody("files modified");
	const testStatusBody = getSecBody("test / build status");
	const planVersionBody = getSecBody("plan version");
	const planRevisionsBody = getSecBody("plan revisions");
	const assumptionsBody = getSecBody("key assumptions");
	const uncertaintiesBody = getSecBody("open questions & uncertainties");
	const currentStatusBody = getSecBody("current status");

	const hasCompleted = !isPlaceholderOrEmpty(completedBody);
	const hasReassessment = !isPlaceholderOrEmpty(reassessmentBody);
	const hasFilesModified = !isPlaceholderOrEmpty(filesModifiedBody);
	const hasTestStatus = !isPlaceholderOrEmpty(testStatusBody);
	const hasRemaining = !isPlaceholderOrEmpty(remainingBody);

	// Helper to extract non-comment, non-empty lines
	const extractLines = (body: string): string[] => {
		return body
			.split(/\r?\n/)
			.map((l) => l.replace(/^>\s*/, "").replace(/^[-*]\s*/, "").replace(/^\[[ xX]\]\s*/, "").trim())
			.filter((l) => l && l !== "-" && !l.startsWith(">") && !l.startsWith("not started ·") && !isPlaceholderOrEmpty(l));
	};

	// Helper to extract file references (e.g. i18n_dict.h, song.c, etc.)
	const extractFileNames = (text: string): string[] => {
		const matches = text.match(/\b[a-zA-Z0-9_\-\.\/]+\.(?:c|h|ts|js|json|mk|sh|md|txt|html|css|wasm)\b/gi) || [];
		const cleaned: string[] = [];
		for (const m of matches) {
			const norm = m.toLowerCase().replace(/^\.\//, "");
			if (!norm.includes("quest") && !cleaned.includes(norm)) {
				cleaned.push(norm);
			}
		}
		return cleaned;
	};

	// Helper to extract key identifiers (like I18N_LOCALE_EN, constants, etc.)
	const extractIdentifiers = (text: string): string[] => {
		const matches = text.match(/\b[A-Z0-9_]{3,}\b/g) || [];
		const cleaned: string[] = [];
		for (const m of matches) {
			if (!["THE", "AND", "FOR", "NOT", "YES", "ALL", "SET", "NEW", "ADD", "RUN", "GET", "PUT", "DEL"].includes(m) && !cleaned.includes(m)) {
				cleaned.push(m);
			}
		}
		return cleaned;
	};

	const completedLines = extractLines(completedBody);
	const reassessmentLines = extractLines(reassessmentBody);
	const remainingLines = extractLines(remainingBody);
	const nextActionText = nextActionBody.replace(/^>\s*/gm, "").trim();

	// 1. Check: Exact Next Action repeats completed action or reassessment conclusion
	if (nextActionText && !isPlaceholderOrEmpty(nextActionText)) {
		const nextActionLower = nextActionText.toLowerCase();
		const nextActionFiles = extractFileNames(nextActionText);
		const nextActionIdents = extractIdentifiers(nextActionText);

		// Verb mapping from past to imperative/present
		const pastToPresentVerbs: Record<string, string[]> = {
			"added": ["add", "adding", "define", "include"],
			"created": ["create", "creating", "make"],
			"implemented": ["implement", "implementing", "write"],
			"fixed": ["fix", "fixing", "resolve"],
			"defined": ["define", "defining", "add"],
			"updated": ["update", "updating", "edit", "modify"],
			"modified": ["modify", "modifying", "update", "edit"],
			"configured": ["configure", "configuring", "set up"],
			"registered": ["register", "registering"],
			"removed": ["remove", "removing", "delete"],
			"deleted": ["delete", "deleting", "remove"],
			"built": ["build", "building", "compile"],
			"compiled": ["compile", "compiling", "build"],
			"verified": ["verify", "verifying", "check"],
			"tested": ["test", "testing"],
		};

		const completedStatements = [...completedLines, ...reassessmentLines];
		for (const stmt of completedStatements) {
			const stmtLower = stmt.toLowerCase();
			const stmtFiles = extractFileNames(stmt);
			const stmtIdents = extractIdentifiers(stmt);

			const sharedFiles = stmtFiles.filter((f) => nextActionFiles.includes(f));
			const sharedIdents = stmtIdents.filter((id) => nextActionIdents.includes(id));

			let verbConflict = false;
			for (const [pastVerb, presVerbs] of Object.entries(pastToPresentVerbs)) {
				if (stmtLower.includes(pastVerb)) {
					for (const presVerb of presVerbs) {
						if (nextActionLower.startsWith(presVerb) || nextActionLower.includes(` ${presVerb} `)) {
							verbConflict = true;
							break;
						}
					}
				}
				if (verbConflict) break;
			}

			// If verb conflict and shared file or identifier, or phrase match
			if (
				(verbConflict && (sharedFiles.length > 0 || sharedIdents.length > 0)) ||
				(stmtLower.length > 15 && nextActionLower.includes(stmtLower.replace(/^added\s+|^implemented\s+|^fixed\s+/, "")))
			) {
				issues.push(
					`Exact Next Action ('${nextActionText.slice(0, 80)}') repeats work already recorded as completed in Latest Reassessment / Completed ('${stmt.slice(0, 80)}'). Advance Exact Next Action to the live next step (e.g. rerun unit tests, verify integration, or proceed to next stage).`
				);
				break;
			}
		}
	}

	// 2. Check: Files Modified empty or omitting files mentioned in completed / reassessment / session
	const filesMentionedInCompleted: string[] = [];
	for (const stmt of [...completedLines, ...reassessmentLines]) {
		for (const f of extractFileNames(stmt)) {
			if (!filesMentionedInCompleted.includes(f)) {
				filesMentionedInCompleted.push(f);
			}
		}
	}
	const sessionFiles = (options?.recentModifiedFiles || []).map((f) => f.toLowerCase().replace(/^\.\//, ""));
	const allExpectedFiles = Array.from(new Set([...filesMentionedInCompleted, ...sessionFiles]));

	if (allExpectedFiles.length > 0) {
		if (!hasFilesModified) {
			issues.push(
				`Files Modified is empty or placeholder, but substantive changes to [${allExpectedFiles.join(", ")}] were recorded in Latest Reassessment / Completed / tool history. List modified files under Files Modified.`
			);
		} else {
			const modFilesList = extractFileNames(filesModifiedBody);
			const missingFiles = allExpectedFiles.filter((f) => !modFilesList.includes(f) && !filesModifiedBody.toLowerCase().includes(f));
			if (missingFiles.length > 0 && filesMentionedInCompleted.some((f) => missingFiles.includes(f))) {
				issues.push(
					`Files Modified omits file(s) [${missingFiles.join(", ")}] that were modified according to Completed / Reassessment. Include all modified files in Files Modified.`
				);
			}
		}
	}

	// 3. Check: Completed empty despite completed implementation recorded in Reassessment
	if (!hasCompleted && hasReassessment) {
		const actionVerbs = ["added", "implemented", "created", "fixed", "defined", "updated", "modified", "configured"];
		const hasCompletedAction = reassessmentLines.some((l) => actionVerbs.some((v) => l.toLowerCase().includes(v)));
		if (hasCompletedAction) {
			issues.push(
				"Completed section is empty despite completed implementation recorded in Latest Reassessment. Move completed work into Completed and remove from Remaining Work."
			);
		}
	}

	// 4. Check: Plan Version > 1 with only Initial plan in Plan Revisions
	const planVersionNum = Number.parseInt(planVersionBody.replace(/\D/g, ""), 10) || 1;
	if (planVersionNum > 1) {
		const revBody = planRevisionsBody.trim();
		const isOnlyInitial =
			isPlaceholderOrEmpty(revBody) ||
			revBody === "- Initial plan formulated." ||
			revBody === "Initial plan formulated." ||
			revBody === "- Initial plan formulated" ||
			revBody === "-";
		if (isOnlyInitial) {
			issues.push(
				`Plan Version is ${planVersionNum} but Plan Revisions only lists initial plan formulation. Plan Revisions must explain what triggered the revision (Previous plan -> Evidence/contradiction -> New understanding -> Revised plan).`
			);
		}
	}

	// 5. Check: User confirmation claimed as eliminating all engineering uncertainties
	const uncLower = uncertaintiesBody.toLowerCase();
	if (
		uncLower.includes("none remaining - user confirmed") ||
		uncLower.includes("no uncertainties remaining - user confirmed") ||
		uncLower.includes("none - user confirmed") ||
		uncLower.includes("user confirmed language defaults") ||
		uncLower.includes("user confirmed negotiation strategy")
	) {
		const hasUncheckedAssumptions = assumptionsBody.includes("- [ ]") || assumptionsBody.includes("unverified");
		if (hasUncheckedAssumptions) {
			issues.push(
				"Open Questions & Uncertainties claims no uncertainties based solely on user requirement confirmation, but unverified engineering assumptions remain in Key Assumptions. User requirement decisions must be separated from verified engineering facts."
			);
		}
	}

	// 6. Check: Test / Build Status reflects recent modifications
	if ((hasFilesModified || (options?.recentModifiedFiles && options.recentModifiedFiles.length > 0)) && !hasTestStatus) {
		issues.push(
			"Files were modified but Test / Build Status is empty. Test / Build Status must explicitly state whether tests have been run or are pending rerun after changes (e.g. '- Unit tests pending rerun after file edits')."
		);
	}

	// 7. Check: Completed items duplicated as open in Remaining Work
	if (hasCompleted && hasRemaining) {
		for (const comp of completedLines) {
			const compClean = comp.toLowerCase().replace(/^(added|implemented|created|fixed|defined)\s+/, "");
			for (const rem of remainingLines) {
				const remClean = rem.toLowerCase().replace(/^(add|implement|create|fix|define)\s+/, "");
				if (
					(compClean === remClean ||
						(compClean.length > 10 && remClean.includes(compClean)) ||
						(remClean.length > 10 && compClean.includes(remClean))) &&
					compClean.length > 5
				) {
					issues.push(
						`Remaining Work still lists item ('${rem.slice(0, 60)}') that is already recorded in Completed. Remove it from Remaining Work or mark it completed.`
					);
				}
			}
		}
	}

	// 8. Check: Current Status vs Remaining Work alignment
	const statusLower = currentStatusBody.toLowerCase();
	const isStatusTemplatePlaceholder = statusLower.includes("research pending ·") || isPlaceholderOrEmpty(currentStatusBody);
	if (!isStatusTemplatePlaceholder && (statusLower.startsWith("- [x] done") || statusLower === "done" || statusLower.startsWith("done") || statusLower === "completed" || statusLower.startsWith("- [x] completed"))) {
		if (remainingBody.includes("- [ ]")) {
			issues.push(
				"Current Status is marked done/completed, but Remaining Work still contains unchecked items."
			);
		}
	}

	return {
		consistent: issues.length === 0,
		issues,
		warnings,
	};
}
