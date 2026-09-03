import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { parseQuestId } from "../markdown.ts";
import { ensureQuestIdInContent, parseMarkdownSections } from "../markdown.ts";
import { questPath, resolveQuestRecordBySlug } from "../paths.ts";
import { generateQuestId } from "../state.ts";
import { isPlaceholderOrEmpty } from "../utils.ts";
import { validateResearchPrerequisites } from "../validation.ts";
import { LoadedQuestState, MarkdownSection } from "../types.ts";

export function parseSectionTimestamp(
  sec?: MarkdownSection,
): number | undefined {
  if (!sec || isPlaceholderOrEmpty(sec.body)) return undefined;
  const raw = sec.body.trim();
  const parsed = Number.parseInt(raw, 10);
  if (!Number.isNaN(parsed) && parsed > 0) return parsed;
  const dateParsed = Date.parse(raw);
  if (!Number.isNaN(dateParsed) && dateParsed > 0) return dateParsed;
  return undefined;
}

export function parseSectionInteger(
  sec?: MarkdownSection,
  defaultVal = 0,
  minVal = 0,
): number {
  if (!sec || isPlaceholderOrEmpty(sec.body)) return defaultVal;
  const parsed = Number.parseInt(sec.body.replace(/\D/g, ""), 10);
  if (!Number.isNaN(parsed) && parsed >= minVal) return parsed;
  return defaultVal;
}

export function parseSectionConfidence(
  sec?: MarkdownSection,
): "low" | "medium" | "high" {
  if (!sec || isPlaceholderOrEmpty(sec.body)) return "low";
  const lower = sec.body.toLowerCase();
  if (lower.includes("high")) return "high";
  if (lower.includes("medium")) return "medium";
  return "low";
}

export function parseReassessmentState(
  sections: Map<string, MarkdownSection>,
  reassessmentVersion: number,
  resolvedReassessmentVersion: number,
): {
  reassessmentRequired: boolean;
  reassessmentReason: string | null;
  reassessmentEvidence: string | null;
} {
  let reassessmentRequired = false;
  let reassessmentReason: string | null = null;
  let reassessmentEvidence: string | null = null;

  const statusSec = sections.get("reassessment status") ||
    sections.get("reassessment state");
  if (statusSec && !isPlaceholderOrEmpty(statusSec.body)) {
    if (statusSec.body.toUpperCase().includes("REQUIRED")) {
      reassessmentRequired = true;
      const match = statusSec.body.match(/REQUIRED[^-]*-(.*)$/i);
      if (match && match[1]) reassessmentReason = match[1].trim();
    }
  } else if (reassessmentVersion > resolvedReassessmentVersion) {
    reassessmentRequired = true;
  }

  const evidenceSec = sections.get("reassessment evidence");
  if (evidenceSec && !isPlaceholderOrEmpty(evidenceSec.body)) {
    reassessmentEvidence = evidenceSec.body.trim();
  }

  return { reassessmentRequired, reassessmentReason, reassessmentEvidence };
}

export function createDefaultEpistemicState(exists = false): LoadedQuestState {
  return {
    originalRequest: "",
    refinements: [],
    exists,
    researchRound: 1,
    researchComplete: false,
    researchRequired: true,
    planVersion: 1,
    planConfidence: "low",
    reassessmentRequired: false,
    reassessmentReason: null,
    reassessmentEvidence: null,
    reassessmentVersion: 0,
    resolvedReassessmentVersion: 0,
    lastPlanRevisionsText: null,
  };
}

export function parseOriginalRequest(sections: Map<string, any>): string {
  const reqSec = sections.get("original request") ||
    sections.get("original user request");
  if (!reqSec) return "";
  const rawText = reqSec.body.replace(/^>\s*/gm, "").trim();
  if (
    rawText &&
    !rawText.startsWith("Paste the verbatim user prompt") &&
    !rawText.startsWith("Goal:")
  ) {
    return rawText;
  }
  return "";
}

export function parseRefinements(sections: Map<string, any>): string[] {
  const refSec = sections.get("quest refinements & user feedback loops") ||
    sections.get("refinements");
  if (!refSec) return [];
  return refSec.body
    .split(/\r?\n/)
    .map((l: string) =>
      l
        .replace(/^[-*]\s*/, "")
        .replace(/^\d+\.\s*/, "")
        .trim()
    )
    .filter((l: string) => l && l !== "-" && !l.startsWith(">"));
}

export function parsePlanRevisionsText(
  sections: Map<string, any>,
): string | null {
  const revSec = sections.get("plan revisions") ||
    sections.get("plan revision history") ||
    sections.get("revisions");
  if (revSec && !isPlaceholderOrEmpty(revSec.body)) return revSec.body.trim();
  return null;
}

