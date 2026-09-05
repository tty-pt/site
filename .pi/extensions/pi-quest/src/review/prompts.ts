// HIGH_LEVEL: #review request — brief identifies qid, type, target, plan, evidence, criteria.
// HIGH_LEVEL: #reviewer independence — minimal info, judge and report.
// HIGH_LEVEL: #review independence — no inherited context, no prior conclusions.
// HIGH_LEVEL: #independent review contexts — fresh context, no inherited reasoning.
import type { Qid } from "../domain/qid";
import type { ReviewKind } from "../domain/quest";
import { DEFAULT_CONFIG, type DraftThresholds } from "../config";

export interface ReviewMaterial {
  objective: string;
  plan: string;
  evidence: string[];
  amendments: string[];
  rebuttal?: string;
  implementationSummary?: string;
}

const READ_ONLY_RULES = `MANDATORY INVARIANTS:
1. You are strictly read-only. Do not edit files, run mutating commands, or change project state.
2. Do not trust summaries or claims. Independently inspect the repository and verify important claims with your read/search tools.
3. You judge and report. You never modify quest state, the plan, or implementation files.`;

const SELF_ATTACK = `TWO-PASS SELF-ATTACK:
PASS 1: Independently evaluate the material against the request and repository evidence.
PASS 2: Attack your own conclusion — what requirement could still be missing, what assumption is most dangerous, what is least supported by evidence?
Only the post-self-critique verdict counts.`;

const OUTPUT_FORMAT = `OUTPUT FORMAT — your response MUST end with exactly this structure:

VERDICT: PASS | FAIL
SEVERITY: NONE | MINOR | MAJOR | CRITICAL

FINDINGS:
- Issue: <concrete issue or omission>
  Evidence: <concrete evidence from repository, plan, or logs>

REQUIRED REVISIONS:
- <concrete change or targeted investigation required>`;

function contextBlock(material: ReviewMaterial): string {
  const evidence = material.evidence.length > 0 ? material.evidence.join("\n") : "(none)";
  const amendments = material.amendments.length > 0 ? material.amendments.join("\n") : "(none)";
  const rebuttal = material.rebuttal
    ? `\n--- IMPLEMENTER REBUTTAL ---\n${material.rebuttal}\n`
    : "";
  return `--- QUEST MATERIAL ---
ORIGINAL REQUEST (primary acceptance criterion):
${material.objective || "(none)"}

APPROVED PLAN:
${material.plan || "(none)"}

RECORDED AMENDMENTS:
${amendments}

EVIDENCE:
${evidence}
${rebuttal}--- END MATERIAL ---`;
}

function draftGuidance(maturity: DraftThresholds): string {
  return `WHAT YOU MUST EVALUATE (DRAFT REVIEW):
You review the PLAN, not implementation. Compare the draft plan against the exact recorded request:
1. Whether the plan addresses the objective; 2. Whether requirements were omitted;
3. Whether it substituted a different problem; 4. Whether research is sufficient;
5. Whether assumptions remain unverified; 6. Whether the work sequence fits;
7. Whether complexity is unnecessary; 8. Whether an alternative was dismissed without evidence;
9. Whether it commits prematurely; 10. Whether it contradicts itself;
11. Whether it credibly satisfies the request.
MATURITY BAR: a reviewable draft has ${maturity.requirements} requirements, or 1 requirement plus ${maturity.evidence} evidence items, with an actionable plan. Below the bar, FAIL fast naming exactly what is missing.
Distinguish: user requirement (blocks) vs technical constraint (binds) vs reviewer preference (NEVER blocks).`;
}

function validationGuidance(): string {
  return `WHAT YOU MUST EVALUATE (VALIDATION):
You review the IMPLEMENTATION against the approved plan plus recorded amendments:
1. Every plan step implemented or explicitly superseded by an amendment;
2. Amendments stay in scope and carry reasons; scope change is a failure;
3. Decisions along the way were appropriate and evidenced;
4. The work is verified (tests, builds, or stated verification).
Reviewer preference NEVER blocks; only unmet requirements or out-of-scope drift do.`;
}

export function buildReviewPrompt(
  kind: ReviewKind,
  qid: Qid,
  target: string,
  material: ReviewMaterial,
  maturity: DraftThresholds = DEFAULT_CONFIG.draftThresholds,
): string {
  const header = kind === "draft"
    ? `ADVERSARIAL DRAFT REVIEW: ${qid} (target revision ${target})`
    : `VALIDATION REVIEW: ${qid} (target implementation snapshot ${target})`;
  const guidance = kind === "draft" ? draftGuidance(maturity) : validationGuidance();
  const impl = material.implementationSummary
    ? `\nIMPLEMENTATION SUMMARY UNDER REVIEW:\n${material.implementationSummary}\n`
    : "";
  return `# ${header}

You are an independent reviewer. You run in a fresh context: evaluate only the material below, never continue anyone's reasoning.
${READ_ONLY_RULES}
${guidance}
${contextBlock(material)}
${impl}
${SELF_ATTACK}
${OUTPUT_FORMAT}`;
}
