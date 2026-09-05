import { readFile } from "node:fs/promises";
import { SECTION_ALIASES } from "./constants.ts";
import { slugify } from "./paths.ts";
import { MarkdownBlock, MarkdownSection } from "./types.ts";

function parseSubHeadings(
  body: string,
  parentHeading: string,
  sections: Map<string, MarkdownSection>,
): void {
  const subLines = body.split(/\r?\n/);
  let subHeading: string | null = null;
  let subLevel = 0;
  let subBodyLines: string[] = [];
  let subInCode = false;

  const flushSub = () => {
    if (subHeading !== null) {
      const subNorm = subHeading.trim().toLowerCase();
      const subBody = subBodyLines.join("\n").trim();
      if (!sections.has(subNorm)) {
        sections.set(subNorm, {
          heading: subHeading,
          normalized: subNorm,
          level: subLevel,
          body: subBody,
          raw: `### ${subHeading}\n${subBody}`,
        });
      }
    }
    subBodyLines = [];
  };

  for (const sLine of subLines) {
    if (/^\s*(```|~~~)/.test(sLine)) {
      subInCode = !subInCode;
      subBodyLines.push(sLine);
      continue;
    }
    const subMatch = sLine.match(/^(#{3,6})\s+(.+)$/);
    if (subMatch && !subInCode) {
      flushSub();
      subLevel = subMatch[1].length;
      subHeading = subMatch[2].trim();
    } else {
      subBodyLines.push(sLine);
    }
  }
  flushSub();
}

import { memoParseMarkdown } from "./utils/cache.ts";
import { createHash } from "node:crypto";

function hashForCache(content: string): string {
  return createHash("sha256").update(content, "utf8").digest("hex").slice(
    0,
    16,
  );
}

function parseMarkdownSectionsImpl(
  content: string,
): Map<string, MarkdownSection> {
  const sections = new Map<string, MarkdownSection>();
  if (!content) return sections;

  const lines = content.split(/\r?\n/);
  let currentHeading: string | null = null;
  let currentLevel = 0;
  let currentBodyLines: string[] = [];
  let inCodeBlock = false;

  const flush = () => {
    if (currentHeading !== null) {
      const norm = currentHeading.trim().toLowerCase();
      const body = currentBodyLines.join("\n").trim();
      sections.set(norm, {
        heading: currentHeading,
        normalized: norm,
        level: currentLevel,
        body,
        raw: `## ${currentHeading}\n${body}`,
      });

      if (currentLevel <= 2 && body.includes("###")) {
        parseSubHeadings(body, currentHeading, sections);
      }
    }
    currentBodyLines = [];
  };

  for (const line of lines) {
    const isMajorHeading = !line.startsWith("###") &&
      /^(#{1,2})\s+[A-Za-z]/.test(line);
    if (isMajorHeading) {
      inCodeBlock = false;
    }

    if (/^\s*(```|~~~)/.test(line)) {
      inCodeBlock = !inCodeBlock;
      currentBodyLines.push(line);
      continue;
    }

    const headingMatch = line.match(/^(#{1,2})\s+(.+)$/);
    if (headingMatch && !inCodeBlock) {
      flush();
      currentLevel = headingMatch[1].length;
      currentHeading = headingMatch[2].trim();
    } else {
      currentBodyLines.push(line);
    }
  }
  flush();

  return sections;
}

export function parseMarkdownSections(
  content: string,
): Map<string, MarkdownSection> {
  if (!content) return new Map();
  const h = hashForCache(content);
  return memoParseMarkdown(content, h, parseMarkdownSectionsImpl);
}

export function isCodeBlockDelimiter(line: string): boolean {
  return /^\s*(```|~~~)/.test(line);
}

export function parseHeadingMatch(
  line: string,
): { heading: string; title: string } | null {
  const match = line.match(/^(#{1,6}\s+)(.+)$/);
  if (!match) return null;
  return {
    heading: line,
    title: match[2].trim(),
  };
}

export function finalizePreambleBlock(
  preambleLines: string[],
): MarkdownBlock | null {
  if (preambleLines.length === 0) return null;
  const raw = preambleLines.join("\n");
  return {
    type: "preamble",
    body: raw,
    raw,
  };
}

export function finalizeSectionBlock(
  heading: string,
  title: string,
  bodyLines: string[],
): MarkdownBlock {
  const body = bodyLines.join("\n");
  return {
    type: "section",
    heading,
    title,
    normalizedTitle: title.trim().toLowerCase(),
    body,
    raw: `${heading}\n${body}`,
  };
}

export function parseMarkdownBlocks(content: string): MarkdownBlock[] {
  const blocks: MarkdownBlock[] = [];
  if (!content) return blocks;

  const lines = content.split(/\r?\n/);
  const currentPreambleLines: string[] = [];
  let currentHeading: string | null = null;
  let currentTitle: string | null = null;
  let currentBodyLines: string[] = [];
  let hasSeenFirstSection = false;
  let inCodeBlock = false;

  const flush = () => {
    if (!hasSeenFirstSection) {
      const preamble = finalizePreambleBlock(currentPreambleLines);
      if (preamble) blocks.push(preamble);
    } else if (currentHeading !== null && currentTitle !== null) {
      blocks.push(
        finalizeSectionBlock(currentHeading, currentTitle, currentBodyLines),
      );
    }
    currentBodyLines = [];
  };

  for (const line of lines) {
    const isMajorHeading = !line.startsWith("###") &&
      /^(#{1,2})\s+[A-Za-z]/.test(line);
    if (isMajorHeading) {
      inCodeBlock = false;
    }

    if (isCodeBlockDelimiter(line)) {
      inCodeBlock = !inCodeBlock;
      if (!hasSeenFirstSection) {
        currentPreambleLines.push(line);
      } else {
        currentBodyLines.push(line);
      }
      continue;
    }

    const headingMatch = !inCodeBlock ? parseHeadingMatch(line) : null;
    if (headingMatch) {
      flush();
      hasSeenFirstSection = true;
      currentHeading = headingMatch.heading;
      currentTitle = headingMatch.title;
    } else if (!hasSeenFirstSection) {
      currentPreambleLines.push(line);
    } else {
      currentBodyLines.push(line);
    }
  }
  flush();

  return blocks;
}

export function matchCanonicalKey(normalizedTitle: string): string | null {
  for (const [canonical, aliases] of Object.entries(SECTION_ALIASES)) {
    if (canonical === normalizedTitle || aliases.includes(normalizedTitle)) {
      return canonical;
    }
  }
  return null;
}

export function formatUninsertedSections(
  updates: Map<string, string>,
  usedCanonicalKeys: Set<string>,
): string[] {
  const uninsertedKeys = Array.from(updates.keys()).filter(
    (k) =>
      !usedCanonicalKeys.has(k) &&
      !usedCanonicalKeys.has(matchCanonicalKey(k) || ""),
  );
  if (uninsertedKeys.length === 0) return [];

  const newSections: string[] = [];
  for (const key of uninsertedKeys) {
    const val = updates.get(key)!.trim();
    if (!val) continue;
    const title = key
      .split(" ")
      .map((w) => w.charAt(0).toUpperCase() + w.slice(1))
      .join(" ");
    newSections.push(`## ${title}\n${val}`);
  }
  return newSections;
}

export function insertNewSectionsIntoBlocks(
  renderedBlocks: string[],
  newSections: string[],
): void {
  if (newSections.length === 0) return;
  const insertIdx = renderedBlocks.findIndex(
    (b) =>
      b.startsWith("## Remaining work") ||
      b.startsWith("## Next recommended step") ||
      b.startsWith("## Next action") ||
      b.startsWith("## Resume prompt") ||
      b.startsWith("## Resume context"),
  );
  if (insertIdx >= 0) {
    renderedBlocks.splice(insertIdx, 0, ...newSections);
  } else {
    renderedBlocks.push(...newSections);
  }
}

export function spliceMarkdownSections(
  originalContent: string,
  updates: Map<string, string>,
): string {
  const blocks = parseMarkdownBlocks(originalContent);
  if (blocks.length === 0) return "";

  const usedCanonicalKeys = new Set<string>();
  const renderedBlocks: string[] = [];

  for (const block of blocks) {
    if (block.type === "preamble") {
      renderedBlocks.push(block.body.trimEnd());
      continue;
    }

    const canonKey = block.normalizedTitle
      ? matchCanonicalKey(block.normalizedTitle)
      : null;
    if (canonKey && updates.has(canonKey)) {
      usedCanonicalKeys.add(canonKey);
      const newBody = updates.get(canonKey)!.trim();
      renderedBlocks.push(`${block.heading}\n${newBody}`);
    } else if (block.normalizedTitle && updates.has(block.normalizedTitle)) {
      usedCanonicalKeys.add(block.normalizedTitle);
      const newBody = updates.get(block.normalizedTitle)!.trim();
      renderedBlocks.push(`${block.heading}\n${newBody}`);
    } else {
      renderedBlocks.push(`${block.heading}\n${block.body.trim()}`);
    }
  }

  const newSections = formatUninsertedSections(updates, usedCanonicalKeys);
  insertNewSectionsIntoBlocks(renderedBlocks, newSections);

  return renderedBlocks.filter(Boolean).join("\n\n") + "\n";
}
