import { readdir, readFile, writeFile, mkdir } from "node:fs/promises";
import { FUTURE_DIR, QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR } from "./constants.ts";
import { logResumeTransition, logSubquestTransition } from "./logging.ts";
import { QUEST_TEMPLATE, parseMarkdownSections } from "./markdown.ts";
import { logError, reportAgentError, sendInternalAgentMessage } from "./messaging.ts";
import { fileExists, questArchivePath, questDirPath, questPath, resolveQuestRecordBySlug } from "./paths.ts";
import { generateQuestId, state } from "./state.ts";
import { ExtensionAPI, ExtensionContext, LoadedQuestState, QuestErrorCode, StoredState, SubquestReconciliationStatus } from "./types.ts";
import { startResearchEpoch } from "./research.ts";

async function hasPositiveSubquestCompletionEvidence(
	childSlug: string,
	parentSlug?: string | null,
	qid?: string | null,
): Promise<boolean> {
	// 1. Check if marked completed in parent quest markdown
	if (parentSlug) {
		const parentRecord = await resolveQuestRecordBySlug(parentSlug);
		const parentPath = parentRecord ? parentRecord.path : questPath(qid || state.questId || parentSlug);
		try {
			const parentContent = await readFile(parentPath, "utf8");
			const lines = parentContent.split(/\r?\n/);
			const isCompleted = lines.some(
				(l) => l.includes(`[[${childSlug}]]`) && /^\s*-\s*\[[xX]\]/.test(l),
			);
			if (isCompleted) return true;
		} catch {}
	}

	// 2. Check if archived evidence exists in .pi/quest/archive/<qid>.zip
	if (qid) {
		const zipPath = questArchivePath(qid);
		if (await fileExists(zipPath)) return true;
	}

	return false;
}

export async function reconcilePendingSubquestResume(
	pendingChild: string,
	targetState: StoredState,
	pi?: ExtensionAPI,
	ctx?: ExtensionContext,
): Promise<SubquestReconciliationStatus> {
	if (!pendingChild) return "obsolete";

	// Case A: Still valid if pending child matches current authoritative active quest and exists
	if (targetState.active === pendingChild) {
		const childCurrentPath = questPath(targetState.questId);
		const childExistsInCurrent = await fileExists(childCurrentPath);
		if (childExistsInCurrent) {
			return "still-valid";
		}
	}

	// Case B: Child has actually completed/archived and parent is now authoritative
	// Determine this from POSITIVE completion/archive evidence on disk
	const hasEvidence = await hasPositiveSubquestCompletionEvidence(pendingChild, targetState.active, targetState.questId);

	if (hasEvidence) {
		targetState.pendingSubquestResumeResolution = {
			child: pendingChild,
			resolution: "obsolete-after-archive",
			resolvedAt: Date.now(),
			parent: targetState.active || null,
		};
		targetState.pendingSubquestResume = null;
		logResumeTransition("RESUME_OBSOLETED", `pending subquest resume for '${pendingChild}' obsoleted after archive`, {
			quest: targetState.active || "",
			child: pendingChild,
			parent: targetState.active || undefined,
			reason: "archived",
		});
		return "obsolete";
	}

	// Case C: Child is neither active nor demonstrably completed
	// Keep pending obligation and report agent-visible error
	const childCurrentPath = questPath(targetState.questId);
	const childExistsInCurrent = await fileExists(childCurrentPath);
	if (pi) {
		const errorMsg = `[Quest Journal] PENDING_RESUME_INCONSISTENT\n\nA pending sub-quest continuation exists for '${pendingChild}', but the current authoritative quest state does not establish that continuation as active or completed.\n\nRequired action:\nreconcile the quest hierarchy and durable child/parent state before continuing.`;
		reportAgentError(
			pi,
			ctx,
			errorMsg,
			{
				code: QuestErrorCode.PENDING_RESUME_INCONSISTENT,
				requiredNextAction: "reconcile the quest hierarchy and durable child/parent state before continuing.",
				details: {
					PendingChild: pendingChild,
					AuthoritativeActive: targetState.active || "(none)",
					ChildFileExists: childExistsInCurrent,
					ArchiveEvidenceFound: hasEvidence,
				},
			},
		);
	}

	return "inconsistent";
}

