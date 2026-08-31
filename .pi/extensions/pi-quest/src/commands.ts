import { mkdir, readFile, rename, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { compactionReady, getEconomyThreshold, getSubquestCompactThreshold, getWarningMargin } from "./compaction.ts";
import { DEFAULT_PRE_COMPACT_WARNING_TOKENS, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS, FUTURE_DIR, QUEST_ARCHIVE_DIR, QUEST_CURRENT_DIR } from "./constants.ts";
import { calculateCurrentTokens, usagePercent, withContext } from "./context.ts";
import { syncImplementationPermission } from "./gates.ts";
import { archiveQuestFile, promptForQuestChoice } from "./lifecycle.ts";
import { FUTURE_QUEST_TEMPLATE, QUEST_TEMPLATE, extractParentFromQuest, extractSubQuestsFromQuest } from "./markdown.ts";
import { logError, sendInternalAgentMessage, sendInternalUserMessage, sendSaveRequest } from "./messaging.ts";
import { cleanDraftIfExists, fileExists, listActiveQuestRecords, listQuestFiles, questDirPath, questPath, resolveQuestRecordBySlug, slugify } from "./paths.ts";
import { persist, verifyAndMarkSaved } from "./persistence.ts";
import { loadExistingQuestEpistemicState, loadExistingQuestIntent } from "./reconstruction.ts";
import { startResearchEpoch, triggerReassessment } from "./research.ts";
import { ensureQuestId, generateQuestId, getState, isRootQuest, state } from "./state.ts";
import { applyLoadedSubquestState, buildSubquestProtocolInstructions, linkSubQuestInParent, pushSubquestToStack } from "./subquest.ts";
import { ExtensionAPI, ExtensionContext } from "./types.ts";
import { updateUIStatus } from "./ui.ts";
import { formatQuestHierarchy, formatTokens, parsePercentage, parseTokenAmount } from "./utils.ts";
import { auditQuestConsistency } from "./validation.ts";

export async function resolveQuestTarget(args: string, ctx: ExtensionContext): Promise<{ name: string; goal: string } | null> {
	const trimmed = args.trim();
	if (!trimmed) {
		const choice = await promptForQuestChoice(ctx, "Which quest do you want to work on?");
		if (!choice) {
			ctx.ui.notify("No quest selected.", "warning");
			return null;
		}
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

	if (fullRecord || (await fileExists(fullFuturePath))) {
		return { name: fullSlug, goal: spaceIdx > 0 ? trimmed : "" };
	}
	if (spaceIdx > 0 && (firstRecord || (await fileExists(firstFuturePath)))) {
		return { name: firstSlug, goal: trimmed.slice(spaceIdx + 1).trim() };
	}
	if (spaceIdx > 0 && isFirstTokenSlug) {
		return { name: firstSlug, goal: trimmed.slice(spaceIdx + 1).trim() };
	}
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

	if (!(await fileExists(path))) {
		if (await fileExists(futurePath)) {
			await rename(futurePath, path);
			if (ctx.hasUI) ctx.ui.notify(`Promoted ${futurePath} → ${path}`, "info");
		} else {
			let finalGoal = goal;
			if (!finalGoal && ctx.hasUI && ctx.mode === "tui") {
				finalGoal = ((await ctx.ui.input("Describe the goal for this quest:")) ?? "").trim();
			}
			if (!finalGoal) {
				finalGoal = name.replace(/-/g, " ");
			}
			await writeFile(path, QUEST_TEMPLATE(name, finalGoal, "", originalReq, s.refinements || [], qid), "utf8");
		}
	}
	await cleanDraftIfExists(name);
	return path;
}

export async function applyQuestSwitchEpistemicState(name: string, targetState?: any): Promise<void> {
	const s = targetState || state;
	s.saveGeneration = null;
	s.lastSavedHash = null;
	s.dirty = false;
	s.consecutiveFailures = 0;
	s.substantiveTurnsSinceCheckpoint = 0;
	s.lastReassessmentPromptAt = 0;
	s.lastReassessmentReason = null;
	s.lastCheckpointPromptAt = 0;

	const loaded = await loadExistingQuestEpistemicState(name);
	if (loaded.exists) {
		if (loaded.questId) {
			s.questId = loaded.questId;
		}
		s.researchRound = loaded.researchRound;
		s.researchComplete = loaded.researchComplete;
		s.researchRequired = loaded.researchRequired;
		s.planVersion = loaded.planVersion;
		s.planConfidence = loaded.planConfidence;
		s.lastPlanRevisionsText = loaded.lastPlanRevisionsText;
		s.reassessmentRequired = loaded.reassessmentRequired;
		s.reassessmentReason = loaded.reassessmentReason;
		s.reassessmentEvidence = loaded.reassessmentEvidence;
		s.reassessmentVersion = loaded.reassessmentVersion;
		s.resolvedReassessmentVersion = loaded.resolvedReassessmentVersion;
		s.lastResearchAt = loaded.lastResearchAt ?? Date.now();
		s.lastPlanRevisionAt = loaded.lastPlanRevisionAt ?? Date.now();
		const isTargetConfirmed = Array.isArray(s.confirmedQuests) && s.confirmedQuests.includes(name);
		s.awaitingUserConfirmation = isRootQuest(s) ? (!isTargetConfirmed && (loaded.awaitingUserConfirmation ?? false)) : false;
		if (loaded.researchComplete) {
			s.currentReceipt = null;
			s.lastCompletedReceipt = {
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
			startResearchEpoch(s, s.reassessmentRequired ? "reassessment" : "research");
		}
	} else {
		s.researchRound = 1;
		s.researchComplete = false;
		s.researchRequired = true;
		s.reassessmentRequired = false;
		s.reassessmentReason = null;
		s.reassessmentEvidence = null;
		s.reassessmentVersion = 0;
		s.resolvedReassessmentVersion = 0;
		s.lastPlanRevisionsText = null;
		s.planVersion = 1;
		s.planConfidence = "low";
		s.lastResearchAt = Date.now();
		s.lastPlanRevisionAt = Date.now();
		s.awaitingUserConfirmation = false;
		startResearchEpoch(s, "research");
	}
	syncImplementationPermission(s);
	if (state !== s) {
		Object.assign(state, s);
	}
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
	s.pendingRootQuest = false;
	s.questIdentityEstablished = true;
	s.pendingRootRequest = null;

	const path = await ensureQuestFileOnDisk(name, goal, originalReq, ctx);
	const switching = s.active !== name;
	s.pickerCancelled = false;
	s.active = name;

	const targetRecord = await resolveQuestRecordBySlug(name);
	if (targetRecord && targetRecord.parent) {
		pushSubquestToStack(s, targetRecord.parent, name);
	} else {
		if (!Array.isArray(s.stack)) s.stack = [];
		if (!s.stack.includes(name)) {
			s.stack.push(name);
		} else {
			const idx = s.stack.lastIndexOf(name);
			s.stack = s.stack.slice(0, idx + 1);
		}
	}

	const intent = await loadExistingQuestIntent(name);
	if (intent.originalRequest) {
		s.prompts = [intent.originalRequest];
		s.refinements = intent.refinements;
	} else if (switching && s.prompts.length === 0) {
		s.prompts = [goal || name];
		s.refinements = [];
	}

	if (switching) {
		await applyQuestSwitchEpistemicState(name, s);
	}

	if (await fileExists(path)) {
		await verifyAndMarkSaved(pi, ctx, name);
	}
	if (state !== s) {
		Object.assign(state, s);
	}
	persist(pi, ctx);

	const startMsg = buildQuestStartInstructions(name, path, goal);
	sendInternalAgentMessage(pi, startMsg, "followUp");
}

export async function handleQuestSaveCommand(_args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	if (!state.active) {
		ctx.ui.notify("No active quest -- use /quest <name> first.", "warning");
		return;
	}
	sendSaveRequest(pi, "Quest-journal: /quest-save -- write a full state snapshot to the active quest file now.");
	state.lastPromptAt = Date.now();
}

export async function handleQuestRefineCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	if (!state.active) {
		ctx.ui.notify("No active quest -- use /quest <name> first.", "warning");
		return;
	}
	let refinement = args.trim();
	if (!refinement && ctx.mode === "tui") {
		refinement = (await ctx.ui.input("Enter quest refinement or new requirements:")) ?? "";
	}
	if (!refinement) {
		ctx.ui.notify("Usage: /quest-refine <instructions...>", "warning");
		return;
	}
	if (!Array.isArray(state.refinements)) state.refinements = [];
	state.refinements.push(refinement);
	triggerReassessment(state, `User refinement received: "${refinement.slice(0, 100)}..."`, refinement);
	persist(pi, ctx);

	sendSaveRequest(
		pi,
		`Quest-journal: /quest-refine -- User quest refinement received:\n"${refinement}"\n\nUpdate \`${questPath(state.questId)}\` now: expand ## Goal if needed, add entry under ## Quest Refinements & User Feedback Loops, update ## Remaining work and ## Test / Build Status, and record any new decisions.`
	);
	state.lastPromptAt = Date.now();
	if (ctx.hasUI) ctx.ui.notify(`Refinement queued for active quest '${state.active}'`, "info");
}

export async function handleQuestDelCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	let name = slugify(args);
	if (!name) {
		const choice = await promptForQuestChoice(ctx, "Select quest to archive:");
		name = choice?.name ? slugify(choice.name) : "";
	}
	if (!name) {
		ctx.ui.notify("No quest selected for archiving.", "warning");
		return;
	}
	const res = await archiveQuestFile(name, pi, ctx);
	if (!res.success) {
		ctx.ui.notify(res.message, "warning");
		return;
	}
	if (ctx.hasUI) ctx.ui.notify(res.message, "info");
}

export async function handleQuestDraftCommand(args: string, ctx: ExtensionContext): Promise<void> {
	let desc = args.trim();
	if (!desc && ctx.mode === "tui") {
		desc = ((await ctx.ui.input("Describe the future quest / proposal (e.g. cx ergonomics):")) ?? "").trim();
	}
	if (!desc) {
		ctx.ui.notify("Usage: /quest-draft <description>", "warning");
		return;
	}
	const name = slugify(desc);
	const existingRecord = await resolveQuestRecordBySlug(name);
	if (existingRecord) {
		ctx.ui.notify(`Quest '${name}' is already active/current in ${existingRecord.path}. Cannot create a draft for an active quest.`, "warning");
		return;
	}
	await mkdir(FUTURE_DIR, { recursive: true });
	const path = `${FUTURE_DIR}/${name}.md`;
	if (!(await fileExists(path))) {
		await writeFile(path, FUTURE_QUEST_TEMPLATE(name, desc), "utf8");
		if (ctx.hasUI) ctx.ui.notify(`Created draft proposal at ${path}`, "info");
	} else {
		if (ctx.hasUI) ctx.ui.notify(`Draft already exists at ${path}`, "warning");
	}
}

export async function handleQuestEconomyCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const trimmed = args.trim();
	const currentThreshold = getEconomyThreshold(ctx);
	const currentWarning = getWarningMargin();
	const currentSubquest = getSubquestCompactThreshold();
	const tokens = calculateCurrentTokens(ctx);
	const tokenStr = tokens !== null ? formatTokens(tokens) : "unknown";

	if (!trimmed) {
		const thresholdStr = currentThreshold > 0 ? `${formatTokens(currentThreshold)} tokens (${currentThreshold.toLocaleString()})` : "disabled";
		const warnStr = `${formatTokens(currentWarning)} tokens (${currentWarning.toLocaleString()})`;
		const subStr = `${formatTokens(currentSubquest)} tokens (${currentSubquest.toLocaleString()})`;
		const effectiveWarn = currentThreshold > 0 ? `${formatTokens(Math.max(0, currentThreshold - currentWarning))}` : "N/A";
		const msg = `Quest Economy: threshold = ${thresholdStr}, pre-compact warning = ${warnStr} (warns at ${effectiveWarn}), subquest launch limit = ${subStr}. Current usage = ${tokenStr} tokens. Usage: /quest-economy <threshold|percent> [warning] [subquestLaunch] (e.g. /quest-economy 50%, /quest-economy 333k 30k 40k, /quest-economy off)`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}

	if (trimmed.toLowerCase() === "default") {
		state.economyTokens = null;
		state.economyPercent = null;
		state.warningMarginTokens = null;
		state.subquestCompactTokens = null;
		persist(pi, ctx);
		const newThreshold = getEconomyThreshold(ctx);
		const newWarning = getWarningMargin();
		const newSub = getSubquestCompactThreshold();
		const msg = `Quest Economy: reset to default (threshold = ${formatTokens(newThreshold)}, warning = ${formatTokens(newWarning)}, subquest = ${formatTokens(newSub)}). Current usage: ${tokenStr}.`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}

	if (trimmed.toLowerCase() === "off" || trimmed.toLowerCase() === "disable" || trimmed.toLowerCase() === "disabled" || trimmed === "0") {
		state.economyTokens = 0;
		state.economyPercent = null;
		persist(pi, ctx);
		const msg = `Quest Economy: auto-compaction disabled. Current usage: ${tokenStr}.`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}

	const parts = trimmed.split(/\s+/);
	const pct = parsePercentage(parts[0]);
	if (pct !== null && pct > 0) {
		state.economyPercent = pct;
		state.economyTokens = null;
	} else {
		const parsedThreshold = parseTokenAmount(parts[0]);
		if (parsedThreshold === null || parsedThreshold <= 0) {
			if (ctx.hasUI) ctx.ui.notify(`Invalid threshold: "${parts[0]}". Examples: 50%, 333k, 333k 30k, 500000, off, default`, "warning");
			return;
		}
		state.economyTokens = parsedThreshold;
		state.economyPercent = null;
	}

	if (parts.length > 1) {
		const parsedWarn = parseTokenAmount(parts[1], DEFAULT_PRE_COMPACT_WARNING_TOKENS);
		if (parsedWarn !== null && parsedWarn > 0) {
			state.warningMarginTokens = parsedWarn;
		}
	}

	if (parts.length > 2) {
		const parsedSub = parseTokenAmount(parts[2], DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
		if (parsedSub !== null && parsedSub >= 0) {
			state.subquestCompactTokens = parsedSub;
		}
	}
	persist(pi, ctx);

	const activeThreshold = getEconomyThreshold(ctx);
	const activeWarning = getWarningMargin();
	const activeSub = getSubquestCompactThreshold();
	const msg = `Quest Economy: threshold set to ${formatTokens(activeThreshold)} tokens (${activeThreshold.toLocaleString()}), warning margin = ${formatTokens(activeWarning)}, subquest launch limit = ${formatTokens(activeSub)}. Current usage: ${tokenStr}.`;
	if (ctx.hasUI) ctx.ui.notify(msg, "info");
}

export async function handleQuestWarningCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const trimmed = args.trim();
	const currentWarning = getWarningMargin();
	const currentThreshold = getEconomyThreshold(ctx);

	if (!trimmed) {
		const warnStr = `${formatTokens(currentWarning)} tokens (${currentWarning.toLocaleString()})`;
		const effectiveWarn = currentThreshold > 0 ? ` (warns at ${formatTokens(Math.max(0, currentThreshold - currentWarning))})` : "";
		const msg = `Quest Pre-Compaction Warning Margin: ${warnStr}${effectiveWarn}. Usage: /quest-warning <tokens> (e.g. /quest-warning 30k, /quest-warning default)`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}

	if (trimmed.toLowerCase() === "default") {
		state.warningMarginTokens = null;
		persist(pi, ctx);
		const newWarning = getWarningMargin();
		const msg = `Quest Pre-Compaction Warning: reset to default (${formatTokens(newWarning)} tokens).`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}

	const parsed = parseTokenAmount(trimmed, DEFAULT_PRE_COMPACT_WARNING_TOKENS);
	if (parsed === null || parsed <= 0) {
		if (ctx.hasUI) ctx.ui.notify(`Invalid warning token amount: "${trimmed}". Examples: 30k, 25000, default`, "warning");
		return;
	}

	state.warningMarginTokens = parsed;
	persist(pi, ctx);
	const msg = `Quest Pre-Compaction Warning: margin set to ${formatTokens(parsed)} tokens (${parsed.toLocaleString()}).`;
	if (ctx.hasUI) ctx.ui.notify(msg, "info");
}

export async function handleQuestSubquestThresholdCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const trimmed = args.trim();
	if (!trimmed) {
		const current = getSubquestCompactThreshold();
		const msg = `Sub-quest launch compaction threshold = ${formatTokens(current)} tokens (${current.toLocaleString()}). Usage: /quest-subquest-threshold <tokens|off|default> (e.g. /quest-subquest-threshold 40k)`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}
	if (trimmed.toLowerCase() === "default") {
		state.subquestCompactTokens = null;
		persist(pi, ctx);
		const msg = `Sub-quest launch compaction threshold reset to default (${formatTokens(getSubquestCompactThreshold())}).`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}
	if (trimmed.toLowerCase() === "off" || trimmed.toLowerCase() === "disable" || trimmed.toLowerCase() === "0") {
		state.subquestCompactTokens = 0;
		persist(pi, ctx);
		const msg = `Sub-quest launch compaction disabled.`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}
	const parsed = parseTokenAmount(trimmed, DEFAULT_SUBQUEST_LAUNCH_MIN_TOKENS);
	if (parsed === null) {
		if (ctx.hasUI) ctx.ui.notify(`Invalid token amount: "${trimmed}". Examples: 40k, 50000, off, default`, "warning");
		return;
	}
	state.subquestCompactTokens = parsed;
	persist(pi, ctx);
	const msg = `Sub-quest launch compaction threshold set to ${formatTokens(parsed)} tokens (${parsed.toLocaleString()}).`;
	if (ctx.hasUI) ctx.ui.notify(msg, "info");
}

