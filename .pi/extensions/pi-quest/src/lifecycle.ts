import { existsSync } from "node:fs";
import { readFile, writeFile, rename, mkdir, unlink, rm, stat } from "node:fs/promises";
import { basename, resolve } from "node:path";
import { FUTURE_DIR, QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR, QuestErrorCode } from "./constants.ts";
import { isCriticalReviewValidForCompletion, isSubagentAvailable, runCriticalReview } from "./critical_agent.ts";
import { appendChangelogEntry, calculateAuthoritativeTerminalStatus, createRunArchive, findProjectRoot, resolveActiveRunHierarchy } from "./diagnostic.ts";
import { syncImplementationPermission } from "./gates.ts";
import { logEvent, logImplementationOutcome, logQuestTransition, logSubquestTransition, pinQuestLog, summarizeQuestJournalLog } from "./logging.ts";
import { extractParentFromQuest, parseMarkdownSections, spliceMarkdownSections } from "./markdown.ts";
import { logError } from "./messaging.ts";
import { cleanDraftIfExists, fileExists, listActiveQuestRecords, listQuestFiles, questArchivePath, questDirPath, questLogPath, questPath, resolveQuestRecordBySlug, slugify } from "./paths.ts";
import { persist, verifyAndMarkSaved } from "./persistence.ts";
import { extractChildResultSummary, loadExistingQuestEpistemicState } from "./reconstruction.ts";
import { startResearchEpoch } from "./research.ts";
import { createDefaultState, generateQuestId, getState, state } from "./state.ts";
import { markSubQuestCompletedInParent } from "./subquest.ts";
import { ExtensionAPI, ExtensionContext, QuestChoiceResult } from "./types.ts";
import { updateUIStatus } from "./ui.ts";

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
	if (loaded && loaded.exists) {
		if (loaded.questId) {
			state.questId = loaded.questId;
		}
		state.prompts = loaded.originalRequest ? [loaded.originalRequest] : promptText ? [promptText] : [];
		state.refinements = loaded.refinements;
		state.researchRound = loaded.researchRound;
		state.researchComplete = loaded.researchComplete;
		state.researchRequired = loaded.researchRequired;
		state.planVersion = loaded.planVersion;
		state.planConfidence = loaded.planConfidence;
		state.lastPlanRevisionsText = loaded.lastPlanRevisionsText;
		state.reassessmentRequired = loaded.reassessmentRequired;
		state.reassessmentReason = loaded.reassessmentReason;
		state.reassessmentEvidence = loaded.reassessmentEvidence;
		state.reassessmentVersion = loaded.reassessmentVersion;
		state.resolvedReassessmentVersion = loaded.resolvedReassessmentVersion;
		state.lastResearchAt = loaded.lastResearchAt ?? Date.now();
		state.lastPlanRevisionAt = loaded.lastPlanRevisionAt ?? Date.now();
		state.awaitingUserConfirmation = !loaded.researchComplete;
		if (loaded.researchComplete) {
			state.currentReceipt = null;
			state.lastCompletedReceipt = {
				epoch: 0,
				epochType: "historical",
				startedAt: loaded.lastResearchAt || Date.now(),
				completedAt: loaded.lastResearchAt || Date.now(),
				toolCalls: 0,
				readTargets: [],
				searchTargets: [],
				commands: [],
				evidenceCount: 0,
				isHistorical: true,
			};
		} else {
			startResearchEpoch(state, state.reassessmentRequired ? "reassessment" : "research");
		}
	} else {
		startResearchEpoch(state, "research");
	}

	syncImplementationPermission(state);
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
	let targetQid = s.questId || name;
	let path = questPath(targetQid);
	let questName = name;

	const record = await resolveQuestRecordBySlug(name);
	if (record) {
		targetQid = record.qid;
		path = record.path;
		questName = record.name;
	} else if (!(await fileExists(path))) {
		return { success: false, message: `No quest file found at ${path}` };
	}

	let parentSlug: string | null = null;
	const stack = Array.isArray(s.stack) ? [...s.stack] : (s.active ? [s.active] : []);
	const idx = stack.lastIndexOf(questName);
	if (idx > 0) {
		parentSlug = stack[idx - 1];
	}
	if (!parentSlug) {
		const targetRec = await resolveQuestRecordBySlug(questName);
		if (targetRec && targetRec.parent) {
			parentSlug = targetRec.parent;
		}
	}
	let childSummary = "";
	let questContent = "";
	try {
		questContent = await readFile(path, "utf8");
		if (!parentSlug) {
			parentSlug = extractParentFromQuest(questContent);
		}
		childSummary = extractChildResultSummary(questContent, questName);
	} catch (err: any) {
		logError(`Failed to read quest file for parent extraction at ${path}`, err, ctx, QuestErrorCode.STATE_RECONSTRUCTION_FAILURE);
	}

	if (!parentSlug) {
		// 1. Check ordinary completion conditions first (applies to root quests)
		const ordinaryCheck = checkOrdinaryCompletionConditions(questContent, s);
		if (!ordinaryCheck.satisfied) {
			return {
				success: false,
				message: `Quest completion blocked by unmet completion conditions: ${ordinaryCheck.reason}`,
			};
		}

		// 2. Root quest completion gate: Final Acceptance Review
		if (isSubagentAvailable(pi, ctx)) {
			const isValidPass = isCriticalReviewValidForCompletion(s);
			if (!isValidPass) {
				const reviewRes = await runCriticalReview(pi, ctx!, { kind: "final_acceptance", questSlug: questName });
				if (reviewRes.available && (!reviewRes.success || reviewRes.review?.verdict !== "PASS")) {
					const reason = reviewRes.review?.findings?.map((f) => f.issue).join("; ") || reviewRes.error || "Acceptance criteria unmet";
					return {
						success: false,
						message: `Final critical acceptance review failed (${reviewRes.review?.verdict || "ERROR"}${reviewRes.review?.severity ? ` / ${reviewRes.review.severity}` : ""}): ${reason}. Resolve findings before completing quest.`,
					};
				}
			}

			// Re-verify that critical review is STILL valid after review execution and that no state mutation occurred
			if (!isCriticalReviewValidForCompletion(s)) {
				return {
					success: false,
					message: "Final critical acceptance review PASS is missing or invalidated by subsequent state changes.",
				};
			}
		}
	}

	// 3. Establish and persist authoritative terminal state
	const isRoot = !parentSlug;
	const questId = targetQid;
	let finalArchiveZipPath = questArchivePath(questId);

	// Invalidate any pending compaction transactions or resume obligations for the quest being archived
	if (s.activeTransaction && (s.activeTransaction.activeQuest === questName || s.activeTransaction.questPath === path)) {
		s.activeTransaction = null;
		s.activeCompactionId = null;
	}
	if (state.activeTransaction && (state.activeTransaction.activeQuest === questName || state.activeTransaction.questPath === path)) {
		state.activeTransaction = null;
		state.activeCompactionId = null;
	}
	if (s.pendingResume && (s.pendingResume.activeQuest === questName || s.pendingResume.checkpointQuestPath === path)) {
		s.pendingResume = null;
	}
	if (state.pendingResume && (state.pendingResume.activeQuest === questName || state.pendingResume.checkpointQuestPath === path)) {
		state.pendingResume = null;
	}
	s.archiveCompactionPending = null;
	state.archiveCompactionPending = null;
	s.compactionPending = false;
	state.compactionPending = false;
	s.preCompactionCheckpointPending = false;
	state.preCompactionCheckpointPending = false;
	s.preCompactionSaveRequestPending = false;
	state.preCompactionSaveRequestPending = false;

	const projectRoot = findProjectRoot(ctx?.cwd);

	let verifiedLogContent = "";
	let verifiedQuestContent = questContent;
	let finalizedHierarchy: any = null;
	let authoritativeTerminalStatus: "COMPLETED" | "FAILED" = "COMPLETED";
	let isCompleted = true;

	if (isRoot) {
		// Explicitly write terminal quest state to disk using existing template sections
		const terminalUpdates = new Map<string, string>();
		terminalUpdates.set("current status", "Completed");
		terminalUpdates.set("final status", "COMPLETED");
		terminalUpdates.set("exact next action", "None");
		terminalUpdates.set("remaining work", "- [x] All tasks completed");
		const terminalQuestContent = spliceMarkdownSections(questContent, terminalUpdates);
		try {
			await writeFile(path, terminalQuestContent, "utf8");
		} catch (err: any) {
			return {
				success: false,
				message: `Failed to write terminal quest state to disk at ${path}: ${err?.message || String(err)}`,
			};
		}

		// Log terminal completion events to the live execution log
		logImplementationOutcome("IMPLEMENTATION_COMPLETED", `quest '${questName}' completed successfully`, {
			quest: questName,
			status: "COMPLETED",
		});
		logSubquestTransition("ARCHIVE", `archived quest ${questName}`, {
			quest: questName,
			subquest: questName,
			dest: finalArchiveZipPath,
			status: "COMPLETED",
		});

		// Durably verify persisted terminal state on disk before destructive removal
		const saveVerification = await verifyAndMarkSaved(pi, ctx, questName);
		if (!saveVerification.success) {
			return {
				success: false,
				message: `Failed to durably verify terminal quest state on disk: ${saveVerification.error}`,
			};
		}

		// Read the committed, verified disk state
		try {
			verifiedLogContent = await readFile(questLogPath(targetQid), "utf8");
		} catch {
			verifiedLogContent = "";
		}
		try {
			verifiedQuestContent = await readFile(path, "utf8");
		} catch {
			verifiedQuestContent = terminalQuestContent;
		}

		// Snapshot finalized run hierarchy from verified live state
		finalizedHierarchy = await resolveActiveRunHierarchy(projectRoot, { questId });

		// Derive authoritative terminal outcome from verified disk state and execution log
		let logSummaryInfo: ReturnType<typeof summarizeQuestJournalLog> | null = null;
		try {
			if (existsSync(questLogPath(targetQid))) {
				logSummaryInfo = summarizeQuestJournalLog(questLogPath(targetQid));
			}
		} catch {}

		const calculatedStatus = calculateAuthoritativeTerminalStatus(finalizedHierarchy, logSummaryInfo, "COMPLETED");
		authoritativeTerminalStatus = calculatedStatus === "FAILED" ? "FAILED" : "COMPLETED";
		isCompleted = authoritativeTerminalStatus === "COMPLETED";

		const checkActiveDirExists = () => existsSync(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid)) || existsSync(questDirPath(targetQid));
		const checkZipExists = () => existsSync(questArchivePath(questId, projectRoot)) || existsSync(questArchivePath(questId));

		onLifecycleStageTransition?.("terminal_commit", {
			questId,
			questName,
			authoritativeTerminalStatus,
			verifiedQuestContent,
			verifiedLogContent,
			activeDirExists: checkActiveDirExists(),
			zipExists: checkZipExists(),
		});

		// Pin log target to finalized sink before active directory removal
		const finalizedLogDir = resolve(projectRoot, ".pi/quest/finalized_logs");
		await mkdir(finalizedLogDir, { recursive: true });
		const pinnedLogPath = resolve(finalizedLogDir, `${targetQid}.log`);
		if (verifiedLogContent) {
			await writeFile(pinnedLogPath, verifiedLogContent, "utf8");
		}
		pinQuestLog(targetQid, pinnedLogPath);

		// 4. Archive / remove active quest directory from disk
		await rm(resolve(projectRoot, QUEST_CURRENT_DIR, targetQid), { recursive: true, force: true });
		if (existsSync(questDirPath(targetQid))) {
			await rm(questDirPath(targetQid), { recursive: true, force: true });
		}
		await cleanDraftIfExists(questName, ctx);

		onLifecycleStageTransition?.("active_removal", {
			questId,
			questName,
			activeDirExists: checkActiveDirExists(),
			zipExists: checkZipExists(),
		});

		// 5. Create diagnostic run zip from the finalized archived/run state (post-completion artifact)
		try {
			const archiveRes = await createRunArchive({
				projectRoot,
				questId,
				status: authoritativeTerminalStatus,
				hierarchy: finalizedHierarchy,
				finalizedLogContent: verifiedLogContent,
				finalizedQuestContent: verifiedQuestContent,
			});
			finalArchiveZipPath = archiveRes.zipPath;
		} catch (err: any) {
			logError(`Automatic run archive generation failed for quest '${questName}'`, err, ctx);
		}

		onLifecycleStageTransition?.("zip_creation", {
			questId,
			questName,
			zipPath: finalArchiveZipPath,
			activeDirExists: checkActiveDirExists(),
			zipExists: checkZipExists(),
		});

		// 6. Append changelog entry using the already-established authoritative outcome
		await appendChangelogEntry(
			projectRoot,
			questName,
			childSummary || questName,
			authoritativeTerminalStatus.toLowerCase(),
			isCompleted,
			questId,
		);

		onLifecycleStageTransition?.("changelog_appended", {
			questId,
			questName,
			zipExists: checkZipExists(),
			activeDirExists: checkActiveDirExists(),
		});
	} else {
		// Subquest archival
		if (path.endsWith(".md") && !path.endsWith("quest.md")) {
			await rm(path, { force: true });
		}
		await cleanDraftIfExists(questName, ctx);

		logSubquestTransition("ARCHIVE", `archived quest ${questName}`, {
			quest: questName,
			subquest: questName,
			parent: parentSlug || undefined,
			dest: finalArchiveZipPath,
		});
	}

	// LIFO stack management: remove archived quest from stack
	if (idx >= 0) {
		stack.splice(idx, 1);
	}

	// Find the top valid quest remaining on the LIFO stack
	let nextActive: string | null = null;
	while (stack.length > 0) {
		const candidate = stack[stack.length - 1];
		const candRecord = await resolveQuestRecordBySlug(candidate);
		if (candRecord && (await fileExists(candRecord.path))) {
			nextActive = candidate;
			s.questId = candRecord.qid;
			break;
		} else if (await fileExists(questPath(candidate))) {
			nextActive = candidate;
			s.questId = candidate;
			break;
		}
		stack.pop();
	}

	// Fallback to parent from file if stack had no active candidate
	if (!nextActive && parentSlug) {
		const parentRecord = await resolveQuestRecordBySlug(parentSlug);
		if (parentRecord && (await fileExists(parentRecord.path))) {
			nextActive = parentSlug;
			s.questId = parentRecord.qid;
			stack.push(parentSlug);
		} else if (await fileExists(questPath(parentSlug))) {
			nextActive = parentSlug;
			s.questId = parentSlug;
			stack.push(parentSlug);
		}
	}

	// Mark sub-quest completed (- [x]) in parent quest file
	if (parentSlug) {
		await markSubQuestCompletedInParent(parentSlug, questName, ctx);
	}

	if (s.active === questName || state.active === questName) {
		s.active = nextActive;
		s.stack = stack;
		state.active = nextActive;
		state.stack = stack;
		if (nextActive) {
			const parentLoaded = await loadExistingQuestEpistemicState(s.questId || nextActive);
			s.prompts = parentLoaded.originalRequest ? [parentLoaded.originalRequest] : [];
			s.refinements = parentLoaded.refinements;
			s.researchRound = parentLoaded.researchRound;
			s.researchComplete = parentLoaded.researchComplete;
			s.researchRequired = parentLoaded.researchRequired;
			s.reassessmentRequired = parentLoaded.reassessmentRequired;
			s.reassessmentReason = parentLoaded.reassessmentReason;
			s.reassessmentEvidence = parentLoaded.reassessmentEvidence;
			s.reassessmentVersion = parentLoaded.reassessmentVersion;
			s.resolvedReassessmentVersion = parentLoaded.resolvedReassessmentVersion;
			s.planVersion = parentLoaded.planVersion;
			s.planConfidence = parentLoaded.planConfidence;
			s.lastPlanRevisionsText = parentLoaded.lastPlanRevisionsText;
			s.lastResearchAt = parentLoaded.lastResearchAt ?? Date.now();
			s.lastPlanRevisionAt = parentLoaded.lastPlanRevisionAt ?? Date.now();
			s.awaitingUserConfirmation = false;
			if (parentLoaded.reassessmentRequired) {
				startResearchEpoch(s, "reassessment");
			} else if (parentLoaded.researchComplete) {
				s.currentReceipt = null;
				s.lastCompletedReceipt = {
					epoch: 0,
					epochType: "historical",
					startedAt: parentLoaded.lastResearchAt || Date.now(),
					completedAt: parentLoaded.lastResearchAt || Date.now(),
					toolCalls: 0,
					readTargets: [],
					searchTargets: [],
					commands: [],
					evidenceCount: 0,
					isHistorical: true,
				};
			} else {
				startResearchEpoch(s, "research");
			}
			syncImplementationPermission(s);
		} else {
			s.prompts = [];
			s.refinements = [];
			s.researchRequired = false;
			s.researchComplete = false;
			s.reassessmentRequired = false;
			s.reassessmentReason = null;
			s.reassessmentEvidence = null;
			s.reassessmentVersion = 0;
			s.resolvedReassessmentVersion = 0;
			s.awaitingUserConfirmation = false;
			syncImplementationPermission(s);
		}
		if (state !== s) {
			Object.assign(state, s);
		}
	} else {
		s.stack = stack;
		state.stack = stack;
	}

	if (nextActive) {
		await verifyAndMarkSaved(pi, ctx, nextActive);
		persist(pi, ctx);
		logSubquestTransition("SUBQUEST_RETURN", `returned to quest '${nextActive}' (LIFO stack)`, { quest: nextActive, parent: nextActive, child: questName });
	} else {
		persist(pi, ctx);
	}

	const returnMsg = nextActive ? ` Resumed parent/previous quest '${nextActive}' (LIFO stack).` : "";
	const message = !parentSlug
		? `Quest archived\nid: ${questId}\narchive: ${finalArchiveZipPath}`
		: `Archived sub-quest ${questName} [${questId}].${returnMsg}`;
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