export function parseAwaitingUserConfirmation(
  sections: Map<string, any>,
): boolean {
  const currentStatusSec = sections.get("current status") ||
    sections.get("status");
  if (currentStatusSec && !isPlaceholderOrEmpty(currentStatusSec.body)) {
    const bodyLower = currentStatusSec.body.toLowerCase();
    if (
      bodyLower.includes("plan provisional") ||
      bodyLower.includes("research pending") ||
      bodyLower.includes("confirmation pending") ||
      bodyLower.includes("research complete") ||
      bodyLower.includes("provisional")
    ) {
      return true;
    }
    if (
      bodyLower.includes("plan confirmed") ||
      bodyLower.includes("in progress") ||
      bodyLower.includes("done")
    ) {
      return false;
    }
  }
  return false;
}

export async function loadExistingQuestEpistemicState(
  slugOrQid: string,
  basePath?: string,
): Promise<LoadedQuestState> {
  let path = "";
  let content: string | null = null;
  if (basePath) {
    if (basePath.endsWith("quest.md") || basePath.endsWith(".md")) {
      path = basePath;
    } else {
      const nested = join(basePath, slugOrQid, "quest.md");
      try {
        content = await readFile(nested, "utf8");
        path = nested;
      } catch {
        path = join(basePath, `${slugOrQid}.md`);
        try {
          content = await readFile(path, "utf8");
        } catch {
          return createDefaultEpistemicState(false);
        }
      }
    }
    if (content === null) {
      try {
        content = await readFile(path, "utf8");
      } catch {
        return createDefaultEpistemicState(false);
      }
    }
  } else {
    path = questPath(slugOrQid);
    try {
      content = await readFile(path, "utf8");
    } catch {
      const record = await resolveQuestRecordBySlug(slugOrQid);
      if (record) {
        path = record.path;
        try {
          content = await readFile(path, "utf8");
        } catch {
          return createDefaultEpistemicState(false);
        }
      } else {
        return createDefaultEpistemicState(false);
      }
    }
  }
  try {
    if (content === null) content = await readFile(path, "utf8");
    let questId = parseQuestId(content);
    if (!questId) {
      questId = generateQuestId();
      try {
        const updatedContent = ensureQuestIdInContent(content, questId);
        await writeFile(path, updatedContent, "utf8");
      } catch {}
    }
    const sections = parseMarkdownSections(content);
    // Try meta.json for machine metadata (sharded store), but only for root quest.md to avoid child contamination; use max to stay monotonic
    let meta: any = null;
    if (path.endsWith("quest.md")) {
      try {
        const dir = path.slice(0, -"/quest.md".length);
        const tryMeta = join(dir, "meta.json");
        const raw = await readFile(tryMeta, "utf8");
        meta = JSON.parse(raw);
      } catch {}
    }
    const originalRequest = parseOriginalRequest(sections);
    const refinements = parseRefinements(sections);
    const mdPlanVersion = parseSectionInteger(
      sections.get("plan version") || sections.get("version"),
      1,
      1,
    );
    const mdResearchRound = parseSectionInteger(
      sections.get("research round") || sections.get("research cycle"),
      1,
      1,
    );
    const mdLastResearchAt = parseSectionTimestamp(
      sections.get("last research at") ||
        sections.get("last research timestamp") ||
        sections.get("last research"),
    );
    const mdLastPlanRevisionAt = parseSectionTimestamp(
      sections.get("last plan revision at") ||
        sections.get("last plan revision timestamp") ||
        sections.get("last plan revision"),
    );
    const mdReassessmentVersion = parseSectionInteger(
      sections.get("reassessment version"),
      0,
      0,
    );
    const mdResolvedReassessmentVersion = parseSectionInteger(
      sections.get("resolved reassessment version"),
      0,
      0,
    );
    const planConfidence = meta?.planConfidence
      ? parseSectionConfidence({
        heading: "",
        normalized: "",
        level: 0,
        body: String(meta.planConfidence),
        raw: "",
      })
      : parseSectionConfidence(
        sections.get("plan confidence") || sections.get("confidence"),
      );
    const planVersion = meta?.planVersion !== undefined
      ? Math.max(mdPlanVersion, Math.max(1, Number(meta.planVersion) || 1))
      : mdPlanVersion;
    const researchRound = meta?.researchRound !== undefined
      ? Math.max(
        mdResearchRound,
        Math.max(1, Number(meta.researchRound) || 1),
      )
      : mdResearchRound;
    const lastResearchAt =
      meta?.lastResearchAt !== undefined && meta.lastResearchAt
        ? Math.max(mdLastResearchAt || 0, Number(meta.lastResearchAt) || 0) ||
          mdLastResearchAt
        : mdLastResearchAt;
    const lastPlanRevisionAt =
      meta?.lastPlanRevisionAt !== undefined && meta.lastPlanRevisionAt
        ? Math.max(
          mdLastPlanRevisionAt || 0,
          Number(meta.lastPlanRevisionAt) || 0,
        ) || mdLastPlanRevisionAt
        : mdLastPlanRevisionAt;
    const reassessmentVersion = meta?.reassessmentVersion !== undefined
      ? Math.max(
        mdReassessmentVersion,
        Math.max(0, Number(meta.reassessmentVersion) || 0),
      )
      : mdReassessmentVersion;
    const resolvedReassessmentVersion =
      meta?.resolvedReassessmentVersion !== undefined
        ? Math.max(
          mdResolvedReassessmentVersion,
          Math.max(0, Number(meta.resolvedReassessmentVersion) || 0),
        )
        : mdResolvedReassessmentVersion;
    const { reassessmentRequired, reassessmentReason, reassessmentEvidence } =
      parseReassessmentState(
        sections,
        reassessmentVersion,
        resolvedReassessmentVersion,
      );
    const lastPlanRevisionsText = parsePlanRevisionsText(sections);
    const validation = validateResearchPrerequisites(
      content,
      planConfidence,
      true,
    );
    const researchComplete = validation.valid && !reassessmentRequired;
    const researchRequired = !researchComplete;
    const awaitingUserConfirmation = parseAwaitingUserConfirmation(sections);
    return {
      questId,
      originalRequest,
      refinements,
      exists: true,
      researchRound,
      researchComplete,
      researchRequired,
      planVersion,
      planConfidence,
      lastResearchAt,
      lastPlanRevisionAt,
      awaitingUserConfirmation,
      reassessmentRequired,
      reassessmentReason,
      reassessmentEvidence,
      reassessmentVersion,
      resolvedReassessmentVersion,
      lastPlanRevisionsText,
    };
  } catch {
    return createDefaultEpistemicState(false);
  }
}

