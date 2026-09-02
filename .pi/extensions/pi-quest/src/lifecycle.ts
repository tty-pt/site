import { createHash } from "node:crypto";
import { copyFileSync, existsSync } from "node:fs";
import { copyFile, readFile, writeFile, rename, mkdir, unlink, rm, stat } from "node:fs/promises";
import { basename, join, resolve } from "node:path";
import { FUTURE_DIR, QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR, QuestErrorCode } from "./constants.ts";
import { isCriticalReviewValidForCompletion, isSubagentAvailable, runCriticalReview } from "./critical_agent.ts";
import { appendChangelogEntry, findProjectRoot } from "./diagnostic.ts";
import { syncImplementationPermission } from "./gates.ts";
import { logEvent, logQuestTransition } from "./logging.ts";
import { extractParentFromQuest, parseMarkdownSections } from "./markdown.ts";
import { logError } from "./messaging.ts";
import { cleanDraftIfExists, fileExists, listActiveQuestRecords, listQuestFiles, questArchivePath, questDirPath, questPath, resolveQuestRecordBySlug, slugify } from "./paths.ts";
import { supersedeObligation } from "./obligations.ts";
import { persist, verifyAndMarkSaved } from "./persistence.ts";
import { loadExistingQuestEpistemicState } from "./reconstruction.ts";
import { startResearchEpoch } from "./research.ts";
import { createDefaultState, generateQuestId, getState, state } from "./state.ts";
import { ExtensionAPI, ExtensionContext, QuestChoiceResult } from "./types.ts";
import { updateUIStatus } from "./ui.ts";
import { resolveArchiveContext, type ArchiveContext } from "./lifecycle/archive/context.ts";
import { runRootCompletionGates } from "./lifecycle/archive/gates.ts";
import { finalizeTerminalState } from "./lifecycle/archive/terminal.ts";
import { pinLogToFinalized, removeActiveDirectory } from "./lifecycle/archive/removal.ts";
import { createArchiveZip } from "./lifecycle/archive/zip.ts";
import { hydrateNextActive, popArchivedAndFindNextActive } from "./lifecycle/archive/stack.ts";
import { applyLoadedEpistemicState } from "./lifecycle/epistemic_init.ts";

export type LifecycleStage = "terminal_commit" | "active_removal" | "zip_creation" | "changelog_appended";
export type LifecycleStageObserver = (stage: LifecycleStage, details: any) => void;

export let onLifecycleStageTransition: LifecycleStageObserver | null = null;
export function setLifecycleStageObserver(observer: LifecycleStageObserver | null): void {
	onLifecycleStageTransition = observer;
}

export function initProvisionalRootQuest(pi: ExtensionAPI, ctx: ExtensionContext, prompt: string): void {
	const questId = state.questId || generateQuestId();
	state.questId = questId;
	state.pendingRootQuest = true;
	state.pendingRootRequest = prompt;
	state.questIdentityEstablished = false;
	state.active = null;
	state.stack = [];
	state.prompts = [prompt];
	state.refinements = [];
	state.dirty = false;
	state.saveGeneration = null;
	state.lastSavedHash = null;
	state.consecutiveFailures = 0;
	state.substantiveTurnsSinceCheckpoint = 0;
	state.lastReassessmentPromptAt = 0;
	state.lastReassessmentReason = null;
	state.lastCheckpointPromptAt = 0;
	state.researchRound = 1;
	state.researchComplete = false;
	state.researchRequired = true;
	state.reassessmentRequired = false;
	state.reassessmentReason = null;
	state.reassessmentEvidence = null;
	state.reassessmentVersion = 0;
	state.resolvedReassessmentVersion = 0;
	state.lastPlanRevisionsText = null;
	state.planVersion = 1;
	state.planConfidence = "low";
	state.lastResearchAt = Date.now();
	state.lastPlanRevisionAt = Date.now();
	state.awaitingUserConfirmation = false;
	startResearchEpoch(state, "research");

	logQuestTransition("QUEST_DETECTED", "substantive root prompt detected", { reason: "new_substantive_prompt" });
	logQuestTransition("QUEST_CREATED", "provisional root quest created", { quest: "" });

	syncImplementationPermission(state);
	persist(pi, ctx);
	updateUIStatus(ctx);
}

