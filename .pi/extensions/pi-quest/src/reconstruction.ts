import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { CUSTOM_TYPE, LEGACY_CUSTOM_TYPE, QuestErrorCode, SECTION_ALIASES } from "./constants.ts";
import { syncImplementationPermission } from "./gates.ts";
import { ensureQuestIdInContent, parseMarkdownSections, parseQuestId } from "./markdown.ts";
import { logError } from "./messaging.ts";
import { fileExists, questPath, resolveQuestRecordBySlug } from "./paths.ts";
import { createDefaultState, generateQuestId, setSessionState, state } from "./state.ts";
import { CompactionTransaction, ExtensionContext, LoadedQuestState, MarkdownSection, PendingAgentNotification, PendingResume, PendingSubquestResumeResolution, StoredState } from "./types.ts";
import { updateUIStatus } from "./ui.ts";
import { isPlaceholderOrEmpty } from "./utils.ts";
import { validateResearchPrerequisites } from "./validation.ts";

export function parseSectionTimestamp(sec?: MarkdownSection): number | undefined {
	if (!sec || isPlaceholderOrEmpty(sec.body)) return undefined;
	const raw = sec.body.trim();
	const parsed = Number.parseInt(raw, 10);
	if (!Number.isNaN(parsed) && parsed > 0) {
		return parsed;
	}
	const dateParsed = Date.parse(raw);
	if (!Number.isNaN(dateParsed) && dateParsed > 0) {
		return dateParsed;
	}
	return undefined;
}

export function parseSectionInteger(sec?: MarkdownSection, defaultVal = 0, minVal = 0): number {
	if (!sec || isPlaceholderOrEmpty(sec.body)) return defaultVal;
	const parsed = Number.parseInt(sec.body.replace(/\D/g, ""), 10);
	if (!Number.isNaN(parsed) && parsed >= minVal) {
		return parsed;
	}
	return defaultVal;
}

