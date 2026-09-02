import { slugify } from "../../paths.ts";
import { parseMarkdownSections } from "../../markdown_parse.ts";

export function extractParentFromQuest(content: string): string | null {
	const sections = parseMarkdownSections(content);
	const parentSec = sections.get("parent quest") || sections.get("parent") || sections.get("parentquest");
	if (!parentSec || !parentSec.body) return null;
	const wikilinkMatch = parentSec.body.match(/\[\[([^\]]+)\]\]/);
	if (wikilinkMatch && wikilinkMatch[1]) {
		const val = wikilinkMatch[1].trim();
		if (val.toLowerCase() === "parent-quest-name") return null;
		return slugify(val);
	}
	const cleanLines = parentSec.body.split(/\r?\n/).map((l) => l.replace(/^>\s*/, "").trim()).filter(Boolean);
	if (cleanLines.length > 0) {
		const token = cleanLines[0].replace(/^-\s*\[[ x]\]\s*/, "").replace(/^-\s*/, "").trim();
		if (token.toLowerCase() === "parent-quest-name") return null;
		return token ? slugify(token) : null;
	}
	return null;
}

export function extractSubQuestsFromQuest(content: string): string[] {
	const sections = parseMarkdownSections(content);
	const subSec = sections.get("sub-quests") || sections.get("subquests") || sections.get("sub quests");
	if (!subSec || !subSec.body) return [];
	const results: string[] = [];
	const linkRegex = /\[\[([^\]]+)\]\]/g;
	let m: RegExpExecArray | null;
	while ((m = linkRegex.exec(subSec.body)) !== null) {
		if (m[1]) results.push(m[1].trim());
	}
	return results;
}

export function FUTURE_QUEST_TEMPLATE(name: string, goal = ""): string {
	const req = goal ? goal.trim().slice(0, 4000) : "";
	return [
		`# Proposal / Future Quest: ${name}`,
		``,
		`Status: **proposal**`,
		``,
		`## Goals & Scope`,
		goal ? goal : `> What are we proposing to change and why?`,
		``,
		`## Requirements`,
		req ? `- ${req}` : `- `,
		``,
		`## Implementation Plan`,
		`1. Investigate via read/search`,
		`2. Plan confidence low → revise`,
		``,
		`## Out of scope`,
		`- `,
		``
	].join("\n");
}

export function parseQuestId(content: string): string | null {
	if (!content || typeof content !== "string") return null;
	const frontmatterMatch = content.match(/^---\r?\n([\s\S]*?)\r?\n---/);
	if (frontmatterMatch) {
		const idMatch = frontmatterMatch[1].match(/^(?:questId|quest_id):\s*([a-zA-Z0-9_-]+)/mi);
		if (idMatch && idMatch[1]) return idMatch[1].trim();
	}
	const commentMatch = content.match(/<!--\s*(?:questId|quest_id):\s*([a-zA-Z0-9_-]+)\s*-->/i);
	if (commentMatch && commentMatch[1]) return commentMatch[1].trim();
	const sections = parseMarkdownSections(content);
	const sec = sections.get("quest id") || sections.get("questid");
	if (sec && sec.body) {
		const m = sec.body.match(/([a-zA-Z0-9_-]{7,16})/);
		if (m && m[1]) return m[1].trim();
	}
	const lines = content.split(/\r?\n/).slice(0, 25);
	for (const line of lines) {
		const m = line.match(/^(?:questId|quest_id):\s*([a-zA-Z0-9_-]+)/i);
		if (m && m[1]) return m[1].trim();
	}
	return null;
}

export function ensureQuestIdInContent(content: string, questId: string): string {
	if (!content || typeof content !== "string") {
		return `---\nquestId: ${questId}\n---\n`;
	}
	const existing = parseQuestId(content);
	if (existing) return content;
	const frontmatterMatch = content.match(/^---\r?\n([\s\S]*?)\r?\n---/);
	if (frontmatterMatch) {
		const newFm = `---\nquestId: ${questId}\n${frontmatterMatch[1].trim()}\n---`;
		return content.replace(/^---\r?\n[\s\S]*?\r?\n---/, newFm);
	}
	return `---\nquestId: ${questId}\n---\n\n${content.trimStart()}`;
}

export function buildTemplateHeader(name: string, goal = "", parent = "", originalRequest = "", questId = ""): string[] {
	const parentSec = parent
		? `## Parent Quest\n[[${parent}]]\n`
		: `## Parent Quest\n> If this is a sub-quest, reference the parent quest here (e.g. [[parent-quest-name]]).\n`;
	const requestBody = originalRequest
		? `> ${originalRequest}`
		: `> Paste the verbatim user prompt here (or very faithful summary if truncated). This section MUST stay faithful -- it is enforced by the extension.`;
	const headerLines: string[] = [];
	if (questId) headerLines.push(`---`, `questId: ${questId}`, `---`, ``);
	headerLines.push(
		`# Quest: ${name}`,
		``,
		`## Goal`,
		goal ? goal : `> What we are trying to accomplish.`,
		``,
		`## Original request`,
		requestBody,
		`>`,
		``,
		parentSec,
		`## Current Status`,
		`- [ ] research pending · plan provisional · plan confirmed · in progress · blocked · done`,
		``,
	);
	return headerLines;
}
