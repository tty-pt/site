import { readFile, writeFile } from "node:fs/promises";
import { QuestErrorCode } from "./constants.ts";
import { syncImplementationPermission } from "./gates.ts";
import { logCriticalReviewTransition, logEvent, logToolActivity } from "./logging.ts";
import { parseMarkdownSections, spliceMarkdownSections } from "./markdown.ts";
import { formatAgentErrorMessage, reportAgentError, sendInternalAgentMessage } from "./messaging.ts";
import { fileExists, questPath, resolveQuestRecordBySlug } from "./paths.ts";
import { persist, verifyAndMarkSaved } from "./persistence.ts";
import { parseOriginalRequest, parseRefinements } from "./reconstruction.ts";
import { triggerReassessment } from "./research.ts";
import { getActiveContext, getSessionId, getState, isRootQuest, state } from "./state.ts";
import {
	CriticalReviewFinding,
	CriticalReviewKind,
	CriticalReviewOriginalRequestCheck,
	CriticalReviewSelfCritique,
	CriticalReviewSeverity,
	CriticalReviewState,
	CriticalReviewVerdict,
	ExtensionAPI,
	ExtensionContext,
	StoredState,
} from "./types.ts";

export interface CriticalReviewOptions {
	kind: CriticalReviewKind;
	questSlug?: string;
	agent?: string;
	force?: boolean;
	rebuttal?: string;
	subagentRunner?: (task: string, options?: any) => Promise<string | { text?: string; content?: any; isError?: boolean; error?: any }>;
}

export interface CriticalReviewExecutionResult {
	success: boolean;
	available: boolean;
	skipped?: boolean;
	review?: CriticalReviewState;
	error?: string;
}

// In-memory or custom runner registry for dependency injection & testing
let customSubagentRunner: ((task: string, options?: any) => Promise<any>) | null = null;

export function setCustomSubagentRunner(runner: ((task: string, options?: any) => Promise<any>) | null): void {
	customSubagentRunner = runner;
}

export type SubagentExecutorFn = (
	task: string,
	options?: { agent?: string; isCriticalReview?: boolean; reviewKind?: string; tools?: string[] }
) => Promise<string | { text?: string; content?: any; isError?: boolean; error?: any }>;

export function isSubagentToolRegistered(pi?: ExtensionAPI, ctx?: ExtensionContext): boolean {
	if (customSubagentRunner) return true;
	if (typeof pi?.getAllTools === "function") {
		try {
			const tools = pi.getAllTools();
			if (Array.isArray(tools)) {
				return tools.some((t: any) => t?.name === "subagent");
			}
		} catch {}
	}
	return false;
}