export async function loadExistingQuestIntent(
  slug: string,
): Promise<{ originalRequest: string; refinements: string[] }> {
  const loaded = await loadExistingQuestEpistemicState(slug);
  return {
    originalRequest: loaded.originalRequest,
    refinements: loaded.refinements,
  };
}

export function extractChildResultSummary(
  content: string,
  name: string,
): string {
  const sections = parseMarkdownSections(content);
  const lines: string[] = [];
  const goalSec = sections.get("goal");
  if (goalSec && goalSec.body) lines.push(`- **Goal**: ${goalSec.body.trim()}`);
  const understandingSec = sections.get("current understanding");
  if (
    understandingSec &&
    understandingSec.body &&
    !understandingSec.body.startsWith(">")
  ) {
    lines.push(
      `- **Established Understanding**:\n${understandingSec.body.trim()}`,
    );
  }
  const findingsSec = sections.get("research findings") ||
    sections.get("in-depth analysis & findings");
  if (findingsSec && findingsSec.body) {
    lines.push(`- **Findings & Discoveries**:\n${findingsSec.body.trim()}`);
  }
  const assumptionsSec = sections.get("key assumptions") ||
    sections.get("assumptions");
  if (assumptionsSec && assumptionsSec.body) {
    lines.push(`- **Assumptions Evaluated**:\n${assumptionsSec.body.trim()}`);
  }
  const rejectedSec = sections.get("rejected approaches");
  if (rejectedSec && rejectedSec.body && !rejectedSec.body.startsWith(">")) {
    lines.push(`- **Rejected Approaches**:\n${rejectedSec.body.trim()}`);
  }
  const reassessSec = sections.get("latest reassessment") ||
    sections.get("reassessment conclusion");
  if (reassessSec && reassessSec.body && !reassessSec.body.startsWith(">")) {
    lines.push(
      `- **Latest Reassessment Conclusion**:\n${reassessSec.body.trim()}`,
    );
  }
  const decisionsSec = sections.get("decisions made") ||
    sections.get("decisions");
  if (decisionsSec && decisionsSec.body) {
    lines.push(`- **Decisions Made**:\n${decisionsSec.body.trim()}`);
  }
  const filesSec = sections.get("files touched") ||
    sections.get("files modified");
  if (filesSec && filesSec.body) {
    lines.push(`- **Files Touched**:\n${filesSec.body.trim()}`);
  }
  return lines.length > 0
    ? lines.join("\n\n")
    : `- Completed sub-quest ${name}.`;
}