export async function handleQuestStatusCommand(_args: string, ctx: ExtensionContext): Promise<string> {
	if (state.pendingRootQuest) {
		const reqPreview = (state.pendingRootRequest || "").slice(0, 100);
		const line = `[PROVISIONAL ROOT INITIALIZATION] - Research required to establish quest identity and plan. Original request: "${reqPreview}..."`;
		if (ctx.hasUI) ctx.ui.notify(line, "info");
		return line;
	}
	if (!state.active) {
		if (ctx.hasUI) ctx.ui.notify("No active quest.", "info");
		return "No active quest.";
	}
	const rec = await resolveQuestRecordBySlug(state.active);
	const path = rec ? rec.path : questPath(state.questId);
	const exists = Boolean(path && (await fileExists(path)));
	const fresh = compactionReady();
	const hier = formatQuestHierarchy(state.active, state.stack);
	const threshold = getEconomyThreshold(ctx);
	const tokens = calculateCurrentTokens(ctx);
	const tokenStr = tokens !== null ? `${formatTokens(tokens)}${threshold > 0 ? `/${formatTokens(threshold)}` : ""}` : `~${Math.round(usagePercent(ctx))}%`;
	let parentInfo = "";
	const parentFromStack = state.stack && state.stack.length >= 2 ? state.stack[state.stack.length - 2] : null;
	if (parentFromStack) {
		parentInfo = ` (parent: [[${parentFromStack}]])`;
	}
	let subInfo = "";

	if (exists) {
		try {
			const content = await readFile(path, "utf8");
			const parent = extractParentFromQuest(content);
			if (parent) parentInfo = ` (parent: [[${parent}]])`;
			const subQuests = extractSubQuestsFromQuest(content);
			if (subQuests.length > 0) subInfo = ` | sub-quests: ${subQuests.join(", ")}`;
			const audit = auditQuestConsistency(content, { recentModifiedFiles: state.sessionModifiedFiles });
			if (!audit.consistent && audit.issues.length > 0) {
				subInfo += ` | ⚠️ ${audit.issues.length} consistency issue(s)`;
			}
		} catch (err: any) {
			logError(`Failed to read quest file for status at ${path}`, err, ctx);
		}
	}

	const qId = state.questId;
	const runInfo = qId ? ` | id: ${qId}` : "";
	const line = exists
		? `${path}${parentInfo} [${hier}] - ${fresh ? "fresh" : "SAVE PENDING"}${runInfo}, tokens ${tokenStr}, prompts ${state.prompts.length}${subInfo}`
		: `${path || state.active} - MISSING on disk!`;
	if (ctx.hasUI) ctx.ui.notify(`Active quest: ${line}`, fresh ? "info" : "warning");
	return line;
}