export function resolveSubagentExecutor(pi?: ExtensionAPI, ctx?: ExtensionContext): SubagentExecutorFn | null {
	if (customSubagentRunner) return customSubagentRunner;

	// Subagent extension supported bridge mechanism (pi.events slash bridge registered by pi-cohort)
	if (pi?.events && typeof pi.events.on === "function" && typeof pi.events.emit === "function") {
		if (!isSubagentToolRegistered(pi, ctx)) {
			return null;
		}
		return async (task: string, options?: any) => {
			return new Promise((resolve, reject) => {
				const requestId = `slash_subagent_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;
				let unsubscribe: (() => void) | void;
				const timeoutTimer = setTimeout(() => {
					cleanup();
					reject(new Error("Subagent execution timed out on event bridge"));
				}, 60000);

				const cleanup = () => {
					clearTimeout(timeoutTimer);
					if (typeof unsubscribe === "function") unsubscribe();
				};

				unsubscribe = pi.events!.on("subagent:slash:response", (data: any) => {
					if (data && data.requestId === requestId) {
						cleanup();
						if (data.isError) {
							reject(new Error(data.errorText || data.result?.content?.[0]?.text || "Subagent execution error"));
						} else {
							resolve(data.result);
						}
					}
				});

				pi.events!.emit("subagent:slash:request", {
					requestId,
					params: {
						agent: options?.agent || "reviewer",
						task,
						isCriticalReview: true,
						reviewKind: options?.reviewKind || "direction",
						tools: ["read", "grep", "find", "ls"],
						async: false,
						clarify: false,
					},
				});
			});
		};
	}

	return null;
}

export function isSubagentAvailable(pi?: ExtensionAPI, ctx?: ExtensionContext): boolean {
	if (customSubagentRunner) return true;
	const registered = isSubagentToolRegistered(pi, ctx);
	if (!registered) return false;
	const executor = resolveSubagentExecutor(pi, ctx);
	return executor !== null;
}

export async function extractQuestReviewContext(slugOrQid: string, s?: StoredState): Promise<{
	originalRequest: string;
	refinements: string[];
	currentUnderstanding: string;
	keyAssumptions: string;
	openQuestions: string;
	plan: string;
	planConfidence: string;
	planRevisions: string;
	findings: string;
	filesModified: string;
	testStatus: string;
	executionSnapshot: string;
	exactNextAction: string;
	remainingWork: string;
	status: string;
}> {
	const activeState = s || state;
	let path = questPath(slugOrQid);
	if (!(await fileExists(path))) {
		const rec = await resolveQuestRecordBySlug(slugOrQid);
		if (rec) path = rec.path;
	}

	let content = "";
	if (await fileExists(path)) {
		try {
			content = await readFile(path, "utf8");
		} catch {}
	}

	const sections = parseMarkdownSections(content);
	const getSec = (key: string): string => {
		const sec = sections.get(key.toLowerCase());
		if (!sec || !sec.body || sec.body.trim().startsWith(">")) return "";
		return sec.body.trim();
	};

	let originalRequest = parseOriginalRequest(sections);
	if (!originalRequest && activeState.prompts && activeState.prompts.length > 0) {
		originalRequest = activeState.prompts[0];
	}

	let refinements = parseRefinements(sections);
	if (refinements.length === 0 && Array.isArray(activeState.refinements) && activeState.refinements.length > 0) {
		refinements = [...activeState.refinements];
	}

	return {
		originalRequest: originalRequest || "(No verbatim prompt recorded)",
		refinements,
		currentUnderstanding: getSec("current understanding") || getSec("understanding"),
		keyAssumptions: getSec("key assumptions") || getSec("assumptions"),
		openQuestions: getSec("open questions & uncertainties") || getSec("open questions") || getSec("uncertainties"),
		plan: getSec("plan") || getSec("detailed multi-stage execution plan"),
		planConfidence: getSec("plan confidence") || activeState.planConfidence || "medium",
		planRevisions: getSec("plan revisions") || getSec("plan revision history"),
		findings: getSec("research findings") || getSec("important findings") || getSec("in-depth analysis & findings"),
		filesModified: getSec("files touched") || getSec("files modified"),
		testStatus: getSec("test / build status") || getSec("test status") || getSec("build & test status"),
		executionSnapshot: getSec("execution snapshot") || getSec("execution state"),
		exactNextAction: getSec("exact next action") || getSec("next recommended step") || getSec("next step"),
		remainingWork: getSec("remaining work") || getSec("remaining tasks"),
		status: getSec("current status") || getSec("status"),
	};
}

export function buildCriticalReviewPrompt(
	kind: CriticalReviewKind,
	questSlug: string,
	context: Awaited<ReturnType<typeof extractQuestReviewContext>>,
	rebuttal?: string,
): string {
	const header = kind === "direction"
		? `[CRITICAL REVIEW: DIRECTION REVIEW for Quest '${questSlug}']`
		: `[CRITICAL REVIEW: FINAL ACCEPTANCE REVIEW for Quest '${questSlug}']`;

	const specificGoal = kind === "direction"
		? `REVIEW MODE: Direction Review (In-flight during execution)
CORE QUESTION: "Given what we currently know, are we solving the right problem and going in the right direction?"
Note: It is NOT expected that the implementation is complete. Focus on problem understanding, important assumptions, current plan, architecture/direction, evidence supporting the plan, dismissed alternatives, emerging contradictions, and unnecessary complexity.
Identify the SINGLE MOST DANGEROUS ASSUMPTION behind the current direction. If unverified -> UNCERTAIN. If wrong -> FAIL.`
		: `REVIEW MODE: Final Acceptance Review (Root quest completion gate)
CORE QUESTION: "Did the actual resulting work fulfill the user's original request?"
Note: This is an acceptance review against reality, not merely a code review. You must directly compare the exact recorded original request against actual repository implementation and evidence.
Produce an explicit table/list: Original requirement -> implementation/evidence -> satisfied? (YES / NO / UNCERTAIN).`;

	const rebuttalSection = rebuttal ? `\n\nMAIN AGENT EVIDENCE-BASED REBUTTAL:\n${rebuttal}\nRe-evaluate your prior findings in light of this specific evidence.\n` : "";

	const refinementsText = context.refinements.length > 0
		? context.refinements.map((r, i) => `${i + 1}. ${r}`).join("\n")
		: "(None)";

	return `${header}

${specificGoal}

REVIEWER MINDSET & INSTRUCTIONS:
Be an independent, technically severe reviewer. Assume the current reasoning may be wrong. Look for incorrect assumptions, premature closure, confirmation bias, requirement drift, unnecessary complexity, broken abstractions, regressions, incomplete edge cases, and conclusions unsupported by evidence. Prefer the simplest design that actually satisfies the user's objective. Do not praise the work unless evidence warrants it.

INDEPENDENT INVESTIGATION REQUIREMENT:
Do not trust the main agent's summary or claims. Verify important claims by inspecting the actual repository and, where useful, performing targeted read-only checks. Evidence from the repository takes precedence over assertions in the main agent's narrative.

MANDATORY TWO-PASS SELF-ATTACK (You must execute both passes internally before returning your verdict):
PASS 1: Independently inspect reality and form a provisional judgment.
PASS 2: Try to prove that judgment wrong! Identify your own assumptions, identify evidence that would invalidate your judgment, and reconsider your conclusion. Only after this self-critique should you finalize your verdict.

INSPECT FOR SELF-DECEPTION:
Explicitly inspect for: premature 'done', confirmation bias, cherry-picked verification, unverified assumptions, scope drift, requirement substitution, temporary workaround treated as solution, test passing interpreted too broadly, known contradiction rationalized away, overengineering, parallel/duplicate sources of truth, complexity created to compensate for an earlier mistake.

DO NOT INVENT REQUIREMENTS:
Distinguish: user requirement, project/technical constraint, reviewer preference.
Only user requirements and technical constraints can justify blocking progress or issuing a FAIL verdict. Do not fail for stylistic preferences unless there is a concrete technical consequence.

--- CONTEXT PROVIDED FOR REVIEW ---
ORIGINAL USER REQUEST (Primary Acceptance Criterion):
${context.originalRequest}

USER REFINEMENTS (Supplementary Context):
${refinementsText}

CURRENT QUEST STATUS:
${context.status || "(in progress)"}

CURRENT UNDERSTANDING:
${context.currentUnderstanding || "(none provided)"}

KEY ASSUMPTIONS:
${context.keyAssumptions || "(none provided)"}

CURRENT PLAN:
${context.plan || "(none provided)"}

PLAN CONFIDENCE:
${context.planConfidence}

IMPORTANT PLAN REVISIONS:
${context.planRevisions || "(none)"}

OPEN QUESTIONS & UNCERTAINTIES:
${context.openQuestions || "(none)"}

RELEVANT FINDINGS:
${context.findings || "(none)"}

FILES CHANGED / TOUCHED:
${context.filesModified || "(none)"}

TEST / BUILD STATUS:
${context.testStatus || "(none)"}

RECENT EXECUTION SNAPSHOT:
${context.executionSnapshot || "(none)"}

EXACT NEXT ACTION:
${context.exactNextAction || "(none)"}
${rebuttalSection}
--- END CONTEXT ---

OUTPUT FORMAT REQUIREMENT:
Your response MUST end with the following structured format:

PASS 1 (Provisional Inspection):
[Provisional Judgment: PASS | FAIL | UNCERTAIN]
[Provisional Summary]

PASS 2 (Self-Critique & Falsification):
- Own assumptions tested: ...
- Evidence evaluated: ...
- Revised Judgment: PASS | FAIL | UNCERTAIN

ORIGINAL-REQUEST CHECK:
- Requirement: <req 1> -> Evidence: <evidence> -> Satisfied: YES | NO | UNCERTAIN

VERDICT: PASS | FAIL | UNCERTAIN
SEVERITY: NONE | MINOR | MAJOR | CRITICAL

FINDINGS:
- Issue: <concrete issue>
  Evidence: <concrete evidence from repository or logs>

REQUIRED ACTIONS:
- <concrete remediation action or targeted investigation required>
`;
}

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

	let initialJudgment: CriticalReviewVerdict = "UNCERTAIN";
	let revisedJudgment: CriticalReviewVerdict = "UNCERTAIN";
	const critiquePoints: string[] = [];

	// Parse Verdict & Severity
	for (let i = lines.length - 1; i >= 0; i--) {
		const line = lines[i].trim();
		const verdictMatch = line.match(/^VERDICT:\s*(PASS|FAIL|UNCERTAIN)\b/i);
		if (verdictMatch) {
			verdict = verdictMatch[1].toUpperCase() as CriticalReviewVerdict;
			break;
		}
	}

	for (let i = lines.length - 1; i >= 0; i--) {
		const line = lines[i].trim();
		const severityMatch = line.match(/^SEVERITY:\s*(NONE|MINOR|MAJOR|CRITICAL)\b/i);
		if (severityMatch) {
			severity = severityMatch[1].toUpperCase() as CriticalReviewSeverity;
			break;
		}
	}

	// Parse Pass 1 & Pass 2 self-critique
	let inPass1 = false;
	let inPass2 = false;
	for (const line of lines) {
		const trimmed = line.trim();
		if (trimmed.startsWith("PASS 1")) {
			inPass1 = true;
			inPass2 = false;
			continue;
		}
		if (trimmed.startsWith("PASS 2")) {
			inPass1 = false;
			inPass2 = true;
			continue;
		}
		if (trimmed.startsWith("ORIGINAL-REQUEST CHECK") || trimmed.startsWith("VERDICT:")) {
			inPass1 = false;
			inPass2 = false;
		}

		if (inPass1) {
			const m = trimmed.match(/(?:Provisional Judgment|Initial Judgment):\s*(PASS|FAIL|UNCERTAIN)/i);
			if (m) initialJudgment = m[1].toUpperCase() as CriticalReviewVerdict;
		}
		if (inPass2) {
			const m = trimmed.match(/Revised Judgment:\s*(PASS|FAIL|UNCERTAIN)/i);
			if (m) revisedJudgment = m[1].toUpperCase() as CriticalReviewVerdict;
			if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
				critiquePoints.push(trimmed.slice(2).trim());
			}
		}
	}

	if (revisedJudgment === "UNCERTAIN" && verdict !== "UNCERTAIN") {
		revisedJudgment = verdict;
	}
	if (initialJudgment === "UNCERTAIN" && revisedJudgment !== "UNCERTAIN") {
		initialJudgment = revisedJudgment;
	}

	// Parse Original Request Check
	let inReqCheck = false;
	let inFindings = false;
	let inActions = false;

	let currentIssue = "";
	let currentEvidence = "";

	for (const line of lines) {
		const trimmed = line.trim();
		if (trimmed.startsWith("ORIGINAL-REQUEST CHECK:")) {
			inReqCheck = true;
			inFindings = false;
			inActions = false;
			continue;
		}
		if (trimmed.startsWith("FINDINGS:")) {
			inReqCheck = false;
			inFindings = true;
			inActions = false;
			continue;
		}
		if (trimmed.startsWith("REQUIRED ACTIONS:")) {
			inReqCheck = false;
			inFindings = false;
			inActions = true;
			continue;
		}
		if (trimmed.startsWith("VERDICT:") || trimmed.startsWith("SEVERITY:")) {
			inReqCheck = false;
			inFindings = false;
			inActions = false;
		}

		if (inReqCheck) {
			if (trimmed.startsWith("- ") || trimmed.startsWith("* ")) {
				const item = trimmed.slice(2).trim();
				if (item.toLowerCase().includes("satisfied: yes") || item.toLowerCase().includes("satisfied: true") || item.includes("-> YES")) {
					satisfied.push(item);
				} else if (item.toLowerCase().includes("satisfied: no") || item.toLowerCase().includes("satisfied: false") || item.includes("-> NO")) {
					unsatisfied.push(item);
				} else if (item.toLowerCase().includes("satisfied: uncertain") || item.includes("-> UNCERTAIN")) {
					unsatisfied.push(item);
				}
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

	// Stylistic preference check: if verdict is FAIL but no critical technical issues exist and finding is purely stylistic
	if (verdict === "FAIL" && severity === "NONE") {
		severity = "MAJOR";
	}
	if (verdict === "PASS" && severity !== "NONE" && severity !== "MINOR") {
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
		originalRequestCheck: { satisfied, unsatisfied },
		selfCritique,
	};
}

export function isCriticalReviewValidForCompletion(targetState?: StoredState): boolean {
	const s = targetState || state;
	if (s.dirty) return false;
	const rev = s.lastCriticalReview;
	if (!rev) return false;
	if (rev.kind !== "final_acceptance") return false;
	if (rev.verdict !== "PASS") return false;

	// Invalidation checks: saveHash, saveCount, or planVersion mismatch
	const currentPlanVersion = s.planVersion || 1;
	const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : null);

	if (rev.reviewedStateVersion.planVersion !== currentPlanVersion) {
		return false;
	}
	if (currentHash && rev.reviewedStateVersion.saveHash && rev.reviewedStateVersion.saveHash !== currentHash) {
		return false;
	}
	if (s.saveCount && rev.reviewedStateVersion.saveCount && rev.reviewedStateVersion.saveCount !== s.saveCount) {
		return false;
	}

	return true;
}

export async function runCriticalReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	options: CriticalReviewOptions,
): Promise<CriticalReviewExecutionResult> {
	const c = getActiveContext(ctx);
	const targetState = getState(c);
	const slug = options.questSlug || targetState.active || "quest";
	const questId = targetState.questId || slug;
	const sessionId = getSessionId(c);
	const correlationId = `rev_${Date.now().toString(36)}_${Math.random().toString(36).slice(2, 6)}`;

	// 1. Check Subagent Registration & Executability
	const registered = Boolean(options.subagentRunner) || Boolean(customSubagentRunner) || isSubagentToolRegistered(pi, ctx);
	if (!registered) {
		// Genuinely unavailable -> silently skip
		return { success: true, available: false, skipped: true };
	}

	const executor = options.subagentRunner || resolveSubagentExecutor(pi, ctx);
	if (!executor) {
		// Tool is registered/listed, but no execution adapter exists -> report and log CRITICAL_REVIEW_UNAVAILABLE
		logCriticalReviewTransition("CRITICAL_REVIEW_UNAVAILABLE", "critical review unavailable: subagent tool not executable", {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			reason: "subagent_tool_not_executable",
		});
		return {
			success: false,
			available: false,
			skipped: false,
			error: "subagent_tool_not_executable",
		};
	}

	// 2. Throttle / Deduplication Check
	const currentPlanVersion = targetState.planVersion || 1;
	const currentHash = targetState.lastSavedHash || (targetState.saveGeneration ? targetState.saveGeneration.hash : null);
	const currentSaveCount = targetState.saveCount || 0;

	const attemptKey = `${slug}:${options.kind}:v${currentPlanVersion}:h${currentHash || currentSaveCount}`;
	if (!targetState.criticalReviewAttempts) {
		targetState.criticalReviewAttempts = {};
	}
	const attempts = (targetState.criticalReviewAttempts[attemptKey] || 0) + 1;
	targetState.criticalReviewAttempts[attemptKey] = attempts;

	if (!options.force && attempts > 3) {
		logCriticalReviewTransition("CRITICAL_REVIEW_FAILED", `critical review attempt limit reached (attempts=${attempts})`, {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			severity: "CRITICAL",
			reason: "Review loop bound exceeded for state version",
		});
		return {
			success: false,
			available: true,
			error: "Review loop bound reached for current state version",
		};
	}

	// 3. Log Review Request & Start
	logCriticalReviewTransition("CRITICAL_REVIEW_REQUESTED", `critical ${options.kind} review requested`, {
		quest: slug,
		questId,
		sessionId,
		reviewId: correlationId,
		reviewKind: options.kind,
	});

	logCriticalReviewTransition("CRITICAL_REVIEW_STARTED", `critical ${options.kind} review started`, {
		quest: slug,
		questId,
		sessionId,
		reviewId: correlationId,
		reviewKind: options.kind,
	});

	// 4. Build Review Prompt
	const reviewContext = await extractQuestReviewContext(slug, targetState);
	const prompt = buildCriticalReviewPrompt(options.kind, slug, reviewContext, options.rebuttal);

	// 5. Execute Subagent Invocation
	let rawResponseText = "";
	targetState.inCriticalReview = true;
	try {
		const res = await executor(prompt, {
			agent: options.agent || "reviewer",
			isCriticalReview: true,
			reviewKind: options.kind,
			tools: ["read", "grep", "find", "ls"],
		});
		rawResponseText = typeof res === "string" ? res : res?.text || (Array.isArray(res?.content) ? res.content.map((c: any) => c.text || "").join("\n") : "");
		
		logToolActivity("subagent", "success", {
			quest: slug,
			questId,
			sessionId,
			phase: "verification",
			command: `[critical review] ${options.kind}`,
			turn: targetState.currentTurn,
			correlationId,
		});
	} catch (err: any) {
		logToolActivity("subagent", "failure", {
			quest: slug,
			questId,
			sessionId,
			phase: "verification",
			command: `[critical review] ${options.kind}`,
			turn: targetState.currentTurn,
			correlationId,
			reason: err?.message,
		});
		logCriticalReviewTransition("CRITICAL_REVIEW_ERROR", `critical review execution error: ${err?.message || "subagent failure"}`, {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			error: err?.message,
		});
		reportAgentError(
			pi,
			ctx,
			`Critical review execution failed: ${err?.message || "Subagent execution error"}`,
			{
				code: QuestErrorCode.CRITICAL_REVIEW_ERROR,
				correlationId,
				requiredNextAction: "Investigate subagent execution error and retry critical review.",
				details: { Quest: slug, ReviewKind: options.kind },
			},
		);
		return {
			success: false,
			available: true,
			error: err?.message || "Subagent execution error",
		};
	} finally {
		targetState.inCriticalReview = false;
	}

	// 6. Parse Response
	const parsed = parseCriticalReviewResponse(rawResponseText);

	// 7. Log Self-Critique Lifecycle
	if (parsed.selfCritique) {
		logCriticalReviewTransition("SELF_CRITIQUE_STARTED", "reviewer self-critique pass started", {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			from: parsed.selfCritique.initialJudgment,
		});
		if (parsed.selfCritique.initialJudgment !== parsed.selfCritique.revisedJudgment) {
			logCriticalReviewTransition("SELF_CRITIQUE_REVISED", `reviewer self-critique revised verdict from ${parsed.selfCritique.initialJudgment} to ${parsed.selfCritique.revisedJudgment}`, {
				quest: slug,
				questId,
				sessionId,
				reviewId: correlationId,
				reviewKind: options.kind,
				from: parsed.selfCritique.initialJudgment,
				to: parsed.selfCritique.revisedJudgment,
			});
		}
	}

	// 8. Construct Review State Object
	const reviewState: CriticalReviewState = {
		id: correlationId,
		questId,
		kind: options.kind,
		reviewedStateVersion: {
			planVersion: currentPlanVersion,
			saveHash: currentHash,
			saveCount: currentSaveCount,
		},
		verdict: parsed.verdict,
		severity: parsed.severity,
		findings: parsed.findings,
		requiredActions: parsed.requiredActions,
		originalRequestCheck: parsed.originalRequestCheck,
		selfCritique: parsed.selfCritique,
		resolved: parsed.verdict === "PASS",
		timestamp: Date.now(),
		correlationId,
	};

	targetState.lastCriticalReview = reviewState;
	if (!Array.isArray(targetState.criticalReviews)) {
		targetState.criticalReviews = [];
	}
	targetState.criticalReviews.push(reviewState);
	if (targetState.criticalReviews.length > 20) {
		targetState.criticalReviews = targetState.criticalReviews.slice(-20);
	}

	// 9. Handle Verdict
	if (parsed.verdict === "PASS") {
		logCriticalReviewTransition("CRITICAL_REVIEW_PASSED", `critical review passed (${options.kind})`, {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			severity: parsed.severity,
			verdict: "PASS",
		});
		targetState.lastReviewedPlanVersion = currentPlanVersion;
		targetState.lastReviewedSaveHash = currentHash;
		targetState.lastReviewedSaveCount = currentSaveCount;
		persist(pi, ctx);
		return { success: true, available: true, review: reviewState };
	}

	if (parsed.verdict === "FAIL") {
		logCriticalReviewTransition("CRITICAL_REVIEW_FAILED", `critical review failed: ${parsed.findings.map((f) => f.issue).join("; ") || "issues found"}`, {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			severity: parsed.severity,
			verdict: "FAIL",
		});
		logCriticalReviewTransition("REMEDIATION_REQUIRED", `remediation required: ${parsed.requiredActions.join("; ") || "fix findings"}`, {
			quest: slug,
			questId,
			sessionId,
			reviewId: correlationId,
			reviewKind: options.kind,
			severity: parsed.severity,
			requiredAction: parsed.requiredActions.join("; "),
		});

		const findingsSummary = parsed.findings.map((f) => `- ${f.issue}${f.evidence ? `\n  Evidence: ${f.evidence}` : ""}`).join("\n");
		const actionsSummary = parsed.requiredActions.map((a) => `- ${a}`).join("\n");

		const errorMsg = `[Quest Journal] CRITICAL REVIEW FAILED

Severity: ${parsed.severity}

Finding:
${findingsSummary || "(Unspecified critical finding)"}

Required action:
${actionsSummary || "Investigate findings, fix deficiencies, and re-verify before proceeding."}

Do not consider the affected work complete until this is resolved and verified.`;

		sendInternalAgentMessage(pi, errorMsg, "steer", "critical_review_failed", correlationId);

		// Material failure affects quest state
		if (parsed.severity === "CRITICAL" || parsed.severity === "MAJOR") {
			triggerReassessment(targetState, `Critical Review Failed: ${parsed.findings.map((f) => f.issue).join("; ")}`, findingsSummary);
		}

		// Persist required actions into quest remaining work on disk
		if (parsed.requiredActions && parsed.requiredActions.length > 0) {
			try {
				let qPath = questPath(questId);
				if (!(await fileExists(qPath))) {
					const rec = await resolveQuestRecordBySlug(slug);
					if (rec) qPath = rec.path;
				}
				if (await fileExists(qPath)) {
					const currentContent = await readFile(qPath, "utf8");
					const sections = parseMarkdownSections(currentContent);
					const remainingSec = sections.get("remaining work") || sections.get("remaining tasks");
					const existingRemaining = remainingSec ? remainingSec.body.trim() : "";
					const newItems = parsed.requiredActions
						.filter((a) => !existingRemaining.includes(a))
						.map((a) => (a.startsWith("- [") ? a : `- [ ] ${a}`))
						.join("\n");
					if (newItems) {
						const combinedRemaining = existingRemaining && existingRemaining !== "-"
							? `${existingRemaining}\n${newItems}`
							: newItems;
						const updates = new Map<string, string>();
						updates.set("remaining work", combinedRemaining);
						const updatedContent = spliceMarkdownSections(currentContent, updates);
						await writeFile(qPath, updatedContent, "utf8");
					}
				}
			} catch {}
		}

		persist(pi, ctx);
		return { success: false, available: true, review: reviewState };
	}

	// UNCERTAIN
	logCriticalReviewTransition("CRITICAL_REVIEW_UNCERTAIN", `critical review uncertain: ${parsed.findings.map((f) => f.issue).join("; ") || "missing evidence"}`, {
		quest: slug,
		questId,
		sessionId,
		reviewId: correlationId,
		reviewKind: options.kind,
		severity: parsed.severity,
		verdict: "UNCERTAIN",
	});
	logCriticalReviewTransition("REMEDIATION_REQUIRED", `targeted investigation required: ${parsed.requiredActions.join("; ") || "verify missing evidence"}`, {
		quest: slug,
		questId,
		sessionId,
		reviewId: correlationId,
		reviewKind: options.kind,
		severity: parsed.severity,
		requiredAction: parsed.requiredActions.join("; "),
	});

	const missingEvidenceSummary = parsed.findings.map((f) => `- ${f.issue}${f.evidence ? `\n  Missing Evidence: ${f.evidence}` : ""}`).join("\n");
	const actionsSummary = parsed.requiredActions.map((a) => `- ${a}`).join("\n");

	const uncertainMsg = `[Quest Journal] CRITICAL REVIEW UNCERTAIN

Missing evidence:
${missingEvidenceSummary || "(Uncertain evidence)"}

Required action:
${actionsSummary || "Perform targeted read/search investigation to establish conclusive evidence."}`;

	sendInternalAgentMessage(pi, uncertainMsg, "steer", "critical_review_uncertain", correlationId);
	persist(pi, ctx);
	return { success: false, available: true, review: reviewState };
}

export async function checkAndTriggerDirectionReview(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	triggerReason: string,
): Promise<CriticalReviewExecutionResult | null> {
	const c = getActiveContext(ctx);
	const s = getState(c);
	if (!s.active || !isRootQuest(s)) return null;
	if (s.reassessmentRequired || s.researchRequired || s.awaitingUserConfirmation) return null;

	const registered = isSubagentToolRegistered(pi, ctx) || Boolean(customSubagentRunner);
	if (!registered) return null;

	const currentPlanVersion = s.planVersion || 1;
	const currentHash = s.lastSavedHash || (s.saveGeneration ? s.saveGeneration.hash : "clean");
	const currentSaveCount = s.saveCount || 0;
	const key = triggerReason === "no_progress"
		? `dir:${s.active}:v${currentPlanVersion}:h${currentHash}:s${currentSaveCount}:no_progress`
		: `dir:${s.active}:v${currentPlanVersion}:h${currentHash}:s${currentSaveCount}`;

	if (s.lastCriticalReview?.kind === "direction" && (s as any).__lastDirectionReviewKey === key) {
		return null;
	}

	const result = await runCriticalReview(pi, ctx, { kind: "direction", questSlug: s.active });
	if (result?.review?.verdict) {
		(s as any).__lastDirectionReviewKey = key;
	}
	return result;
}

export async function submitReviewRebuttal(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	rebuttalText: string,
	options?: { questSlug?: string; kind?: CriticalReviewKind; subagentRunner?: any },
): Promise<CriticalReviewExecutionResult> {
	const c = getActiveContext(ctx);
	const s = getState(c);
	const slug = options?.questSlug || s.active || "quest";
	const kind = options?.kind || (s.lastCriticalReview?.kind || "direction");

	return await runCriticalReview(pi, ctx, {
		kind,
		questSlug: slug,
		force: true,
		rebuttal: rebuttalText,
		subagentRunner: options?.subagentRunner,
	});
}