export async function activateExistingQuest(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	slug: string,
	promptText?: string,
): Promise<boolean> {
	if (!slug) return false;
	let targetQid = slug;
	let path = questPath(slug);
	let questName = slug;

	const record = await resolveQuestRecordBySlug(slug);
	if (record) {
		targetQid = record.qid;
		path = record.path;
		questName = record.name;
	}

	const futurePath = `${FUTURE_DIR}/${slug}.md`;
	const isExistingOnDisk = await fileExists(path);

	if (!isExistingOnDisk && (await fileExists(futurePath))) {
		targetQid = generateQuestId();
		path = questPath(targetQid);
		await mkdir(questDirPath(targetQid), { recursive: true });
		try {
			const archDir = join(questDirPath(targetQid), "future-archive");
			try { if (!existsSync(archDir)) { const { mkdirSync } = await import("node:fs"); mkdirSync(archDir, { recursive: true }); } } catch {}
			const destArch = join(archDir, basename(futurePath));
			try { copyFileSync(futurePath, destArch); } catch { try { await copyFile(futurePath, destArch); } catch {} }
			try { const content = await readFile(futurePath, "utf8"); const h = createHash("sha256").update(content).digest("hex").slice(0, 12); logEvent("DRAFT_DISCARDED" as any, `draft discarded`, { quest: state.active || slug, slug, hash: h, dest: destArch, reason: "activateExistingQuest" } as any); } catch {}
		} catch {}
		await rename(futurePath, path);
		if (ctx?.hasUI) ctx.ui.notify(`Promoted draft ${futurePath} → ${path}`, "info");
	} else if (!isExistingOnDisk) {
		logQuestTransition("QUEST_ACTIVATION_FAILED", `failed to activate quest '${slug}': file not found`, { quest: slug, reason: "file_not_found" });
		return false;
	}

	await cleanDraftIfExists(slug, ctx);

	state.questId = targetQid;
	state.pendingRootQuest = false;
	state.pendingRootRequest = null;
	state.questIdentityEstablished = true;
	state.pickerCancelled = false;
	state.active = questName;
	state.stack = [questName];
	state.dirty = false;
	state.saveGeneration = null;
	state.lastSavedHash = null;
	state.consecutiveFailures = 0;
	state.substantiveTurnsSinceCheckpoint = 0;
	state.lastReassessmentPromptAt = 0;
	state.lastReassessmentReason = null;
	state.lastCheckpointPromptAt = 0;

	const loaded = await loadExistingQuestEpistemicState(targetQid);
	if (loaded.questId) state.questId = loaded.questId;
	applyLoadedEpistemicState(state, loaded, promptText || undefined);
	await verifyAndMarkSaved(pi, ctx, questName);
	persist(pi, ctx);
	updateUIStatus(ctx);

	logQuestTransition("QUEST_REUSED", `reusing existing quest '${questName}'`, { quest: questName });

	return true;
}

export async function ensureRootQuestForPrompt(
	pi: ExtensionAPI,
	ctx: ExtensionContext,
	prompt: string,
): Promise<boolean> {
	if (state.active) return false;
	const trimmed = prompt.trim();
	if (!trimmed) return false;

	const activeRecords = await listActiveQuestRecords();
	for (const r of activeRecords) {
		if ((r.name.length >= 3 && trimmed.toLowerCase().includes(r.name.toLowerCase())) || trimmed.toLowerCase().includes(r.qid.toLowerCase())) {
			return await activateExistingQuest(pi, ctx, r.name, trimmed);
		}
	}
	const futureFiles = await listQuestFiles(FUTURE_DIR);
	for (const f of futureFiles) {
		const s = f.replace(/\.md$/, "");
		if (s.length >= 3 && trimmed.toLowerCase().includes(s.toLowerCase())) {
			return await activateExistingQuest(pi, ctx, s, trimmed);
		}
	}

	initProvisionalRootQuest(pi, ctx, trimmed);
	return true;
}

