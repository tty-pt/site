import { createHash } from "node:crypto";
import { copyFileSync, existsSync } from "node:fs";
import { copyFile, mkdir, rename, writeFile } from "node:fs/promises";
import { readFile } from "node:fs/promises";
import { basename, join } from "node:path";
import { FUTURE_DIR } from "../constants.ts";
import { archiveQuestFile, promptForQuestChoice } from "../lifecycle.ts";
import { FUTURE_QUEST_TEMPLATE, QUEST_TEMPLATE } from "../markdown.ts";
import { logEvent } from "../logging.ts";
import { sendInternalAgentMessage, sendSaveRequest, sendInternalUserMessage } from "../messaging.ts";
import { cleanDraftIfExists, questDirPath, questPath, resolveQuestRecordBySlug, slugify } from "../paths.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { loadExistingQuestEpistemicState, loadExistingQuestIntent } from "../reconstruction.ts";
import { startResearchEpoch, triggerReassessment } from "../research.ts";
import { generateQuestId, getState, isRootQuest, state } from "../state.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";
import { updateUIStatus } from "../ui.ts";
import { syncImplementationPermission } from "../gates.ts";
import { pushSubquestToStack } from "../subquest.ts";

export async function resolveQuestTarget(args: string, ctx: ExtensionContext): Promise<{ name: string; goal: string } | null> {
	const trimmed = args.trim();
	if (!trimmed) {
		const choice = await promptForQuestChoice(ctx, "Which quest do you want to work on?");
		if (!choice) { ctx.ui.notify("No quest selected.", "warning"); return null; }
		return { name: choice.name, goal: choice.goal || "" };
	}
	const spaceIdx = trimmed.indexOf(" ");
	const firstToken = spaceIdx > 0 ? trimmed.slice(0, spaceIdx) : trimmed;
	const isFirstTokenSlug = firstToken.includes("-") || firstToken.includes("_");
	const fullSlug = slugify(trimmed, 45);
	const firstSlug = slugify(firstToken, 45);
	const fullRecord = await resolveQuestRecordBySlug(fullSlug);
	const firstRecord = spaceIdx > 0 ? await resolveQuestRecordBySlug(firstSlug) : null;
	const fullFuturePath = `${FUTURE_DIR}/${fullSlug}.md`;
	const firstFuturePath = `${FUTURE_DIR}/${firstSlug}.md`;
	const existsFullFuture = await (async () => { try { await readFile(fullFuturePath, "utf8"); return true; } catch { return false; } })();
	const existsFirstFuture = spaceIdx > 0 ? await (async () => { try { await readFile(firstFuturePath, "utf8"); return true; } catch { return false; } })() : false;
	if (fullRecord || existsFullFuture) return { name: fullSlug, goal: spaceIdx > 0 ? trimmed : "" };
	if (spaceIdx > 0 && (firstRecord || existsFirstFuture)) return { name: firstSlug, goal: trimmed.slice(spaceIdx + 1).trim() };
	if (spaceIdx > 0 && isFirstTokenSlug) return { name: firstSlug, goal: trimmed.slice(spaceIdx + 1).trim() };
	return { name: fullSlug, goal: trimmed };
}

export async function ensureQuestFileOnDisk(name: string, goal: string, originalReq: string, ctx: ExtensionContext): Promise<string> {
	const s = getState(ctx);
	const record = await resolveQuestRecordBySlug(name);
	let qid = record ? record.qid : generateQuestId();
	s.questId = qid;
	state.questId = qid;
	await mkdir(questDirPath(qid), { recursive: true });
	const path = questPath(qid);
	const futurePath = `${FUTURE_DIR}/${name}.md`;
	try {
		await readFile(path, "utf8");
	} catch {
		try {
			await readFile(futurePath, "utf8");
			// Archive-before-unlink: preserve future/<slug>.md before rename (see issue 04 path E)
			try {
				const futureContent = await readFile(futurePath, "utf8").catch(() => "");
				if (qid) {
					const archDir = join(questDirPath(qid), "future-archive");
					try { await mkdir(archDir, { recursive: true }); } catch {}
					const destArch = join(archDir, basename(futurePath));
					try { await copyFile(futurePath, destArch); } catch { try { copyFileSync(futurePath, destArch); } catch {} }
					try { const h = createHash("sha256").update(futureContent || name).digest("hex").slice(0, 12); logEvent("DRAFT_DISCARDED" as any, `draft discarded`, { quest: name, slug: name, hash: h, dest: destArch, reason: "ensureQuestFileOnDisk" } as any); } catch {}
					// Verify archive succeeded before rename; fallback to copy check
					if (!existsSync(destArch)) {
						try { copyFileSync(futurePath, destArch); } catch {}
					}
				}
			} catch {}
			await rename(futurePath, path);
			if (ctx.hasUI) ctx.ui.notify(`Promoted ${futurePath} → ${path}`, "info");
		} catch {
			let finalGoal = goal;
			if (!finalGoal && ctx.hasUI && ctx.mode === "tui") finalGoal = ((await ctx.ui.input("Describe the goal for this quest:")) ?? "").trim();
			if (!finalGoal) finalGoal = name.replace(/-/g, " ");
			await writeFile(path, QUEST_TEMPLATE(name, finalGoal, "", originalReq, s.refinements || [], qid), "utf8");
		}
	}
	// cleanDraftIfExists is now redundant after archived rename (idempotent), keep for safety
	await cleanDraftIfExists(name);
	return path;
}