export async function handleQuestsCommand(_args: string, ctx: ExtensionContext): Promise<void> {
	const currentRecords = await listActiveQuestRecords();
	const future = await listQuestFiles(FUTURE_DIR);

	const parentOf = new Map<string, string>();
	const childrenOf = new Map<string, string[]>();

	for (const r of currentRecords) {
		const slug = r.name;
		try {
			const content = await readFile(r.path, "utf8");
			const p = extractParentFromQuest(content);
			if (p && currentRecords.some((cr) => cr.name === p || cr.qid === p)) {
				parentOf.set(slug, p);
				const list = childrenOf.get(p) || [];
				if (!list.includes(slug)) list.push(slug);
				childrenOf.set(p, list);
			}
			const subs = extractSubQuestsFromQuest(content);
			if (subs.length > 0) {
				const list = childrenOf.get(slug) || [];
				for (const s of subs) {
					if (!list.includes(s)) list.push(s);
				}
				childrenOf.set(slug, list);
			}
		} catch (err: any) {
			logError(`Failed to read quest file for parent linking at ${r.path}`, err, ctx);
		}
	}

	const renderedCurrent: string[] = [];
	for (const r of currentRecords) {
		const slug = r.name;
		if (parentOf.has(slug)) continue;

		const isActive = state.active === slug;
		renderedCurrent.push(`  ${slug}${isActive ? "  ◀ active" : ""}`);
		const subs = childrenOf.get(slug) || [];
		for (const sub of subs) {
			const isSubActive = state.active === sub;
			renderedCurrent.push(`    ↳ ${sub}${isSubActive ? "  ◀ active" : ""}`);
		}
	}

	const futureRows = future.length
		? future.map((f) => `  ${f.replace(/\.md$/, "")}`)
		: ["  (none - use /quest-draft <name>)"];

	ctx.ui.setWidget("quest", [
		`Active: ${state.active ? (questPath(state.questId) || state.active) : "(none)"}`,
		"",
		"Current quests:",
		...(renderedCurrent.length ? renderedCurrent : ["  (none - use /quest <name>)"]),
		"",
		"Future / Backlog quests:",
		...futureRows,
	]);
}