export function checkOrdinaryCompletionConditions(
	markdownContent: string,
	s: any,
): { satisfied: boolean; reason?: string } {
	if (s.reassessmentRequired) {
		return {
			satisfied: false,
			reason: `Reassessment is pending (v${s.reassessmentVersion || 1}: ${s.reassessmentReason || "contradiction detected"}). Resolve reassessment before completing quest.`,
		};
	}

	if (s.dirty) {
		return {
			satisfied: false,
			reason: "Active quest has unsaved changes. Save and mark saved before completing quest.",
		};
	}

	if (markdownContent) {
		const sections = parseMarkdownSections(markdownContent);
		const remainingSec = sections.get("remaining work") || sections.get("remaining tasks") || sections.get("remaining");
		if (remainingSec && remainingSec.body) {
			const lines = remainingSec.body.split(/\r?\n/);
			const unchecked = lines
				.map((l) => l.trim())
				.filter((l) => {
					if (!l.startsWith("- [ ]") && !l.startsWith("* [ ]") && !l.startsWith("[ ]")) return false;
					const taskText = l.replace(/^[-*]?\s*\[\s*\]\s*/, "").trim();
					return taskText.length > 0 && !taskText.startsWith(">") && taskText !== "-" && !/^(none|n\/a|no remaining work|no remaining tasks)$/i.test(taskText);
				});
			if (unchecked.length > 0) {
				const sample = unchecked.slice(0, 3).map((u) => u.replace(/^[-*]?\s*\[\s*\]\s*/, "")).join("; ");
				return {
					satisfied: false,
					reason: `Unfinished tasks in Remaining Work: ${sample}${unchecked.length > 3 ? ` (and ${unchecked.length - 3} more)` : ""}`,
				};
			}
		}
	}

	return { satisfied: true };
}

export async function archiveQuestFile(name: string, pi: ExtensionAPI, ctx?: ExtensionContext): Promise<{ success: boolean; message: string; dest?: string; nextActive?: string | null; childSummary?: string }> {
	const s = getState(ctx);
	const ctxRes = await resolveArchiveContext(name, ctx);
	if ("error" in ctxRes) return { success: false, message: ctxRes.error };
	const { targetQid, path, questName, parentSlug, questContent, childSummary } = ctxRes as ArchiveContext;
	const stack = Array.isArray(s.stack) ? [...s.stack] : (s.active ? [s.active] : []);

	if (!parentSlug) {
		const gateRes = await runRootCompletionGates(questContent, s, pi, ctx as ExtensionContext, questName);
		if (gateRes.blocked) return { success: false, message: gateRes.message! };
	}

	const isRoot = !parentSlug;
	const questId = targetQid;
	let finalArchiveZipPath = questArchivePath(questId);

	if (s.activeTransaction && (s.activeTransaction.activeQuest === questName || s.activeTransaction.questPath === path)) {
		s.activeTransaction = null; s.activeCompactionId = null;
	}
	if (state.activeTransaction && (state.activeTransaction.activeQuest === questName || state.activeTransaction.questPath === path)) {
		state.activeTransaction = null; state.activeCompactionId = null;
	}
	if (s.pendingResume && (s.pendingResume.activeQuest === questName || s.pendingResume.checkpointQuestPath === path)) s.pendingResume = null;
	if (state.pendingResume && (state.pendingResume.activeQuest === questName || state.pendingResume.checkpointQuestPath === path)) state.pendingResume = null;
	supersedeObligation(s, (obl) => obl.questId === questName || obl.questId === questId, "Quest archived");
	if (s !== state) supersedeObligation(state, (obl) => obl.questId === questName || obl.questId === questId, "Quest archived");
	s.archiveCompactionPending = null; state.archiveCompactionPending = null;
	s.compactionPending = false; state.compactionPending = false;
	s.preCompactionCheckpointPending = false; state.preCompactionCheckpointPending = false;
	s.preCompactionSaveRequestPending = false; state.preCompactionSaveRequestPending = false;

	const projectRoot = findProjectRoot(ctx?.cwd);
	let verifiedLogContent = "";
	let verifiedQuestContent = questContent;
	let finalizedHierarchy: any = null;
	let authoritativeTerminalStatus: "COMPLETED" | "FAILED" = "COMPLETED";
	let isCompleted = true;

	if (isRoot) {
		const termRes = await finalizeTerminalState(pi, ctx, questName, questId, path, questContent, projectRoot,
			() => existsSync(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid)) || existsSync(questDirPath(targetQid)),
			() => existsSync(questArchivePath(questId, projectRoot)) || existsSync(questArchivePath(questId)));
		if (termRes.error) return { success: false, message: termRes.error };
		verifiedLogContent = termRes.verifiedLogContent;
		verifiedQuestContent = termRes.verifiedQuestContent;
		finalizedHierarchy = termRes.finalizedHierarchy;
		authoritativeTerminalStatus = termRes.authoritativeTerminalStatus;
		isCompleted = authoritativeTerminalStatus === "COMPLETED";

		await pinLogToFinalized(targetQid, verifiedLogContent, projectRoot);
		await removeActiveDirectory(targetQid, questName, projectRoot, ctx,
			() => existsSync(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid)) || existsSync(questDirPath(targetQid)),
			() => existsSync(questArchivePath(questId, projectRoot)) || existsSync(questArchivePath(questId)));

		finalArchiveZipPath = await createArchiveZip(projectRoot, questId, questName, authoritativeTerminalStatus, finalizedHierarchy, verifiedLogContent, verifiedQuestContent,
			() => existsSync(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid)) || existsSync(questDirPath(targetQid)),
			() => existsSync(questArchivePath(questId, projectRoot)) || existsSync(questArchivePath(questId)), ctx);

		await appendChangelogEntry(projectRoot, questName, childSummary || questName, authoritativeTerminalStatus.toLowerCase(), isCompleted, questId);
		onLifecycleStageTransition?.("changelog_appended", {
			questId, questName,
			zipExists: existsSync(questArchivePath(questId, projectRoot)) || existsSync(questArchivePath(questId)),
			activeDirExists: existsSync(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid)) || existsSync(questDirPath(targetQid)),
		});
	} else {
		const { rm } = await import("node:fs/promises");
		if (path.endsWith(".md") && !path.endsWith("quest.md")) await rm(path, { force: true });
		await cleanDraftIfExists(questName, ctx);
		const { logSubquestTransition } = await import("./logging.ts");
		logSubquestTransition("ARCHIVE", `archived quest ${questName}`, { quest: questName, subquest: questName, parent: parentSlug || undefined, dest: finalArchiveZipPath });
	}

	const { nextActive, stack: newStack } = await popArchivedAndFindNextActive(stack, questName, parentSlug, s);
	await hydrateNextActive(nextActive, newStack, s, questName, parentSlug, pi, ctx as ExtensionContext);

	const returnMsg = nextActive ? ` Resumed parent/previous quest '${nextActive}' (LIFO stack).` : "";
	const message = !parentSlug ? `Quest archived\nid: ${questId}\narchive: ${finalArchiveZipPath}` : `Archived sub-quest ${questName} [${questId}].${returnMsg}`;
	return { success: true, message, dest: finalArchiveZipPath, nextActive, childSummary };
}