export function parseSectionConfidence(sec?: MarkdownSection): "low" | "medium" | "high" {
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
): { reassessmentRequired: boolean; reassessmentReason: string | null; reassessmentEvidence: string | null } {
	let reassessmentRequired = false;
	let reassessmentReason: string | null = null;
	let reassessmentEvidence: string | null = null;

	const statusSec = sections.get("reassessment status") || sections.get("reassessment state");
	if (statusSec && !isPlaceholderOrEmpty(statusSec.body)) {
		if (statusSec.body.toUpperCase().includes("REQUIRED")) {
			reassessmentRequired = true;
			const match = statusSec.body.match(/REQUIRED[^-]*-(.*)$/i);
			if (match && match[1]) {
				reassessmentReason = match[1].trim();
			}
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
	const reqSec = sections.get("original request") || sections.get("original user request");
	if (!reqSec) return "";
	const rawText = reqSec.body.replace(/^>\s*/gm, "").trim();
	if (rawText && !rawText.startsWith("Paste the verbatim user prompt") && !rawText.startsWith("Goal:")) {
		return rawText;
	}
	return "";
}

export function parseRefinements(sections: Map<string, any>): string[] {
	const refSec = sections.get("quest refinements & user feedback loops") || sections.get("refinements");
	if (!refSec) return [];
	return refSec.body
		.split(/\r?\n/)
		.map((l: string) => l.replace(/^[-*]\s*/, "").replace(/^\d+\.\s*/, "").trim())
		.filter((l: string) => l && l !== "-" && !l.startsWith(">"));
}

export function parsePlanRevisionsText(sections: Map<string, any>): string | null {
	const revSec = sections.get("plan revisions") || sections.get("plan revision history") || sections.get("revisions");
	if (revSec && !isPlaceholderOrEmpty(revSec.body)) {
		return revSec.body.trim();
	}
	return null;
}

export function parseAwaitingUserConfirmation(sections: Map<string, any>): boolean {
	const currentStatusSec = sections.get("current status") || sections.get("status");
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
		if (bodyLower.includes("plan confirmed") || bodyLower.includes("in progress") || bodyLower.includes("done")) {
			return false;
		}
	}
	return false;
}

export async function loadExistingQuestEpistemicState(slugOrQid: string, basePath?: string): Promise<LoadedQuestState> {
	let path = "";
	if (basePath) {
		if (basePath.endsWith("quest.md") || basePath.endsWith(".md")) {
			path = basePath;
		} else {
			const nested = join(basePath, slugOrQid, "quest.md");
			if (await fileExists(nested)) {
				path = nested;
			} else {
				path = join(basePath, `${slugOrQid}.md`);
			}
		}
	} else {
		path = questPath(slugOrQid);
		if (!(await fileExists(path))) {
			const record = await resolveQuestRecordBySlug(slugOrQid);
			if (record) {
				path = record.path;
			}
		}
	}
	if (!path || !(await fileExists(path))) {
		return createDefaultEpistemicState(false);
	}
	try {
		const content = await readFile(path, "utf8");
		let questId = parseQuestId(content);
		if (!questId) {
			questId = generateQuestId();
			try {
				const updatedContent = ensureQuestIdInContent(content, questId);
				await writeFile(path, updatedContent, "utf8");
			} catch {}
		}

		const sections = parseMarkdownSections(content);
		const originalRequest = parseOriginalRequest(sections);
		const refinements = parseRefinements(sections);

		const planConfidence = parseSectionConfidence(sections.get("plan confidence") || sections.get("confidence"));
		const planVersion = parseSectionInteger(sections.get("plan version") || sections.get("version"), 1, 1);
		const researchRound = parseSectionInteger(sections.get("research round") || sections.get("research cycle"), 1, 1);
		const lastResearchAt = parseSectionTimestamp(sections.get("last research at") || sections.get("last research timestamp") || sections.get("last research"));
		const lastPlanRevisionAt = parseSectionTimestamp(sections.get("last plan revision at") || sections.get("last plan revision timestamp") || sections.get("last plan revision"));
		const reassessmentVersion = parseSectionInteger(sections.get("reassessment version"), 0, 0);
		const resolvedReassessmentVersion = parseSectionInteger(sections.get("resolved reassessment version"), 0, 0);

		const { reassessmentRequired, reassessmentReason, reassessmentEvidence } = parseReassessmentState(
			sections,
			reassessmentVersion,
			resolvedReassessmentVersion,
		);

		const lastPlanRevisionsText = parsePlanRevisionsText(sections);
		const validation = validateResearchPrerequisites(content, planConfidence, true);
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

export async function loadExistingQuestIntent(slug: string): Promise<{ originalRequest: string; refinements: string[] }> {
	const loaded = await loadExistingQuestEpistemicState(slug);
	return { originalRequest: loaded.originalRequest, refinements: loaded.refinements };
}

export function extractChildResultSummary(content: string, name: string): string {
	const sections = parseMarkdownSections(content);
	const lines: string[] = [];

	const goalSec = sections.get("goal");
	if (goalSec && goalSec.body) {
		lines.push(`- **Goal**: ${goalSec.body.trim()}`);
	}

	const understandingSec = sections.get("current understanding");
	if (understandingSec && understandingSec.body && !understandingSec.body.startsWith(">")) {
		lines.push(`- **Established Understanding**:\n${understandingSec.body.trim()}`);
	}

	const findingsSec = sections.get("research findings") || sections.get("in-depth analysis & findings");
	if (findingsSec && findingsSec.body) {
		lines.push(`- **Findings & Discoveries**:\n${findingsSec.body.trim()}`);
	}

	const assumptionsSec = sections.get("key assumptions") || sections.get("assumptions");
	if (assumptionsSec && assumptionsSec.body) {
		lines.push(`- **Assumptions Evaluated**:\n${assumptionsSec.body.trim()}`);
	}

	const rejectedSec = sections.get("rejected approaches");
	if (rejectedSec && rejectedSec.body && !rejectedSec.body.startsWith(">")) {
		lines.push(`- **Rejected Approaches**:\n${rejectedSec.body.trim()}`);
	}

	const reassessSec = sections.get("latest reassessment") || sections.get("reassessment conclusion");
	if (reassessSec && reassessSec.body && !reassessSec.body.startsWith(">")) {
		lines.push(`- **Latest Reassessment Conclusion**:\n${reassessSec.body.trim()}`);
	}

	const decisionsSec = sections.get("decisions made") || sections.get("decisions");
	if (decisionsSec && decisionsSec.body) {
		lines.push(`- **Decisions Made**:\n${decisionsSec.body.trim()}`);
	}

	const filesSec = sections.get("files touched") || sections.get("files modified");
	if (filesSec && filesSec.body) {
		lines.push(`- **Files Touched**:\n${filesSec.body.trim()}`);
	}

	return lines.length > 0 ? lines.join("\n\n") : `- Completed sub-quest ${name}.`;
}

export const RESUME_TARGET_SECTIONS = [
	{ key: "original request", title: "Original Request", maxChars: 4000 },
	{ key: "current status", title: "Current Status", maxChars: 2000 },
	{ key: "current understanding", title: "Current Understanding", maxChars: 4000 },
	{ key: "key assumptions", title: "Key Assumptions", maxChars: 3000 },
	{ key: "open questions & uncertainties", title: "Open Questions & Uncertainties", maxChars: 3000 },
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
	{ key: "quest refinements & user feedback loops", title: "Quest Refinements & User Feedback Loops", maxChars: 2000 },
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
		sec &&
		sec.body &&
		sec.body.trim() &&
		sec.body.trim() !== "-" &&
		!sec.body.trim().startsWith("> Paste the verbatim user prompt here") &&
		!sec.body.trim().startsWith("> What we are trying to accomplish.")
	) {
		let body = sec.body.trim();
		if (target.maxChars && body.length > target.maxChars) {
			body = body.slice(0, target.maxChars).trim() + "… [see quest file for full section]";
		}
		seenTitles.add(target.title);
		usedRawSections.add(sec);
		return `### ${target.title}\n${body}`;
	}
	return null;
}

export function extractResumeSections(sections: Map<string, MarkdownSection>): string[] {
	const seenTitles = new Set<string>();
	const usedRawSections = new Set<MarkdownSection>();
	const extracted: string[] = [];

	for (const target of RESUME_TARGET_SECTIONS) {
		const formatted = extractFormattedResumeSection(target, sections, seenTitles, usedRawSections);
		if (formatted) extracted.push(formatted);
	}

	// If execution snapshot was not present, fall back to legacy individual sections
	if (!seenTitles.has("Execution Snapshot")) {
		for (const fallback of RESUME_FALLBACK_SECTIONS) {
			const formatted = extractFormattedResumeSection(fallback, sections, seenTitles, usedRawSections);
			if (formatted) extracted.push(formatted);
		}
	}

	return extracted;
}

export async function loadActiveQuestResumeContext(): Promise<string> {
	const path = questPath(state.questId);
	if (!path || !(await fileExists(path))) {
		if (!state.active) return "";
		const record = await resolveQuestRecordBySlug(state.active);
		if (!record || !(await fileExists(record.path))) return "";
		try {
			const content = await readFile(record.path, "utf8");
			if (!content) return "";
			const sections = parseMarkdownSections(content);
			const extracted = extractResumeSections(sections);
			if (extracted.length === 0) return "";
			return `\n\n# Active Quest Resume Context (from \`${record.path}\`)\n${extracted.join("\n\n")}`;
		} catch (err: any) {
			logError(`Failed to load resume context from ${record.path}`, err, undefined, QuestErrorCode.RESUME_STATE_INCONSISTENT);
			return "";
		}
	}
	try {
		const content = await readFile(path, "utf8");
		if (!content) return "";

		const sections = parseMarkdownSections(content);
		const extracted = extractResumeSections(sections);

		if (extracted.length === 0) return "";
		return `\n\n# Active Quest Resume Context (from \`${path}\`)\n${extracted.join("\n\n")}`;
	} catch (err: any) {
		logError(`Failed to load resume context from ${path}`, err, undefined, QuestErrorCode.RESUME_STATE_INCONSISTENT);
		return "";
	}
}

export function reconstructActiveTransaction(txData: any): CompactionTransaction | null {
	if (!txData || typeof txData !== "object") return null;
	return {
		id: String(txData.id || ""),
		phase: txData.phase || "in-flight",
		activeQuest: String(txData.activeQuest || ""),
		questPath: typeof txData.questPath === "string" ? txData.questPath : undefined,
		reason: txData.reason || "normal-compaction",
		checkpointSaveCount: typeof txData.checkpointSaveCount === "number" ? txData.checkpointSaveCount : undefined,
		checkpointHash: typeof txData.checkpointHash === "string" ? txData.checkpointHash : undefined,
		observedSaveCount: typeof txData.observedSaveCount === "number" ? txData.observedSaveCount : undefined,
		observedHash: typeof txData.observedHash === "string" ? txData.observedHash : undefined,
		observedQuestPath: typeof txData.observedQuestPath === "string" ? txData.observedQuestPath : undefined,
		stack: Array.isArray(txData.stack) ? [...txData.stack] : [],
		researchRound: Number(txData.researchRound || 1),
		reassessmentVersion: Number(txData.reassessmentVersion || 0),
		planVersion: Number(txData.planVersion || 1),
		createdAt: Number(txData.createdAt || Date.now()),
		completedAt: typeof txData.completedAt === "number" ? txData.completedAt : undefined,
		failedAt: typeof txData.failedAt === "number" ? txData.failedAt : undefined,
		error: typeof txData.error === "string" ? txData.error : undefined,
	};
}

export function reconstructPendingResume(pendingResume: any): PendingResume | null {
	if (!pendingResume || typeof pendingResume !== "object") return null;
	return {
		compactionId: String(pendingResume.compactionId || ""),
		activeQuest: String(pendingResume.activeQuest || ""),
		reason: pendingResume.reason || "normal-compaction",
		checkpointSaveCount: typeof pendingResume.checkpointSaveCount === "number"
			? Number(pendingResume.checkpointSaveCount)
			: (undefined as any),
		checkpointHash: typeof pendingResume.checkpointHash === "string"
			? String(pendingResume.checkpointHash)
			: (undefined as any),
		checkpointQuestPath: typeof pendingResume.checkpointQuestPath === "string"
			? String(pendingResume.checkpointQuestPath)
			: (undefined as any),
		attempts: Number(pendingResume.attempts || 0),
		createdAt: Number(pendingResume.createdAt || Date.now()),
		lastAttemptAt: typeof pendingResume.lastAttemptAt === "number" ? pendingResume.lastAttemptAt : undefined,
		deliveredAt: typeof pendingResume.deliveredAt === "number" ? pendingResume.deliveredAt : undefined,
	};
}

export function reconstructPendingNotifications(notifications: any): PendingAgentNotification[] {
	if (!Array.isArray(notifications)) return [];
	return notifications.map((n: any) => ({
		id: String(n.id || `notif_${Date.now()}`),
		code: String(n.code || ""),
		message: String(n.message || ""),
		deliverAs: n.deliverAs || "followUp",
		requiredNextAction: n.requiredNextAction,
		details: n.details,
		attempts: Number(n.attempts || 0),
		createdAt: Number(n.createdAt || Date.now()),
		lastAttemptAt: typeof n.lastAttemptAt === "number" ? n.lastAttemptAt : undefined,
	}));
}

export function reconstructPendingSubquestResolution(res: any): PendingSubquestResumeResolution | null {
	if (!res || typeof res !== "object") return null;
	return {
		child: String(res.child || ""),
		resolution: res.resolution || "obsolete-after-archive",
		resolvedAt: Number(res.resolvedAt || Date.now()),
		parent: res.parent ? String(res.parent) : null,
		details: typeof res.details === "string" ? res.details : undefined,
	};
}

export function loadPersistedJournalSnapshot(ctx: ExtensionContext): StoredState | undefined {
	let latest: StoredState | undefined;
	for (const entry of ctx.sessionManager.getBranch()) {
		if (entry.type === "custom" && (entry.customType === CUSTOM_TYPE || entry.customType === LEGACY_CUSTOM_TYPE) && entry.data) {
			latest = entry.data as unknown as StoredState;
		}
	}
	return latest;
}

export function restoreSessionState(latest: StoredState): StoredState {
	const txData = latest.activeTransaction && typeof latest.activeTransaction === "object"
		? (latest.activeTransaction as any)
		: null;
	const isCompacting = txData ? (txData.phase === "in-flight" || txData.phase === "prepared") : false;

	return {
		questId: latest.questId || null,
		active: latest.active || null,
		pendingRootQuest: typeof latest.pendingRootQuest === "boolean" ? latest.pendingRootQuest : false,
		pendingRootRequest: typeof latest.pendingRootRequest === "string" ? latest.pendingRootRequest : null,
		questIdentityEstablished: typeof latest.questIdentityEstablished === "boolean" ? latest.questIdentityEstablished : false,
		saveCount: latest.saveCount || 0,
		compactCount: latest.compactCount || 0,
		prompts: Array.isArray(latest.prompts) ? latest.prompts : [],
		refinements: Array.isArray(latest.refinements) ? latest.refinements : [],
		stack: Array.isArray(latest.stack) ? latest.stack : (latest.active ? [latest.active] : []),
		dirty: typeof latest.dirty === "boolean" ? latest.dirty : false,
		compactionPending: isCompacting || (typeof latest.compactionPending === "boolean" ? latest.compactionPending : false),
		archiveCompactionPending: null,
		subquestLaunchCompactionPending:
			typeof latest.subquestLaunchCompactionPending === "boolean"
				? latest.subquestLaunchCompactionPending
				: false,
		pendingSubquestResume:
			typeof latest.pendingSubquestResume === "string" ? latest.pendingSubquestResume : null,
		pendingSubquestResumeResolution: reconstructPendingSubquestResolution(latest.pendingSubquestResumeResolution),
		preCompactionCheckpointPending: false,
		preCompactionSaveRequestPending: false,
		saveGeneration: latest.saveGeneration || null,
		lastSavedHash: latest.lastSavedHash || null,
		economyTokens: typeof latest.economyTokens === "number" ? latest.economyTokens : undefined,
		economyPercent: typeof latest.economyPercent === "number" ? latest.economyPercent : undefined,
		warningMarginTokens: typeof latest.warningMarginTokens === "number" ? latest.warningMarginTokens : undefined,
		subquestCompactTokens: typeof latest.subquestCompactTokens === "number" ? latest.subquestCompactTokens : undefined,
		lastWarnedCompactionTokens: undefined,
		lastPromptAt: typeof latest.lastPromptAt === "number" ? latest.lastPromptAt : Date.now(),
		lastResumePromptAt: typeof latest.lastResumePromptAt === "number" ? latest.lastResumePromptAt : 0,
		lastResumeTarget: typeof latest.lastResumeTarget === "string" ? latest.lastResumeTarget : null,
		lastResumeCompactCount: typeof latest.lastResumeCompactCount === "number" ? latest.lastResumeCompactCount : undefined,
		activeTransaction: reconstructActiveTransaction(txData),
		activeCompactionId: typeof latest.activeCompactionId === "string" ? latest.activeCompactionId : null,
		lastDeliveredCompactionId: typeof latest.lastDeliveredCompactionId === "string" ? latest.lastDeliveredCompactionId : null,
		pendingResume: reconstructPendingResume(latest.pendingResume),
		pendingNotifications: reconstructPendingNotifications(latest.pendingNotifications),
		pickerCancelled: typeof latest.pickerCancelled === "boolean" ? latest.pickerCancelled : false,
		researchRound: typeof latest.researchRound === "number" ? latest.researchRound : 1,
		researchComplete: typeof latest.researchComplete === "boolean" ? latest.researchComplete : false,
		researchRequired: typeof latest.researchRequired === "boolean" ? latest.researchRequired : (!latest.researchComplete),
		reassessmentRequired: typeof latest.reassessmentRequired === "boolean" ? latest.reassessmentRequired : false,
		reassessmentReason: typeof latest.reassessmentReason === "string" ? latest.reassessmentReason : null,
		reassessmentEvidence: typeof latest.reassessmentEvidence === "string" ? latest.reassessmentEvidence : null,
		reassessmentVersion: typeof latest.reassessmentVersion === "number" ? latest.reassessmentVersion : 0,
		resolvedReassessmentVersion: typeof latest.resolvedReassessmentVersion === "number" ? latest.resolvedReassessmentVersion : 0,
		lastPlanRevisionsText: typeof latest.lastPlanRevisionsText === "string" ? latest.lastPlanRevisionsText : null,
		confirmedQuests: Array.isArray(latest.confirmedQuests) ? latest.confirmedQuests : [],
		lastReassessmentPromptAt: typeof latest.lastReassessmentPromptAt === "number" ? latest.lastReassessmentPromptAt : 0,
		lastReassessmentReason: typeof latest.lastReassessmentReason === "string" ? latest.lastReassessmentReason : null,
		lastCheckpointPromptAt: typeof latest.lastCheckpointPromptAt === "number" ? latest.lastCheckpointPromptAt : 0,
		planVersion: typeof latest.planVersion === "number" ? latest.planVersion : 1,
		planConfidence: (latest.planConfidence as any) || "low",
		lastResearchAt: typeof latest.lastResearchAt === "number" ? latest.lastResearchAt : Date.now(),
		lastPlanRevisionAt: typeof latest.lastPlanRevisionAt === "number" ? latest.lastPlanRevisionAt : Date.now(),
		lastPromptedReassessmentVersion: typeof latest.lastPromptedReassessmentVersion === "number" ? latest.lastPromptedReassessmentVersion : 0,
		implementationAllowed: false,
		awaitingUserConfirmation: typeof latest.awaitingUserConfirmation === "boolean" ? latest.awaitingUserConfirmation : false,
		consecutiveFailures: typeof latest.consecutiveFailures === "number" ? latest.consecutiveFailures : 0,
		substantiveTurnsSinceCheckpoint: typeof latest.substantiveTurnsSinceCheckpoint === "number" ? latest.substantiveTurnsSinceCheckpoint : 0,
		lastCriticalReview: latest.lastCriticalReview ? { ...latest.lastCriticalReview } : null,
		criticalReviews: Array.isArray(latest.criticalReviews) ? latest.criticalReviews.map((r) => ({ ...r })) : [],
		criticalReviewAttempts: latest.criticalReviewAttempts ? { ...latest.criticalReviewAttempts } : {},
		lastReviewedSaveHash: latest.lastReviewedSaveHash || null,
		lastReviewedPlanVersion: latest.lastReviewedPlanVersion || null,
		lastReviewedSaveCount: latest.lastReviewedSaveCount || null,
		investigationEpoch: typeof latest.investigationEpoch === "number" ? latest.investigationEpoch : 1,
		currentReceipt: latest.currentReceipt || null,
		lastCompletedReceipt: latest.lastCompletedReceipt || null,
	};
}

export function reconcileDerivedState(reconstructedState: StoredState, ctx: ExtensionContext): StoredState {
	syncImplementationPermission(reconstructedState);
	setSessionState(ctx, reconstructedState);
	updateUIStatus(ctx);
	return reconstructedState;
}

export function reconstruct(ctx: ExtensionContext): StoredState {
	try {
		const latest = loadPersistedJournalSnapshot(ctx);
		const reconstructedState: StoredState = latest && (latest.active || latest.pendingRootQuest)
			? restoreSessionState(latest)
			: createDefaultState();
		return reconcileDerivedState(reconstructedState, ctx);
	} catch (err: any) {
		logError("Failed to reconstruct state from session history", err, ctx, QuestErrorCode.STATE_RECONSTRUCTION_FAILURE);
		const fallback = createDefaultState();
		return reconcileDerivedState(fallback, ctx);
	}
}