export async function handleSubquestCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	let raw = args.trim();
	if (!raw && ctx.mode === "tui") {
		raw = ((await ctx.ui.input("Describe the sub-quest (e.g. handle auth edge cases):")) ?? "").trim();
	}
	if (!raw) {
		ctx.ui.notify("Usage: /subquest [--plan|-p] <description...>", "warning");
		return;
	}

	let switchNow = true;
	if (raw.startsWith("--plan ") || raw.startsWith("-p ") || raw.startsWith("--no-switch ")) {
		switchNow = false;
		raw = raw.replace(/^(--plan|-p|--no-switch)\s+/, "").trim();
	}

	const goal = raw;
	const name = slugify(raw);

	const qid = state.questId || generateQuestId();
	state.questId = qid;
	await mkdir(questDirPath(qid), { recursive: true });
	const path = join(questDirPath(qid), `${name}.md`);
	const parentName = state.active || "";
	const isExisting = await fileExists(path);

	if (!isExisting) {
		await writeFile(path, QUEST_TEMPLATE(name, goal, parentName, "", [], qid), "utf8");
	}
	if (parentName) {
		await linkSubQuestInParent(parentName, name, goal, ctx);
		await verifyAndMarkSaved(pi, ctx, parentName);
	}

	if (!switchNow) {
		const msg = `Planned sub-quest **${name}** at \`${path}\`${parentName ? ` linked in parent **${parentName}**` : ""}. Kept active quest **${state.active}**.`;
		if (ctx.hasUI) ctx.ui.notify(msg, "info");
		return;
	}

	pushSubquestToStack(state, parentName, name);
	const subLoaded = await loadExistingQuestEpistemicState(qid);
	applyLoadedSubquestState(state, goal, isExisting, subLoaded);

	await verifyAndMarkSaved(pi, ctx, name);
	persist(pi, ctx);
	updateUIStatus(ctx);

	const subquestMsg = buildSubquestProtocolInstructions(name, goal, parentName, path);
	sendInternalUserMessage(pi, subquestMsg);
}

