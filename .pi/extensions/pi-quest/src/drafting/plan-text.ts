// HIGH_LEVEL: #drafting — the quest document is plain markdown sections.
// Pure plan-text operations over the draft file: hashing, section splicing,
// parsing, maturity thresholds, and the review-count marker that forces a
// re-review by slightly altering the plan.
import { createHash } from "node:crypto";
import { DEFAULT_CONFIG, type DraftThresholds } from "../config";
import type { QuestState } from "../domain/quest";
import type { ReviewMaterial } from "../review/prompts";
import { diffPlans } from "./plan-diff";

export interface DraftSections {
  requirements: string[];
  evidence: string[];
  plan: string;
}

export function hashContent(content: string): string {
  return createHash("sha256").update(content, "utf8").digest("hex");
}

export function splicePlanSection(text: string, plan: string): string {
  const lines = text.split("\n");
  const start = lines.findIndex((line) => {
    const header = line.match(/^##\s+(.+?)\s*$/i);
    return header !== null && header[1].toLowerCase().includes("implementation plan");
  });
  if (start === -1) {
    const body = text.endsWith("\n") ? text : `${text}\n`;
    return `${body}\n## Implementation Plan\n\n${plan.trim()}\n`;
  }
  let end = lines.length;
  for (let i = start + 1; i < lines.length; i += 1) {
    if (/^##\s+.+?\s*$/.test(lines[i])) {
      end = i;
      break;
    }
  }
  return [...lines.slice(0, start + 1), "", plan.trim(), "", ...lines.slice(end)].join("\n");
}

export function parseDraftSections(text: string): DraftSections {
  const requirements: string[] = [];
  const evidence: string[] = [];
  const planLines: string[] = [];
  let section: "requirements" | "evidence" | "plan" | null = null;
  for (const line of text.split(/\r?\n/)) {
    const header = line.match(/^##\s+(.+?)\s*$/);
    if (header) {
      const name = header[1].toLowerCase();
      if (name.includes("requirement")) section = "requirements";
      else if (name.includes("evidence")) section = "evidence";
      else if (name.includes("implementation plan")) section = "plan";
      // Pre-draft investigation recorded via recordRefinement is research:
      // its bullets fold into the evidence list so it counts toward the bar.
      else if (name.includes("findings") && name.includes("pre-draft")) section = "evidence";
      else section = null;
      continue;
    }
    if (section === "plan") {
      planLines.push(line);
      continue;
    }
    const bullet = line.match(/^\s*(?:[-*+]|\d+[.)])\s+(.+?)\s*$/);
    if (bullet && (section === "requirements" || section === "evidence")) {
      if (section === "requirements") requirements.push(bullet[1]);
      else evidence.push(bullet[1]);
    }
  }
  return { requirements, evidence, plan: planLines.join("\n").trim() };
}

export function meetsReviewThresholds(
  sections: DraftSections,
  thresholds = DEFAULT_CONFIG.draftThresholds,
): boolean {
  const req = sections.requirements.length;
  const ev = sections.evidence.length;
  const counts = req >= thresholds.requirements || (req >= 1 && ev >= thresholds.evidence);
  return counts && sections.plan.length > 0;
}

// The deterministic draft profile the agent sees before and at every save:
// counts, the maturity-bar verdict, and what is missing to clear it. It is the
// agent's free look at what the reviewer will check, without a review boot.
export function draftProfileText(
  sections: DraftSections,
  thresholds: DraftThresholds,
  citationSummary = "",
): string {
  const req = sections.requirements.length;
  const ev = sections.evidence.length;
  const planPresent = sections.plan.trim().length > 0;
  const counts = req >= thresholds.requirements || (req >= 1 && ev >= thresholds.evidence);
  const meets = counts && planPresent;
  const gapLeg = ` (needs ${thresholds.requirements} requirements, or 1 requirement + ${thresholds.evidence} evidence, with an actionable plan)`;
  const lines = [
    `draft profile: requirements ${req}, evidence ${ev}, plan ${planPresent ? "present" : "missing"}`,
  ];
  if (meets) {
    lines.push(`reviewability: maturity bar: met${gapLeg}`);
  } else {
    const gaps: string[] = [];
    if (!planPresent) gaps.push("author the ## Implementation Plan section");
    if (req < thresholds.requirements && ev < thresholds.evidence) {
      gaps.push(`add ${thresholds.requirements - req} more requirements, or ${thresholds.evidence - ev} more evidence items with at least 1 requirement`);
    } else if (req < thresholds.requirements) {
      gaps.push(`add ${thresholds.requirements - req} more requirements`);
    } else if (ev < thresholds.evidence) {
      gaps.push(`add ${thresholds.evidence - ev} more evidence items`);
    }
    lines.push(`reviewability: maturity bar NOT met${gapLeg}; missing: ${gaps.join("; ") || "none"}`);
  }
  if (citationSummary !== "") lines.push(citationSummary);
  return lines.join("\n");
}

// --- Claim citations --------------------------------------------------------
// File:line citations are the plan's verifiable surface. Parsed purely so the
// save path can warn on drift and the review brief can carry a CLAIM MANIFEST.

export interface PlanCitation {
  file: string;
  lineStart: number;
  lineEnd: number | undefined;
}

const CITATION_RE = /([A-Za-z0-9_][A-Za-z0-9_./-]*\.(?:c|h|ts|md|sh))[:](\d+)(?:-(\d+))?/g;

// URLs like http://host/y.c:5 look like citations but are not local refs.
export function citeLocations(text: string): PlanCitation[] {
  const out: PlanCitation[] = [];
  const scrubbed = text.replace(/[A-Za-z][A-Za-z0-9+.-]*:\/\/[^\s)]+/g, " ");
  const re = new RegExp(CITATION_RE.source, "g");
  let m: RegExpExecArray | null;
  while ((m = re.exec(scrubbed)) !== null) {
    const lineEnd = m[3] === undefined ? undefined : Number(m[3]);
    out.push({ file: m[1], lineStart: Number(m[2]), lineEnd });
  }
  return out;
}