export async function promptForQuestChoice(ctx: ExtensionContext, title = "Select quest:"): Promise<QuestChoiceResult | null> {
	if (!ctx.hasUI || ctx.mode !== "tui") return null;
	const activeRecords = await listActiveQuestRecords();
	const future = await listQuestFiles(FUTURE_DIR);
	const choices: string[] = [];

	for (const r of activeRecords) {
		choices.push(state.active === r.name ? `${r.name} (active)` : r.name);
	}
	for (const f of future) {
		const name = f.replace(/\.md$/, "");
		if (!activeRecords.some((r) => r.name === name)) {
			choices.push(`${name} (draft)`);
		}
	}
	choices.push("New quest…", "Cancel");

	const choice = await ctx.ui.select(title, choices);
	if (!choice || choice === "Cancel") return null;

	if (choice === "New quest…") {
		const nameInput = (await ctx.ui.input("Enter short quest name / slug (e.g. expand-editor-textarea):")) ?? "";
		const trimmedName = nameInput.trim();
		if (!trimmedName) return null;
		const name = slugify(trimmedName, 45);
		if (!name) return null;
		const goalInput = (await ctx.ui.input("Describe what you want to accomplish (optional):")) ?? "";
		const trimmedGoal = goalInput.trim();
		return { name, goal: trimmedGoal || trimmedName };
	}

	const clean = choice.replace(/ \(active\)$/, "").replace(/ \(draft\)$/, "");
	const name = slugify(clean);
	return name ? { name } : null;
}
