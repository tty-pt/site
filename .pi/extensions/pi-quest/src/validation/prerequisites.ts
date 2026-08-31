import { SECTION_ALIASES } from "../constants.ts";
import { parseMarkdownSections } from "../markdown.ts";
import { MarkdownSection } from "../types.ts";
import { isPlaceholderOrEmpty } from "../utils.ts";

export function validateResearchPrerequisites(
  markdownContent: string,
  planConfidence?: string,
  allowLowConfidence = false,
  planConfidenceReason?: string,
): { valid: boolean; missingSections: string[]; confidenceIssue?: string } {
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
      if (s && !isPlaceholderOrEmpty(s.body)) { foundSec = s; break; }
    }
    if (!foundSec) missingSections.push(req.label);
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

  return { valid: missingSections.length === 0 && !confidenceIssue, missingSections, confidenceIssue };
}
