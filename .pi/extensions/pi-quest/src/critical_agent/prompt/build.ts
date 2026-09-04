import { readFile } from "node:fs/promises";
import { parseMarkdownSections } from "../../markdown.ts";
import {
  fileExists,
  questPath,
  resolveQuestRecordBySlug,
} from "../../paths.ts";
import {
  parseOriginalRequest,
  parseRefinements,
} from "../../reconstruction.ts";
import { state } from "../../state.ts";
import {
  CriticalReviewKind,
  QuestReviewContext,
  StoredState,
} from "../../types.ts";

export async function extractQuestReviewContext(
  slugOrQid: string,
  s?: StoredState,
): Promise<QuestReviewContext> {
  const activeState = s || state;
  // Draft-aware: if activeDraft matches slug, prefer future draft file
  const { FUTURE_DIR } = await import("../../constants.ts");
  const futurePath = `${FUTURE_DIR}/${slugOrQid}.md`;
  let path = questPath(slugOrQid);
  if (activeState.activeDraft === slugOrQid && (await fileExists(futurePath))) {
    path = futurePath;
  } else if (!(await fileExists(path))) {
    if (await fileExists(futurePath)) path = futurePath;
    else {
      const rec = await resolveQuestRecordBySlug(slugOrQid);
      if (rec) path = rec.path;
    }
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
  if (
    !originalRequest && activeState.prompts && activeState.prompts.length > 0
  ) {
    originalRequest = activeState.prompts[0];
  }

  let refinements = parseRefinements(sections);
  if (
    refinements.length === 0 && Array.isArray(activeState.refinements) &&
    activeState.refinements.length > 0
  ) {
    refinements = [...activeState.refinements];
  }

  return {
    originalRequest: originalRequest || "(No verbatim prompt recorded)",
    refinements,
    currentUnderstanding: getSec("current understanding") ||
      getSec("understanding"),
    keyAssumptions: getSec("key assumptions") || getSec("assumptions"),
    openQuestions: getSec("open questions & uncertainties") ||
      getSec("open questions") || getSec("uncertainties"),
    plan: getSec("plan") || getSec("detailed multi-stage execution plan"),
    planConfidence: getSec("plan confidence") || activeState.planConfidence ||
      "medium",
    planRevisions: getSec("plan revisions") || getSec("plan revision history"),
    findings: getSec("research findings") || getSec("important findings") ||
      getSec("in-depth analysis & findings"),
    filesModified: getSec("files touched") || getSec("files modified"),
    testStatus: getSec("test / build status") || getSec("test status") ||
      getSec("build & test status"),
    executionSnapshot: getSec("execution snapshot") ||
      getSec("execution state"),
    exactNextAction: getSec("exact next action") ||
      getSec("next recommended step") || getSec("next step"),
    remainingWork: getSec("remaining work") || getSec("remaining tasks"),
    status: getSec("current status") || getSec("status"),
  };
}

export function buildCriticalReviewPrompt(
  kind: CriticalReviewKind,
  questSlug: string,
  context: QuestReviewContext,
  rebuttal?: string,
  triggerReason?: string,
  boundaryKey?: string | null,
): string {
  const trig = triggerReason ? ` — ${triggerReason}` : "";
  const boundaryLabel = boundaryKey
    ? ` — ${
      boundaryKey.startsWith("draft:")
        ? `draft h${boundaryKey.split(":")[2]?.slice(0, 5) || ""}`
        : boundaryKey
    }`
    : "";
  const header = kind === "plan_review"
    ? `ADVERSARIAL PLAN REVIEW: ${questSlug} (PLAN REVIEW${trig})${boundaryLabel}`
    : kind === "direction"
    ? `CRITICAL DIRECTION & RESEARCH REVIEW: ${questSlug} (DIRECTION REVIEW${trig})`
    : `CRITICAL FINAL ACCEPTANCE & COMPLETION REVIEW: ${questSlug} (FINAL ACCEPTANCE REVIEW${trig})`;

  const rebuttalSection = rebuttal
    ? `\n--- MAIN AGENT EVIDENCE-BASED REBUTTAL / CLARIFICATION ---\n${rebuttal}\n`
    : "";

  const planEvaluationGuidance = kind === "plan_review" || kind === "direction"
    ? `
WHAT YOU MUST EVALUATE (PLAN REVIEW):
You are reviewing the PLAN, not the final implementation code.
You must compare the draft plan against the exact recorded original user request.
Evaluate the following 11 criteria rigorously:
1. Whether the plan actually addresses the user's objective;
2. Whether important requirements were omitted;
3. Whether the plan substituted a different problem for the requested one;
4. Whether the research supporting the plan is sufficient;
5. Whether important assumptions remain unverified;
6. Whether the proposed sequence of work is appropriate;
7. Whether the plan contains unnecessary complexity;
8. Whether an important alternative was dismissed without evidence;
9. Whether the plan is prematurely committing to an implementation before the problem is understood;
10. Whether the plan contains contradictions or internally incompatible steps;
11. Whether the plan provides a credible path to satisfying the original request.

CRITICAL DISTINCTION:
You MUST explicitly distinguish:
- User Requirement (must be strictly satisfied)
- Technical Constraint (must be respected)
- Reviewer Preference (MUST NOT block a plan)
Reviewer preference alone must never block a plan. A concrete risk to correctness or unmet requirement may block it.

### 13. REVIEWER SERIALIZATION (know your context)

You are ONE review in a single-flight pipeline. There is **at most ONE active
Critical Review per quest at any time**, and a global cap of 1 review applies
across all quests. This is enforced by the scheduler (\`canLaunchReview\` /
\`registerActiveReview\`, \`GLOBAL_REVIEW_CAP\`), NOT by you. Your job is to
evaluate — never to spawn, serialize, or "fix" the scheduler.

The plan you received is a **snapshot** captured at launch; a newer state may
already exist. You evaluate the plan exactly as snapshotted. If a newer plan
arrived while you ran, that does not mean you are wrong or that you must be
re-run — the caller discards stale results and, at most, launches ONE fresh
review for the latest state.

Serialization facts (do not contradict these):
* A review already running for the quest prevents another from starting; repeated requests are **coalesced**, never parallelized.
* A state/hash/version change while a reviewer runs does **not** launch a second reviewer immediately.
* Differing state hashes are different snapshots, but all are reviews of the same quest's reasoning and are **serialized**.
* Planning updates are frequent and cheap; coalesce repeated review requests into one pending/latest request.
* When the current reviewer finishes, the caller alone decides whether the latest state still needs a new review.
`
    : "";

  return `# ${header}

You are the Critical Reviewer subagent for the Quest Journal.
Your sole job is adversarial falsification: find unverified assumptions, plan contradictions, missing evidence, scope drift, or unmet requirements before changes proceed or before completion is accepted.

MANDATORY INVARIANTS:
1. You are strictly read-only. Do not attempt to edit files or change project state.
2. Do not trust the main agent's summary or claims. Independently inspect the repository and verify important claims using your read/search tools. Treat the plan as untrusted until checked against repository evidence and the verbatim original request.
3. Read the verbatim original user prompt and all refinements. Ensure all requirements are explicitly accounted for in the plan.
4. If technical evidence is missing or contradictions exist, you MUST issue a REVISE or UNCERTAIN verdict. Do NOT rubber-stamp.
${planEvaluationGuidance}
--- CURRENT QUEST CONTEXT ---
ORIGINAL USER REQUEST (Primary Acceptance Criterion):
${context.originalRequest || "(none)"}

REFINEMENTS / FEEDBACK:
${(context.refinements?.length ?? 0) > 0 ? context.refinements.join("\n") : "(none)"}

CURRENT UNDERSTANDING:
${context.currentUnderstanding || "(none provided)"}

KEY ASSUMPTIONS:
${context.keyAssumptions || "(none provided)"}

CURRENT PLAN DRAFT:
${context.plan || "(none provided)"}

PLAN CONFIDENCE:
${context.planConfidence || "medium"}

IMPORTANT PLAN REVISIONS:
${context.planRevisions || "(none)"}

OPEN QUESTIONS & UNCERTAINTIES:
${context.openQuestions || "(none)"}

RELEVANT RESEARCH FINDINGS / EVIDENCE:
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

TWO-PASS SELF-ATTACK REQUIREMENT:
You MUST internally perform two passes:
PASS 1: Independently evaluate the plan against the original request and repository evidence.
PASS 2: Attack your own conclusion. Ask specifically:
- What requirement could this plan still be missing?
- What assumption is most dangerous?
- What part of the plan is least supported by evidence?
- What would make this plan wrong?
Only the post-self-critique verdict counts.

OUTPUT FORMAT REQUIREMENT:
Your response MUST end with the following structured format:

PASS 1 (Independent Evaluation):
[Provisional Judgment: APPROVE | REVISE | UNCERTAIN]
[Provisional Summary]

PASS 2 (Self-Attack & Falsification):
- Assumptions tested: ...
- Evidence evaluated: ...
- Invalidation risk: ...
- Revised Judgment: APPROVE | REVISE | UNCERTAIN

PROMPT-COMPLIANCE:
- Requirement: <requirement from original user prompt> -> Plan Handling: <how plan addresses it> -> Status: SATISFIED | UNSATISFIED | UNCERTAIN

VERDICT: APPROVE | REVISE | UNCERTAIN
SEVERITY: NONE | MINOR | MAJOR | CRITICAL

FINDINGS:
- Issue: <concrete issue or omission>
  Evidence: <concrete evidence from repository, plan draft, or logs>

REQUIRED REVISIONS:
- <concrete change or targeted investigation required>
`;
}
