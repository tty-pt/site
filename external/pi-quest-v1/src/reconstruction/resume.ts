import { readFile } from "node:fs/promises";
import { QuestErrorCode, SECTION_ALIASES } from "../constants.ts";
import { logError } from "../messaging.ts";
import { tryLog } from "../logging.ts";
import { questPath, resolveQuestRecordBySlug } from "../paths.ts";
import { state } from "../state.ts";
import { parseMarkdownSections } from "../markdown.ts";
import type { MarkdownSection } from "../types.ts";

export const RESUME_TARGET_SECTIONS = [
  { key: "original request", title: "Original Request", maxChars: 4000 },
  { key: "current status", title: "Current Status", maxChars: 2000 },
  {
    key: "current understanding",
    title: "Current Understanding",
    maxChars: 4000,
  },
  { key: "key assumptions", title: "Key Assumptions", maxChars: 3000 },
  {
    key: "open questions & uncertainties",
    title: "Open Questions & Uncertainties",
    maxChars: 3000,
  },
  { key: "plan", title: "Plan", maxChars: 5000 },
  { key: "plan confidence", title: "Plan Confidence", maxChars: 1000 },
  { key: "plan revisions", title: "Plan Revisions", maxChars: 3000 },
  { key: "latest reassessment", title: "Latest Reassessment", maxChars: 3000 },
  { key: "rejected approaches", title: "Rejected Approaches", maxChars: 3000 },
  { key: "execution snapshot", title: "Execution Snapshot", maxChars: 8000 },
  { key: "completed", title: "Completed", maxChars: 3000 },
  { key: "in progress", title: "In Progress", maxChars: 2000 },
  { key: "files modified", title: "Files Modified", maxChars: 2000 },
  { key: "remaining work", title: "Remaining Work", maxChars: 4000 },
  { key: "exact next action", title: "Exact Next Action", maxChars: 3000 },
  { key: "test / build status", title: "Test / Build Status", maxChars: 2000 },
  { key: "resume prompt", title: "Resume Context", maxChars: 5000 },
];

export const RESUME_FALLBACK_SECTIONS = [
  { key: "goal", title: "Goal", maxChars: 800 },
  { key: "parent quest", title: "Parent Quest", maxChars: 400 },
  { key: "research findings", title: "Important Findings", maxChars: 3000 },
  { key: "decisions made", title: "Decisions", maxChars: 3000 },
  { key: "constraints & rules", title: "Constraints & Rules", maxChars: 1000 },
  { key: "files examined", title: "Files Examined", maxChars: 1000 },
  { key: "files touched", title: "Files Modified", maxChars: 2000 },
  { key: "sub-quests", title: "Sub-Quests", maxChars: 1000 },
  {
    key: "quest refinements & user feedback loops",
    title: "Quest Refinements & User Feedback Loops",
    maxChars: 2000,
  },
];

export function extractFormattedResumeSection(
  target: { key: string; title: string; maxChars: number },
  sections: Map<string, MarkdownSection>,
  seenTitles: Set<string>,
  usedRawSections: Set<MarkdownSection>,
): string | null {
  if (seenTitles.has(target.title)) return null;
  const aliases = [target.key, ...(SECTION_ALIASES[target.key] || [])];
  let sec: MarkdownSection | undefined;
  for (const alias of aliases) {
    const found = sections.get(alias);
    if (found && !usedRawSections.has(found)) {
      sec = found;
      break;
    }
  }
  if (
    sec && sec.body && sec.body.trim() && sec.body.trim() !== "-" &&
    !sec.body.trim().startsWith("> Paste the verbatim user prompt here") &&
    !sec.body.trim().startsWith("> What we are trying to accomplish.")
  ) {
    let body = sec.body.trim();
    if (target.maxChars && body.length > target.maxChars) {
      body = body.slice(0, target.maxChars).trim() +
        "… [see quest file for full section]";
    }
    seenTitles.add(target.title);
    usedRawSections.add(sec);
    return `### ${target.title}\n${body}`;
  }
  return null;
}

export function extractResumeSections(
  sections: Map<string, MarkdownSection>,
): string[] {
  const seenTitles = new Set<string>();
  const usedRawSections = new Set<MarkdownSection>();
  const extracted: string[] = [];
  for (const target of RESUME_TARGET_SECTIONS) {
    const formatted = extractFormattedResumeSection(
      target,
      sections,
      seenTitles,
      usedRawSections,
    );
    if (formatted) extracted.push(formatted);
  }
  if (!seenTitles.has("Execution Snapshot")) {
    for (const fallback of RESUME_FALLBACK_SECTIONS) {
      const formatted = extractFormattedResumeSection(
        fallback,
        sections,
        seenTitles,
        usedRawSections,
      );
      if (formatted) extracted.push(formatted);
    }
  }
  return extracted;
}

function resolveQuestPath(): string {
  if (state.questId) {
    const p = questPath(state.questId);
    return p;
  }
  return "";
}

export async function loadActiveQuestResumeContext(
  opts?: { includeShards?: boolean },
): Promise<string> {
  const includeShards = opts?.includeShards ?? false;
  const path = resolveQuestPath();
  const loadFromPath = async (p: string): Promise<string> => {
    try {
      const content = await readFile(p, "utf8");
      if (!content) return "";
      let merged = content;
      if (includeShards) {
        const dir = p.endsWith("quest.md")
          ? p.slice(0, -"/quest.md".length)
          : p.slice(0, p.lastIndexOf("/"));
        const shardNames = ["plan.md", "research.md", "execution.md"];
        for (const n of shardNames) {
          try {
            const shard = await readFile(`${dir}/${n}`, "utf8");
            if (shard) merged += "\n\n" + shard;
          } catch {}
        }
      }
      const sections = parseMarkdownSections(merged);
      const extracted = extractResumeSections(sections);
      if (extracted.length === 0) return "";
      return `\n\n# Active Quest Resume Context (from \`${p}\`)\n${
        extracted.join("\n\n")
      }`;
    } catch (err: any) {
      // B3: ENOENT on first-create (quest dir exists before quest.md is written) is normal
      if (
        err?.code === "ENOENT" || String(err?.message || "").includes("ENOENT")
      ) {
        tryLog(
          "RESUME_CONTEXT_MISSING",
          `resume context not yet available (first-create): ${p}`,
          { quest: state.active || "" },
        );
        return "";
      }
      logError(
        `Failed to load resume context from ${p}`,
        err,
        undefined,
        QuestErrorCode.RESUME_STATE_INCONSISTENT,
      );
      return "";
    }
  };
  if (path) {
    const direct = await loadFromPath(path);
    if (direct) return direct;
  }
  if (!state.active) return "";
  const record = await resolveQuestRecordBySlug(state.active);
  if (!record) return "";
  return loadFromPath(record.path);
}