export async function applyQuestSwitchEpistemicState(name: string, targetState?: any): Promise<void> {
	const s = targetState || state;
	s.saveGeneration = null; s.lastSavedHash = null; s.dirty = false; s.consecutiveFailures = 0; s.substantiveTurnsSinceCheckpoint = 0; s.lastReassessmentPromptAt = 0; s.lastReassessmentReason = null; s.lastCheckpointPromptAt = 0;
	const loaded = await loadExistingQuestEpistemicState(name);
	if (loaded.exists) {
		if (loaded.questId) s.questId = loaded.questId;
		s.researchRound = loaded.researchRound; s.researchComplete = loaded.researchComplete; s.researchRequired = loaded.researchRequired; s.planVersion = loaded.planVersion; s.planConfidence = loaded.planConfidence; s.lastPlanRevisionsText = loaded.lastPlanRevisionsText; s.reassessmentRequired = loaded.reassessmentRequired; s.reassessmentReason = loaded.reassessmentReason; s.reassessmentEvidence = loaded.reassessmentEvidence; s.reassessmentVersion = loaded.reassessmentVersion; s.resolvedReassessmentVersion = loaded.resolvedReassessmentVersion; s.lastResearchAt = loaded.lastResearchAt ?? Date.now(); s.lastPlanRevisionAt = loaded.lastPlanRevisionAt ?? Date.now();
		const isTargetConfirmed = Array.isArray(s.confirmedQuests) && s.confirmedQuests.includes(name);
		s.awaitingUserConfirmation = isRootQuest(s) ? (!isTargetConfirmed && (loaded.awaitingUserConfirmation ?? false)) : false;
		if (loaded.researchComplete) {
			s.currentReceipt = null;
			s.lastCompletedReceipt = { epoch: 0, epochType: "historical", startedAt: loaded.lastResearchAt || Date.now(), completedAt: loaded.lastResearchAt || Date.now(), toolCalls: 0, readTargets: [], searchTargets: [], commands: [], evidenceCount: 0, isHistorical: true };
		} else {
			startResearchEpoch(s, s.reassessmentRequired ? "reassessment" : "research");
		}
	} else {
		s.researchRound = 1; s.researchComplete = false; s.researchRequired = true; s.reassessmentRequired = false; s.reassessmentReason = null; s.reassessmentEvidence = null; s.reassessmentVersion = 0; s.resolvedReassessmentVersion = 0; s.lastPlanRevisionsText = null; s.planVersion = 1; s.planConfidence = "low"; s.lastResearchAt = Date.now(); s.lastPlanRevisionAt = Date.now(); s.awaitingUserConfirmation = false;
		startResearchEpoch(s, "research");
	}
	syncImplementationPermission(s);
	if (state !== s) Object.assign(state, s);
}

export function buildQuestStartInstructions(name: string, path: string, goal: string): string {
	const goalText = goal ? `\n\n**Stated Goal**: ${goal}` : "";
	return `Now working on quest **${name}**. Quest file: \`${path}\`.${goalText}

**Iterative Research, Planning & Falsification Protocol (Turn 1)**:
1. Discover how to build, run, and test the project (e.g. read AGENTS.md, Makefile, scripts).
2. Perform targeted codebase investigation: inspect relevant libraries, module boundaries, data flows, and execution paths.
3. Identify key assumptions and material uncertainties. Specifically investigate the highest-risk assumptions and consider plausible alternative architectures.
4. Formulate a provisional execution plan in \`${path}\`. For any discrete phase taking more than one step, use \`quest_subquest({ goal: '...', switchNow: false })\` to queue it without losing your current focus.
5. Actively challenge the plan: what evidence or test could prove it wrong? If unresolved uncertainties remain, perform a targeted research pass.
6. In Turn 1 of this root/main quest, present your research findings, assumptions evaluated, architectural trade-offs, and revised plan clearly to the user, and ASK FOR USER CONFIRMATION before writing or modifying feature code. Once confirmed, proceed autonomously.

**TDD & Quality Workflow**:
1. For each implementation stage, establish an appropriate verification strategy before implementation. Prefer tests-first when practical, but do not create artificial tests merely to satisfy this workflow.
2. Develop feature -> build -> run -> verify targeted tests.
3. Dynamic Reassessment: Re-evaluate and revise the plan if tests fail or unexpected code paths are uncovered.
4. Support end-of-task user feedback loops and polish iterations until final confirmation.
5. Final Quality Gates: zero build errors/warnings, zero debug artifacts, and full test suite passing with zero errors.`;
}

