import { readFile } from "node:fs/promises";
import { SECTION_ALIASES } from "./constants.ts";
import { slugify } from "./paths.ts";
import { MarkdownBlock, MarkdownSection } from "./types.ts";

function parseSubHeadings(body: string, parentHeading: string, sections: Map<string, MarkdownSection>): void {
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

export function parseMarkdownSections(content: string): Map<string, MarkdownSection> {
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

export function isCodeBlockDelimiter(line: string): boolean {
	return /^\s*(```|~~~)/.test(line);
}

export function parseHeadingMatch(line: string): { heading: string; title: string } | null {
	const match = line.match(/^(#{1,6}\s+)(.+)$/);
	if (!match) return null;
	return {
		heading: line,
		title: match[2].trim(),
	};
}

export function finalizePreambleBlock(preambleLines: string[]): MarkdownBlock | null {
	if (preambleLines.length === 0) return null;
	const raw = preambleLines.join("\n");
	return {
		type: "preamble",
		body: raw,
		raw,
	};
}

export function finalizeSectionBlock(heading: string, title: string, bodyLines: string[]): MarkdownBlock {
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
			blocks.push(finalizeSectionBlock(currentHeading, currentTitle, currentBodyLines));
		}
		currentBodyLines = [];
	};

	for (const line of lines) {
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

export function formatUninsertedSections(updates: Map<string, string>, usedCanonicalKeys: Set<string>): string[] {
	const uninsertedKeys = Array.from(updates.keys()).filter(
		(k) => !usedCanonicalKeys.has(k) && !usedCanonicalKeys.has(matchCanonicalKey(k) || "")
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

export function insertNewSectionsIntoBlocks(renderedBlocks: string[], newSections: string[]): void {
	if (newSections.length === 0) return;
	const insertIdx = renderedBlocks.findIndex(
		(b) =>
			b.startsWith("## Remaining work") ||
			b.startsWith("## Next recommended step") ||
			b.startsWith("## Next action") ||
			b.startsWith("## Resume prompt") ||
			b.startsWith("## Resume context")
	);
	if (insertIdx >= 0) {
		renderedBlocks.splice(insertIdx, 0, ...newSections);
	} else {
		renderedBlocks.push(...newSections);
	}
}

export function spliceMarkdownSections(originalContent: string, updates: Map<string, string>): string {
	const blocks = parseMarkdownBlocks(originalContent);
	if (blocks.length === 0) return "";

	const usedCanonicalKeys = new Set<string>();
	const renderedBlocks: string[] = [];

	for (const block of blocks) {
		if (block.type === "preamble") {
			renderedBlocks.push(block.body.trimEnd());
			continue;
		}

		const canonKey = block.normalizedTitle ? matchCanonicalKey(block.normalizedTitle) : null;
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
	return [
		`# Proposal / Future Quest: ${name}`,
		``,
		`Status: **proposal**`,
		``,
		`## Goals & Scope`,
		goal ? goal : `> What are we proposing to change and why?`,
		``,
		`## Requirements`,
		`- `,
		``,
		`## Implementation Plan`,
		`1. `,
		``,
		`## Out of scope`,
		`- `,
		``
	].join("\n");
}

export function parseQuestId(content: string): string | null {
	if (!content || typeof content !== "string") return null;
	// 1. Check YAML frontmatter: --- ... questId: <id> ... ---
	const frontmatterMatch = content.match(/^---\r?\n([\s\S]*?)\r?\n---/);
	if (frontmatterMatch) {
		const idMatch = frontmatterMatch[1].match(/^(?:questId|quest_id):\s*([a-zA-Z0-9_-]+)/mi);
		if (idMatch && idMatch[1]) return idMatch[1].trim();
	}
	// 2. Check HTML comments: <!-- questId: ... -->
	const commentMatch = content.match(/<!--\s*(?:questId|quest_id):\s*([a-zA-Z0-9_-]+)\s*-->/i);
	if (commentMatch && commentMatch[1]) return commentMatch[1].trim();

	// 3. Check section '## Quest ID'
	const sections = parseMarkdownSections(content);
	const sec = sections.get("quest id") || sections.get("questid");
	if (sec && sec.body) {
		const m = sec.body.match(/([a-zA-Z0-9_-]{7,16})/);
		if (m && m[1]) return m[1].trim();
	}

	// 4. Fallback line anywhere in preamble or top 25 lines
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
	if (existing) {
		return content;
	}

	// If frontmatter exists, inject questId into it
	const frontmatterMatch = content.match(/^---\r?\n([\s\S]*?)\r?\n---/);
	if (frontmatterMatch) {
		const newFm = `---\nquestId: ${questId}\n${frontmatterMatch[1].trim()}\n---`;
		return content.replace(/^---\r?\n[\s\S]*?\r?\n---/, newFm);
	}

	// Prepend frontmatter
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
	if (questId) {
		headerLines.push(`---`, `questId: ${questId}`, `---`, ``);
	}

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

export function buildTemplateEpistemicSections(): string[] {
	return [
		`## Current Understanding`,
		`> Core architectural facts, execution paths, and component boundaries established by research.`,
		`- `,
		``,
		`## Key Assumptions`,
		`> Material assumptions supporting the approach (unverified | validated | invalidated).`,
		`- [ ] `,
		``,
		`## Open Questions & Uncertainties`,
		`> Material uncertainties to resolve through targeted investigation or testing.`,
		`- [ ] `,
		``,
		`## Research Findings`,
		`> Factual discoveries, data flow analysis, and tested hypotheses.`,
		`- `,
		``,
	];
}

export function buildTemplateMetadataSections(): string[] {
	const now = Date.now();
	return [
		`## Plan Version`,
		`1`,
		``,
		`## Research Round`,
		`1`,
		``,
		`## Last Research At`,
		`${now}`,
		``,
		`## Last Plan Revision At`,
		`${now}`,
		``,
	];
}

export function buildTemplatePlanSections(): string[] {
	return [
		`## Plan`,
		`> Multi-stage execution plan (treat as provisional until falsification pass completes).`,
		`1. `,
		``,
		`## Plan Confidence`,
		`> low · medium · high (justify confidence based on verified assumptions and evidence).`,
		``,
		`## Plan Revisions`,
		`> Record of plan changes: previous plan -> invalidating evidence -> new finding -> revised plan.`,
		`- Initial plan formulated.`,
		``,
		`## Latest Reassessment`,
		`> Records findings from the most recent fresh investigation when reassessment is triggered.`,
		`- `,
		``,
		`## Rejected Approaches`,
		`> Disproved hypotheses, failed attempts, and why they were abandoned.`,
		`- `,
		``,
	];
}

export function buildTemplateExecutionSnapshot(goal = ""): string[] {
	return [
		`## Execution Snapshot`,
		``,
		`### Objective`,
		goal ? `> ${goal}` : `> What we are trying to accomplish.`,
		``,
		`### Completed`,
		`- `,
		``,
		`### In Progress`,
		`- `,
		``,
		`### Important Discoveries`,
		`- `,
		``,
		`### Decisions`,
		`- `,
		``,
		`### Constraints`,
		`- `,
		``,
		`### Files Examined`,
		`- `,
		``,
		`### Files Modified`,
		`- `,
		``,
		`### Test / Build Status`,
		`- `,
		``,
		`### Known Problems / Uncertainties`,
		`- `,
		``,
		`### Remaining Work`,
		`- [ ] `,
		``,
		`### Exact Next Action`,
		`> `,
		``,
		`### Resume Context`,
		`> `,
		``,
	];
}

export function buildTemplateFooterSections(refinements: string[] = []): string[] {
	const refinementsBody = refinements && refinements.length > 0
		? refinements.map((r) => `- ${r}`).join("\n")
		: `- `;

	return [
		`## Decisions Made`,
		`- `,
		``,
		`## Constraints & Rules`,
		`- `,
		``,
		`## Files Touched`,
		`- `,
		``,
		`## Test / Build Status`,
		`- `,
		``,
		`## Sub-Quests`,
		`> Planned sub-quests, follow-ups, or tangent quests linked to this quest.`,
		`- [ ] `,
		``,
		`## Quest Refinements & User Feedback Loops`,
		`> Mid-workflow refinements, post-implementation iterations, and user adjustments.`,
		refinementsBody,
		``,
		`## Remaining Work`,
		`- [ ] `,
		``,
		`## Exact Next Action`,
		`> Most justified immediate action (investigate, verify assumption, revise plan, implement, run tests).`,
		``,
		`## Resume Context`,
		`> Concise epistemic briefing for continuing this quest without repeating solved research.`,
		``,
	];
}

export function QUEST_TEMPLATE(name: string, goal = "", parent = "", originalRequest = "", refinements: string[] = [], questId = ""): string {
	return [
		...buildTemplateHeader(name, goal, parent, originalRequest, questId),
		...buildTemplateEpistemicSections(),
		...buildTemplateMetadataSections(),
		...buildTemplatePlanSections(),
		...buildTemplateExecutionSnapshot(goal),
		...buildTemplateFooterSections(refinements),
	].join("\n");
}

export const MANDATORY_WORKFLOW_RULES = `

# MANDATORY QUEST WORKFLOW RULES & QUALITY GATES (STRICTLY ENFORCED)
Mandatory Quest Workflow Rules:
CRITICAL INSTRUCTION: You MUST strictly adhere to these workflow rules and project quality gates on every turn. Do not take shortcuts, do not invent unverified assumptions, and do not bypass verification steps.

When working on quests:
1. **Iterative Research, Provisional Planning & Falsification Protocol (Turn 1 Protocol)**:
   - Before writing or editing feature code, follow the iterative research protocol:
     \`research -> provisional understanding -> provisional plan -> challenge plan -> targeted research -> revised plan -> implementation\`.
   - **Research & Understand**: Establish relevant architecture, library contracts, module boundaries, and execution paths through targeted reading and call tracing.
   - **Discover Actual Problem Structure**: Discover how the problem naturally divides during research rather than inventing an artificial flat list of bullets. If the task breaks down into distinct subsystems, architectural concerns, or separable investigations, identify those workstreams early and map them as sub-quests linked into the parent coordination plan (\`[[subquest-name]]\`).
   - **Identify Assumptions & Uncertainties**: Explicitly list key assumptions your approach relies on and identify unresolved questions. Identify highest-risk assumptions and investigate them specifically.
   - **Formulate Provisional Plan**: Produce a provisional multi-stage plan and record it under \`## Plan\` in \`.pi/quest/current/<qid>/quest.md\`. Explicitly treat initial plans as provisional.
   - **Actively Challenge & Falsify the Plan**: Ask what evidence could prove this plan wrong. Inspect relevant tests/code to test critical assumptions. If the plan depends on unresolved uncertainties, perform another targeted research pass.
   - **Stopping Condition**: Research continues until relevant architecture is understood, important execution paths are verified, material assumptions are tested, plausible alternatives are considered, and major uncertainties are resolved or explicitly accepted (do NOT stop based on arbitrary file counts).
   - **Turn 1 Confirmation & Asking User Questions Protocol**:
     - In Turn 1 of a root/main quest (and whenever asking the user for input or confirmation):
       1. First perform research and update the quest file via \`quest_update_state\` during the tool execution phase.
       2. If research revealed natural workstreams, explain the decomposition clearly (e.g. "The problem breaks naturally into [N] independent areas: A, B... I'm separating those into sub-quests because each requires different investigation and can be verified independently.") and create sub-quests with \`quest_subquest({ switchNow: false })\`.
       3. Then present your research findings, key assumptions evaluated, and revised parent plan clearly to the user.
       4. To ask for user confirmation or input: either invoke the \`ask_questions\` tool (with structured options), OR emit your final question in plain text with **ZERO accompanying tool calls**.
       5. **CRITICAL**: NEVER emit tool calls (such as \`quest_update_state\`, \`quest_mark_saved\`, \`edit\`, \`write\`, \`bash\`) in the same turn that you ask a question to the user. Emitting tool calls causes the agent harness to execute the tool and immediately run another turn, blowing past the question without waiting for the user's answer. Once confirmed by the user (and inside child sub-quests), execution is autonomous across turns and tools without requiring manual user slash commands.
2. **Build & Run Discovery**: Discover how to build and run the project before editing code (\`make\`, \`make watch\`, test runners).
3. **Verification Strategy**: For each implementation stage, establish an appropriate verification strategy before implementation. Prefer tests-first when practical, but do not create artificial tests merely to satisfy this workflow.
4. **Iterative Build, Run & Test**: Feature implementation -> build -> run -> verify targeted tests.
5. **Dynamic Reassessment on Contradictory Evidence**:
   - The quest file is external working memory, not an infallible dogma.
   - Whenever execution encounters contradictory evidence (failed tests/commands, unexpected execution paths, undiscovered architecture, unexpected complexity, or user refinements), trigger a targeted reassessment before continuing.
   - Investigate the contradiction, challenge whether the current plan remains valid, record revisions in \`## Plan Revisions\` and \`## Rejected Approaches\`, update \`.pi/quest/current/<qid>/quest.md\`, and proceed with the revised Exact Next Action.
6. **Post-Implementation & User Feedback Loops**:
   - Expect and support user polish iterations at the end of a quest or sub-quest.
   - When the user provides feedback, refinements, or tweaks mid-quest or post-implementation, log them under \`## Quest Refinements & User Feedback Loops\`, update acceptance checklists, execute changes, and verify with tests until the user confirms satisfaction.
7. **Quest Completion & Wrap-Up Flow**:
   - **Root / Top-Level Quest Completion**: When all stages, features, and acceptance criteria are completed, restart the test daemon/server and execute the FULL test suite (\`make test\`) to verify zero errors or regressions. Only after the full test suite passes with zero failures, prompt the user via \`ask_questions\` with structured options (Refine anything, Archive quest and auto-compact, Archive quest without auto-compact, Change to manual mode).
   - **Sub-Quest Completion (Autonomous Continuation & Parent Reassessment)**: When finishing a child sub-quest, autonomously archive the sub-quest via \`quest_archive({ compact: boolean })\`. The parent quest receives the child's established findings, evaluates whether parent assumptions changed, updates its plan, and seamlessly resumes execution.
8. **Final Verification & Quality Gates**:
   - Zero compiler errors or warnings.
   - Zero debug artifacts (no leftover console.logs, prints, or scratch code).
   - Full test suite (\`make test\`) must pass with zero errors.`;

export const AUTONOMOUS_QUEST_MANAGEMENT_RULES = `

# Autonomous Quest Management (Zero Manual User Commands Needed)
You manage quests autonomously on disk in \`.pi/quest/current/<qid>/quest.md\`. The user should NEVER need to type manual slash commands.

1. **Continuous Durable Epistemic Memory**:
   - Treat \`.pi/quest/current/<qid>/quest.md\` as your durable working memory and epistemic record.
   - Proactively update it during normal execution whenever expensive-to-reconstruct discoveries, decisions, tested assumptions, plan revisions, or progress occur.
   - Persist: Current Understanding, Key Assumptions, Open Questions, Research Findings, Plan, Plan Confidence, Plan Revisions, Rejected Approaches, Files Touched, Test/Build Status, and Exact Next Action.
   - Criterion: *Would losing the current context force a fresh agent to repeat significant investigation, reconsider a rejected approach, or guess what to do next?* If yes, persist it.
   - Use \`quest_update_state\` or \`edit\` + \`quest_mark_saved\`.

2. **Auto-Initialize New Quest on Substantive Requests (Research-Grounded Formation)**:
   - When a substantive request arrives, the system enters provisional root initialization.
   - Do NOT immediately create a generic quest file with raw prompt slugification.
   - **Initial Investigation & Orientation**: First investigate relevant architecture, execution paths, constraints, and conventions using read/search/bash tools.
   - **Semantic Quest Identity**: Based on what you learned during research, establish a concise, intelligible semantic name for the body of work (e.g. 'persistent-agent-research', 'oauth-login-flow', 'editor-rendering-crash' — NOT words mechanically copied from the user's prompt).
   - **Initialize Durable Quest**: Call \`quest_update_state({ name: "<semantic-name>", goal: "...", understanding: "...", assumptions: [...], openQuestions: [...], findings: [...], plan: [...], planConfidence: "medium"|"high", exactNextAction: "...", researchComplete: true })\`. This creates \`.pi/quest/current/<qid>/quest.md\` populated with your actual research findings, preserves the verbatim user prompt in \`## Original request\`, and validates the epistemic state.
   - **First-Turn Response**: Summarize your research findings, the newly established architectural facts, why the chosen quest identity fits, and the proposed plan to the user.
   - **Turn 1 Confirmation**: Ask for user confirmation (using \`ask_questions\` or plain text with ZERO accompanying tool calls) before editing feature code. Once confirmed, execute autonomously.

3. **Auto-Refine Active Quest on User Feedback**:
   - When the user provides feedback or new requirements while a quest is active, it is automatically captured as a refinement.
   - Reassess whether the refinement changes the current plan, record changes in \`## Quest Refinements & User Feedback Loops\` and \`## Plan Revisions\`, and update the quest file.

4. **Auto-Create Sub-Quests & Meaningful Decomposition (LIFO Stack)**:
   - **Mental Model**: Decompose according to the discovered structure of the problem, not according to the number of bullets in the plan:
     \`research -> discover actual problem structure -> identify independently investigable/executable workstreams -> create sub-quests for useful separable work -> establish parent plan around them -> execute\`.
   - **When to Create Sub-Quests**: During initial research, explicitly look for work that has one or more of these properties:
     * a distinct subsystem, architectural concern, or execution path;
     * a self-contained investigation that could produce durable findings for the parent;
     * a task with its own assumptions and verification strategy;
     * a component that may require several implementation/verification steps;
     * a parallel or weakly coupled workstream;
     * a risky unknown that deserves independent investigation;
     * a side problem that would otherwise pollute the parent quest's context;
     * work that can be completed and then summarized back to the parent.
   - **When NOT to Create Sub-Quests (Avoid Over-Decomposition)**:
     Do NOT create sub-quests for:
     * a trivial one-command task;
     * a tiny edit;
     * an obvious sequential step that does not benefit from independent context;
     * every item in a checklist;
     * every file touched;
     * every test command.
   - **The Golden Criterion**:
     *Would this unit benefit from having its own durable research state, assumptions, findings, execution history, and context boundary?*
     If yes, create a sub-quest (\`quest_subquest({ name: "...", goal: "...", switchNow: false })\`). If no, keep it within the parent quest.
   - **Meaningful Decomposition**: Choose concise, semantic names (e.g. \`auth-callback-flow\`, \`session-persistence\`, \`frontend-auth-state\`).
   - **Allow Sub-Quests to Emerge from Research**: Begin with PROVISIONAL ROOT RESEARCH, discover during investigation how the work naturally divides into separate concerns, create the appropriate sub-quests, and structure the parent coordination plan around them.
   - **Sub-Quest Creation During Initial Planning**: If research reveals multiple genuinely separable workstreams, create sub-quests for them even when there are only two or three. If the work is tightly coupled, keep it within the parent quest even if there are several plan steps. (A plan with >3 distinct phases or complex components serves as a strong heuristic signal to evaluate for decomposition).
   - **Parent Plan Expressed in Terms of Sub-Quests**: The root plan should reference the actual sub-quests (\`[[sub-quest-name]]\`) rather than duplicating all child reasoning:
     \`\`\`text
     ## Plan
     1. Investigate and resolve [[sub-quest-1]]
     2. Investigate and resolve [[sub-quest-2]]
     3. Integrate the resulting changes and verify end-to-end
     \`\`\`
     The parent retains only coordination-level reasoning; detailed reasoning belongs in the child quest. This protects context economy in the parent.
   - **LIFO Execution Model & Context Inheritance**: Sub-quests operate on a LIFO stack. The parent remains the durable owner of the overall objective. The child inherits context as hypotheses to independently verify:
     \`read inherited context -> independently investigate -> identify assumptions -> test high-risk assumptions -> form provisional plan -> challenge plan -> targeted research if needed -> persist plan -> implement -> verify -> archive -> return findings to parent\`.
   - **Epistemic Hard Gates & Autonomous Child Execution**: Sub-quest creation does NOT bypass epistemic enforcement: a child cannot modify project code until its required research state is established in its own quest file. Once research requirements are satisfied, the child proceeds autonomously without a redundant human confirmation.
   - **Sub-Quest Completion**: When finishing a child sub-quest, call \`quest_archive()\` to pop the stack and autonomously return to the parent quest with a child-result summary!

5. **Auto-Archive Upon Completion (LIFO Pop)**:
   - **For Sub-Quests**: Autonomously archive via \`quest_archive({ compact: boolean })\` and return to parent.
   - **For Root Quests**: When finished, prompt user via \`ask_questions\` to refine or archive.

6. **Auto-Compaction & Autonomous Resumption**:
   - Context automatically compacts dynamically when approaching threshold.
   - When context approaches the compaction threshold, an explicit pre-compaction final save directive will instruct you to perform a final exhaustive state save before compaction.
   - **Durable Epistemic Recovery Following ANY Compaction**:
     - Immediately read \`.pi/quest/current/<qid>/quest.md\` (the single source of truth on disk).
     - Recover Current Understanding, Key Assumptions, Plan, Plan Confidence, and Exact Next Action.
     - Validate whether the current plan is still supported by the recovered evidence. Do not repeat research merely to reconstruct lost context; use the quest file to recover established knowledge. However, if an important assumption remains unverified, tests disagree, or new evidence contradicts the model, re-investigate that specific aspect before continuing.
     - Proceed directly with executing the justified Exact Next Action without waiting for user commands or modal questions.
7. **Faithful User Request**: The \`## Original request\` section MUST remain verbatim.

8. **Durable-State Reconciliation Protocol (Semantic Freshness & Invariant Sync)**:
   - **The Governing Invariant**: The quest file MUST describe what is true NOW, not what was true several turns ago, and not merely what was intended.
   - **Reconcile After Substantive Work**: After every substantive implementation, investigation, test/build run, reassessment, or sub-quest return, reconcile the entire execution snapshot:
     * **Dominance of Verified Events**: If a change was completed (e.g. constants added, function written, test fixed), remove it from \`Remaining Work\`, record it under \`Completed\`, list all modified files in \`Files Modified\`, update \`Test / Build Status\` (e.g. "Unit tests pending rerun after edits"), and advance \`Exact Next Action\` to the next step. Never leave completed tasks in \`Exact Next Action\` or \`Remaining Work\`.
     * **Exact Next Action is a Live Pointer**: Must answer: *"If a fresh agent opened this quest right now, what is the single most justified thing it should do next?"* Never repeat what was already done.
     * **Evidence-Calibrated Plan Confidence**: Plan confidence reflects evidence across the entire architecture (build system, module boundaries, SSR, WASM, UX integration), NOT just the simplicity of the immediate next edit. If overall integration remains unverified, keep confidence lower and explain why.
     * **Distinguish User Decisions from Engineering Conclusions**: User requirement answers only establish user intent; they do NOT prove engineering feasibility or correctness. Explicitly separate: *User decision*, *Verified engineering fact*, *Unverified assumption*, and *Open question*. Never claim "no uncertainties remaining" merely because the user answered a question.
     * **Meaningful Plan Versions**: When Plan Version increments, explain the revision in \`## Plan Revisions\` (\`Previous plan -> Evidence/contradiction -> New understanding -> Revised plan\`).
     * **Synchronize Epistemic & Execution Layers**: When execution produces evidence, update both epistemic state (understanding, assumptions, findings, confidence, plan) and execution state (completed, in progress, files modified, tests, remaining work, exact next action).
     * **Pre-Save Consistency Audit**: Immediately before calling \`quest_mark_saved\`, perform an internal audit (Does Completed contradict Remaining Work? Does Files Modified omit changed files? Does Exact Next Action repeat completed work? Does Plan Version have a revision? Does Test Status reflect recent edits?). Correct stale facts before marking saved.`;

export function getWorkflowInstructions(resumeContext: string): string {
	return `${MANDATORY_WORKFLOW_RULES}${AUTONOMOUS_QUEST_MANAGEMENT_RULES}${resumeContext}`;
}