export async function linkSubQuestInParent(parentSlug: string, childSlug: string, description = "", ctx?: ExtensionContext): Promise<boolean> {
	if (!parentSlug || !childSlug || parentSlug === childSlug) return false;
	const parentRecord = await resolveQuestRecordBySlug(parentSlug);
	let targetPath = parentRecord ? parentRecord.path : "";
	if (!targetPath && state.active === parentSlug && state.questId) {
		targetPath = questPath(state.questId);
	}
	const futurePath = `${FUTURE_DIR}/${parentSlug}.md`;
	let effectivePath = targetPath;
	try {
		if (!effectivePath) throw new Error("empty");
		await readFile(effectivePath, "utf8");
	} catch {
		effectivePath = "";
		try {
			await readFile(futurePath, "utf8");
			effectivePath = futurePath;
		} catch {
			effectivePath = "";
		}
	}
	targetPath = effectivePath;
	if (!targetPath) {
		const qid = generateQuestId();
		await mkdir(questDirPath(qid), { recursive: true });
		targetPath = questPath(qid);
		await writeFile(targetPath, QUEST_TEMPLATE(parentSlug, ""), "utf8");
	}

	try {
		let content = await readFile(targetPath, "utf8");
		const linkEntry = description ? `- [ ] [[${childSlug}]] - ${description}` : `- [ ] [[${childSlug}]]`;

		const sections = parseMarkdownSections(content);
		const subSec = sections.get("sub-quests") || sections.get("subquests") || sections.get("sub quests");

		if (subSec && subSec.body && subSec.body.includes(`[[${childSlug}]]`)) {
			return true;
		}

		if (subSec) {
			const cleanedBody = subSec.body.replace(/- \[\s*\]\s*(\n|$)/g, "").replace(/^-\s*(\n|$)/gm, "").trimEnd();
			const newSectionBody = cleanedBody ? `${cleanedBody}\n${linkEntry}` : `> Sub-quests, follow-ups, or tangent quests spawned from this quest.\n${linkEntry}`;
			const regex = new RegExp(`(##\\s+${subSec.heading}\\s*\\n)([\\s\\S]*?)(?=\\n##\\s+|$)`, "i");
			content = content.replace(regex, `$1${newSectionBody}\n\n`);
		} else {
			const insertBeforeRegex = /\n(##\s+(?:Why this matters|Decisions made|Constraints & Rules|Remaining work))/i;
			const newSection = `\n## Sub-Quests\n> Sub-quests, follow-ups, or tangent quests spawned from this quest.\n${linkEntry}\n`;
			if (insertBeforeRegex.test(content)) {
				content = content.replace(insertBeforeRegex, `${newSection}\n$1`);
			} else {
				content = `${content.trimEnd()}\n${newSection}\n`;
			}
		}

		await writeFile(targetPath, content, "utf8");
		return true;
	} catch (err: any) {
		logError(`Failed to link sub-quest in ${targetPath}`, err, ctx, QuestErrorCode.SUBQUEST_FAILURE);
		return false;
	}
}

export async function markSubQuestCompletedInParent(parentSlug: string, childSlug: string, ctx?: ExtensionContext): Promise<boolean> {
	const parentRecord = await resolveQuestRecordBySlug(parentSlug);
	const parentPath = parentRecord ? parentRecord.path : questPath(state.questId || parentSlug);
	try {
		await readFile(parentPath, "utf8");
	} catch {
		return false;
	}
	try {
		const content = await readFile(parentPath, "utf8");
		const lines = content.split(/\r?\n/);
		let matched = false;
		const updatedLines = lines.map((line) => {
			if (line.includes(childSlug) && /^\s*-\s*\[\s*\]/.test(line)) {
				matched = true;
				return line.replace(/^(\s*-\s*\[)\s*(\]\s*.*)$/, "$1x$2");
			}
			return line;
		});
		if (matched) {
			await writeFile(parentPath, updatedLines.join("\n"), "utf8");
			return true;
		}
	} catch (err: any) {
		logError(`Failed to mark subquest ${childSlug} completed in ${parentPath}`, err, ctx, QuestErrorCode.SUBQUEST_FAILURE);
	}
	return false;
}

export function sendChildReturnParentPrompt(
	pi: ExtensionAPI,
	parentName: string,
	childName: string,
	childSummary: string,
	ctx?: ExtensionContext,
) {
	const parentPath = questPath(state.questId);
	const directiveText = `⚡ **Sub-Quest '${childName}' Completed — Parent Evaluation Directive**:
Child sub-quest **${childName}** has completed and been archived. You have returned to parent quest **${parentName}**.

**Child Sub-Quest Results & Established Findings**:
${childSummary}

**Parent Evaluation & Resumption Protocol**:
Do not blindly resume the parent's previous Exact Next Action without evaluating whether the child's findings changed anything.
1. **Inspect Child Findings**: Review what sub-quest **${childName}** established.
2. **Determine Impact on Parent**: Determine whether parent assumptions or current plan were affected.
3. **If Affected**:
   - Trigger/enter reassessment (investigate the contradiction or unexpected findings).
   - Perform targeted investigation into affected areas.
   - Revise or re-validate the plan under \`## Plan Revisions\` in \`${parentPath}\`.
   - Update \`${parentPath}\` with \`quest_update_state({ reassessmentComplete: true, reassessmentConclusion: "...", ... })\`.
4. **If Not Affected**:
   - Record the child result under \`## Research Findings\` and mark the sub-quest completed in \`${parentPath}\`.
   - Preserve the existing parent plan and confidence.
   - Save the quest file via \`quest_mark_saved\` or \`quest_update_state\` (do NOT pass \`reassessmentComplete: true\` unless reassessment was actually required).
   - Continue with the justified next action.
5. Proceed autonomously with parent quest execution.`;

	sendInternalAgentMessage(pi, directiveText, "followUp");
	logSubquestTransition("SUBQUEST_COMPLETE", `subquest ${childName} completed, returned to ${parentName}`, {
		quest: parentName,
		subquest: childName,
		child: childName,
		parent: parentName,
	});
}

export function pushSubquestToStack(targetState: StoredState, parentName: string, name: string): void {
	targetState.pickerCancelled = false;
	if (!Array.isArray(targetState.stack)) targetState.stack = [];
	if (parentName && !targetState.stack.includes(parentName)) {
		targetState.stack.push(parentName);
	}
	if (!targetState.stack.includes(name)) {
		targetState.stack.push(name);
	} else {
		const idx = targetState.stack.lastIndexOf(name);
		targetState.stack = targetState.stack.slice(0, idx + 1);
	}
	targetState.active = name;
}

export function applyLoadedSubquestState(
	targetState: StoredState,
	goal: string,
	isExisting: boolean,
	subLoaded: LoadedQuestState,
): void {
	if (subLoaded.exists && isExisting) {
		targetState.prompts = subLoaded.originalRequest ? [subLoaded.originalRequest] : [goal];
		targetState.refinements = subLoaded.refinements;
		targetState.researchRound = subLoaded.researchRound;
		targetState.researchComplete = subLoaded.researchComplete;
		targetState.researchRequired = subLoaded.researchRequired;
		targetState.planVersion = subLoaded.planVersion;
		targetState.planConfidence = subLoaded.planConfidence;
		targetState.lastPlanRevisionsText = subLoaded.lastPlanRevisionsText;
		targetState.reassessmentRequired = subLoaded.reassessmentRequired;
		targetState.reassessmentReason = subLoaded.reassessmentReason;
		targetState.reassessmentEvidence = subLoaded.reassessmentEvidence;
		targetState.reassessmentVersion = subLoaded.reassessmentVersion;
		targetState.resolvedReassessmentVersion = subLoaded.resolvedReassessmentVersion;
		targetState.lastResearchAt = subLoaded.lastResearchAt ?? Date.now();
		targetState.lastPlanRevisionAt = subLoaded.lastPlanRevisionAt ?? Date.now();
	} else {
		targetState.prompts = [goal];
		targetState.refinements = [];
		targetState.researchRound = 1;
		targetState.researchComplete = false;
		targetState.researchRequired = true;
		targetState.reassessmentRequired = false;
		targetState.reassessmentReason = null;
		targetState.reassessmentEvidence = null;
		targetState.reassessmentVersion = 0;
		targetState.resolvedReassessmentVersion = 0;
		targetState.lastPlanRevisionsText = null;
		targetState.planVersion = 1;
		targetState.planConfidence = "low";
		targetState.lastResearchAt = Date.now();
		targetState.lastPlanRevisionAt = Date.now();
		targetState.awaitingUserConfirmation = false;
		startResearchEpoch(targetState, "research");
	}
	targetState.saveGeneration = null;
	targetState.lastSavedHash = null;
	targetState.dirty = false;
}

export function buildSubquestProtocolInstructions(
	name: string,
	goal: string,
	parentName: string,
	path: string,
): string {
	const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
	return `Now working on sub-quest **${name}**${parentName ? ` (parent: **${parentName}**)` : ""}. Sub-quest file: \`${path}\`.${goalText}

**Sub-Quest Iterative Research & Execution Protocol**:
Sub-quests do NOT inherit the parent's conclusions as immutable facts; treat them as context and hypotheses to independently verify.

1. Read \`${path}\` to inspect inherited context and goal.
2. Independently investigate the relevant subsystem, execution paths, and dependencies.
3. Identify assumptions inherited or required, and test high-risk assumptions directly.
4. Formulate a provisional plan, challenge it against potential failure modes, and revise if needed.
5. Update \`${path}\` (Current Understanding, Key Assumptions, Plan, Plan Confidence, Exact Next Action) and call \`quest_mark_saved\`.
6. Autonomously execute implementation and verify with tests without waiting for user confirmation.
7. Upon completion, archive via \`quest_archive()\` to return findings to the parent quest.`;
}