export async function handleQuestCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const s = getState(ctx);
	const target = await resolveQuestTarget(args, ctx);
	if (!target || !target.name) return;
	const { name, goal } = target;
	const originalReq = s.pendingRootRequest || (s.prompts && s.prompts.length > 0 ? s.prompts[0] : "");
	s.pendingRootQuest = false; s.questIdentityEstablished = true; s.pendingRootRequest = null;
	const path = await ensureQuestFileOnDisk(name, goal, originalReq, ctx);
	const switching = s.active !== name;
	s.pickerCancelled = false; s.active = name;
	const targetRecord = await resolveQuestRecordBySlug(name);
	if (targetRecord && (targetRecord as any).parent) pushSubquestToStack(s, (targetRecord as any).parent, name);
	else {
		if (!Array.isArray(s.stack)) s.stack = [];
		if (!s.stack.includes(name)) s.stack.push(name);
		else { const idx = s.stack.lastIndexOf(name); s.stack = s.stack.slice(0, idx + 1); }
	}
	const intent = await loadExistingQuestIntent(name);
	if (intent.originalRequest) { s.prompts = [intent.originalRequest]; s.refinements = intent.refinements; }
	else if (switching && s.prompts.length === 0) { s.prompts = [goal || name]; s.refinements = []; }
	if (switching) await applyQuestSwitchEpistemicState(name, s);
	try {
		await readFile(path, "utf8");
		await verifyAndMarkSaved(pi, ctx, name);
	} catch {}
	if (state !== s) Object.assign(state, s);
	persist(pi, ctx);
	const startMsg = buildQuestStartInstructions(name, path, goal);
	sendInternalAgentMessage(pi, startMsg, "followUp");
}

export async function handleQuestSaveCommand(_args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	if (!state.active) { ctx.ui.notify("No active quest -- use /quest <name> first.", "warning"); return; }
	sendSaveRequest(pi, "Quest-journal: /quest-save -- write a full state snapshot to the active quest file now.");
	state.lastPromptAt = Date.now();
}

export async function handleQuestRefineCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	if (!state.active) { ctx.ui.notify("No active quest -- use /quest <name> first.", "warning"); return; }
	let refinement = args.trim();
	if (!refinement && ctx.mode === "tui") refinement = (await ctx.ui.input("Enter quest refinement or new requirements:")) ?? "";
	if (!refinement) { ctx.ui.notify("Usage: /quest-refine <instructions...>", "warning"); return; }
	if (!Array.isArray(state.refinements)) state.refinements = [];
	state.refinements.push(refinement);
	triggerReassessment(state, `User refinement received: "${refinement.slice(0, 100)}..."`, refinement);
	persist(pi, ctx);
	sendSaveRequest(pi, `Quest-journal: /quest-refine -- User quest refinement received:\n"${refinement}"\n\nUpdate \`${questPath(state.questId)}\` now: expand ## Goal if needed, add entry under ## Quest Refinements & User Feedback Loops, update ## Remaining work and ## Test / Build Status, and record any new decisions.`);
	state.lastPromptAt = Date.now();
	if (ctx.hasUI) ctx.ui.notify(`Refinement queued for active quest '${state.active}'`, "info");
}

export async function handleQuestDelCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	let name = slugify(args);
	if (!name) { const choice = await promptForQuestChoice(ctx, "Select quest to archive:"); name = choice?.name ? slugify(choice.name) : ""; }
	if (!name) { ctx.ui.notify("No quest selected for archiving.", "warning"); return; }
	const res = await archiveQuestFile(name, pi, ctx);
	if (!res.success) { ctx.ui.notify(res.message, "warning"); return; }
	if (ctx.hasUI) ctx.ui.notify(res.message, "info");
}

export async function getQuestCompletions(prefix: string): Promise<Array<{ value: string; label: string }> | null> {
	const { listActiveQuestRecords, listQuestFiles } = await import("../paths.ts");
	const { FUTURE_DIR } = await import("../constants.ts");
	const currentRecords = await listActiveQuestRecords();
	const future = await listQuestFiles(FUTURE_DIR);
	const names = [...new Set([...currentRecords.map((r) => r.name), ...future.map((f) => f.replace(/\.md$/, ""))])];
	const filtered = names.filter((n) => n.startsWith(prefix));
	return filtered.length ? filtered.map((value) => ({ value, label: value })) : null;
}