export async function getQuestCompletions(prefix: string): Promise<Array<{ value: string; label: string }> | null> {
	const currentRecords = await listActiveQuestRecords();
	const future = await listQuestFiles(FUTURE_DIR);
	const names = [...new Set([...currentRecords.map((r) => r.name), ...future.map((f) => f.replace(/\.md$/, ""))])];
	const filtered = names.filter((n) => n.startsWith(prefix));
	return filtered.length ? filtered.map((value) => ({ value, label: value })) : null;
}

export function installCommands(pi: ExtensionAPI): void {
	const economyCompletions = async (prefix: string) => {
		const options = ["50%", "40%", "80%", "75%", "70%", "333k", "400k", "500k", "off", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	const warningCompletions = async (prefix: string) => {
		const options = ["15k", "20k", "25k", "30k", "35k", "40k", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	const subquestThresholdCompletions = async (prefix: string) => {
		const options = ["20k", "30k", "40k", "50k", "60k", "off", "default"];
		const filtered = options.filter((o) => o.toLowerCase().startsWith(prefix.toLowerCase()));
		return filtered.map((value) => ({ value, label: value }));
	};

	pi.registerCommand("quest", {
		description: `Set the active quest (e.g. /quest cx). Creates .pi/quest/current/<qid>/quest.md.`,
		getArgumentCompletions: getQuestCompletions,
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-save", {
		description: "Persist the active quest file now.",
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestSaveCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-refine", {
		description: "Refine the active quest mid-workflow or add post-implementation requirements (e.g. /quest-refine Add edge case handling).",
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestRefineCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-del", {
		description: `Archive the current or named quest into ${QUEST_ARCHIVE_DIR}/<qid>.zip.`,
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestDelCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-draft", {
		description: "Draft a future quest or proposal without making it active.",
		handler: handleQuestDraftCommand,
	});

	pi.registerCommand("quest-economy", {
		description: "Configure or check token economy auto-compaction threshold (e.g. /quest-economy 50%, /quest-economy 333k 30k, /quest-economy off).",
		getArgumentCompletions: economyCompletions,
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestEconomyCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-warning", {
		description: "Configure pre-compaction warning margin (e.g. /quest-warning 30k).",
		getArgumentCompletions: warningCompletions,
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestWarningCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-subquest-threshold", {
		description: "Configure the minimum token threshold for auto-compacting when launching a sub-quest (e.g. /quest-subquest-threshold 40k).",
		getArgumentCompletions: subquestThresholdCompletions,
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestSubquestThresholdCommand(args, ctx, pi)),
	});

	pi.registerCommand("quest-status", {
		description: "Show the active quest and whether its file is fresh.",
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestStatusCommand(args, ctx)),
	});

	pi.registerCommand("quests", {
		description: "List current and future quests.",
		handler: withContext((args: string, ctx: ExtensionContext) => handleQuestsCommand(args, ctx)),
	});

	pi.registerCommand("subquest", {
		description: "Create and switch to a sub-quest linked to the current active quest (e.g. /subquest error-handling Handle network disconnects).",
		getArgumentCompletions: getQuestCompletions,
		handler: withContext((args: string, ctx: ExtensionContext) => handleSubquestCommand(args, ctx, pi)),
	});

	pi.registerCommand("sub-quest", {
		description: "Alias for /subquest.",
		getArgumentCompletions: getQuestCompletions,
		handler: withContext((args: string, ctx: ExtensionContext) => handleSubquestCommand(args, ctx, pi)),
	});
}