// The reviewer spot-checks these instead of re-deriving the whole tree.
export function buildClaimManifest(sections: DraftSections): string {
  const lines: string[] = [];
  for (const item of sections.evidence) {
    lines.push(`- Evidence claim (verify in repo): ${item}`);
  }
  for (const cite of citeLocations(sections.plan)) {
    lines.push(`- Cited location: ${cite.file}:${cite.lineStart}${cite.lineEnd !== undefined ? `-${cite.lineEnd}` : ""}`);
  }
  return lines.join("\n");
}

// The review-count marker lives inside the Implementation Plan section: an
// HTML comment, invisible in render, ignorable by the reviewer. Bumping it
// changes the content hash, so a failed review re-boots through the one
// save→boot pipeline and survives a mid-retry restart.
export const REVIEW_COUNT_MARKER = /^<!--\s*pi-quest:\s*review-count\s+(\d+)\s*-->\s*$/i;
export const REVIEW_COUNT_LINE = (count: number): string => `<!-- pi-quest: review-count ${count} -->`;

// Agent plan writes seed the counter at zero; stale agent-carried markers
// never survive.
export function seedReviewCount(plan: string): string {
  const lines = plan.split("\n").filter((line) => !REVIEW_COUNT_MARKER.test(line.trim()));
  return [REVIEW_COUNT_LINE(0), ...lines].join("\n");
}

// Retry bumps rewrite the quest doc with the incremented count. No plan body
// means no bootable review: the text is returned untouched.
export function bumpReviewCount(text: string, count: number): string {
  const sections = parseDraftSections(text);
  const planLines = sections.plan
    .split("\n")
    .filter((line) => line.trim() !== "" && !REVIEW_COUNT_MARKER.test(line.trim()));
  if (planLines.length === 0) return text;
  const bumpedPlan = [REVIEW_COUNT_LINE(count), ...planLines].join("\n");
  return splicePlanSection(text, bumpedPlan);
}

// The re-review brief must diff against the previously reviewed plan, never
// against the plan being sent.
export function reviewMaterial(state: QuestState, sections: DraftSections): ReviewMaterial {
  const openRebuttal = [...state.reviewDialogue].reverse().find((d) => d.verdictAfter === undefined);
  const base: ReviewMaterial = {
    objective: state.pendingRootRequest ?? state.objective,
    plan: sections.plan,
    evidence: sections.evidence,
    amendments: state.amendments.map((a) => `${a.change} (${a.reasons})`),
    rebuttal: openRebuttal?.implementerRebuttal,
    claimManifest: buildClaimManifest(sections),
  };
  const last = state.lastReview;
  const previous = state.draft?.lastReviewedPlan;
  if (last === null || previous === undefined || previous === null) return base;
  // Prior verdict always travels: an evidence-only revision answers the last
  // findings even when the plan itself is unchanged (then diffPlans is null).
  const continued: ReviewMaterial = {
    ...base,
    previousVerdict: last.verdict,
    previousFindings: last.findings,
  };
  const planDiff = diffPlans(previous, sections.plan);
  if (planDiff === null) return continued;
  return { ...continued, planDiff };
}
