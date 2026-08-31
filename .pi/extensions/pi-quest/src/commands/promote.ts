import { existsSync } from "node:fs";
import { mkdir, readFile, writeFile, unlink } from "node:fs/promises";
import { FUTURE_DIR } from "../constants.ts";
import { isDraftReviewValid } from "../critical_agent/policy.ts";
import { isSubagentToolRegistered, getCustomSubagentRunner } from "../critical_agent/index.ts";
import { futureDraftPath, questPath, questDirPath } from "../paths.ts";
import { ensureQuestIdInContent } from "../markdown/template/header.ts";
import { persist, verifyAndMarkSaved } from "../persistence.ts";
import { startResearchEpoch } from "../research.ts";
import { generateQuestId, getState, state } from "../state.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";
import { syncImplementationPermission } from "../gates.ts";
import { updateUIStatus } from "../ui.ts";

export async function promoteDraft(
	slug: string,
	ctx?: ExtensionContext,
	pi?: ExtensionAPI,
): Promise<{ success: boolean; message?: string; qid?: string }> {
	const s = getState(ctx);
	const targetSlug = slug || s.activeDraft || "";
	if (!targetSlug) return { success: false, message: "No active draft to promote." };
	const futurePath = futureDraftPath(targetSlug);
	try { await readFile(futurePath, "utf8"); } catch { return { success: false, message: `Draft not found: ${futurePath}` }; }

	// Review gate: require APPROVE unless no reviewer available
	const hasReviewer = Boolean(getCustomSubagentRunner()) || isSubagentToolRegistered(pi as any, ctx as any);
	if (hasReviewer && !isDraftReviewValid(s)) {
		return {
			success: false,
			message: `Draft '${targetSlug}' not yet reviewer-approved. Run plan_review and obtain APPROVE before "go" (boundaryKey must match future draft hash).`,
		};
	}

	const qid = generateQuestId();
	s.questId = qid;
	(state as any).questId = qid;
	await mkdir(questDirPath(qid), { recursive: true });

	let content: string;
	try { content = await readFile(futurePath, "utf8"); } catch (e: any) { return { success: false, message: `Failed to read draft: ${e?.message}` }; }

	// Enrich with questId frontmatter
	content = ensureQuestIdInContent(content, qid);

	// Ensure ## Original request and refinements reflect draftPrompts
	const draftPrompts = Array.isArray(s.draftPrompts) && s.draftPrompts.length > 0 ? [...s.draftPrompts] : [content.match(/## Goals & Scope[\s\S]*?\n\n([\s\S]*?)\n##/)?.[1]?.trim() || targetSlug];
	// Inject original request if placeholder
	if (content.includes("> What are we proposing") || content.includes("> Paste the verbatim")) {
		// no-op, keep as is; reconstruction will use draftPrompts
	}

	const destPath = questPath(qid);
	await writeFile(destPath, content, "utf8");
	try { await unlink(futurePath); } catch {}

	// Carry prompts
	s.prompts = draftPrompts.length > 0 ? [...draftPrompts] : [targetSlug];
	s.refinements = draftPrompts.length > 1 ? draftPrompts.slice(1) : [];
	if (s.prompts.length > 10) s.prompts = [s.prompts[0], ...s.prompts.slice(-9)];
	if (s.refinements.length > 10) s.refinements = s.refinements.slice(-10);

	s.activeDraft = null;
	s.draftPrompts = [];
	s.draftCreatedAt = null;
	s.active = targetSlug;
	if (!Array.isArray(s.stack)) s.stack = [];
	if (!s.stack.includes(targetSlug)) s.stack.push(targetSlug);
	s.pendingRootQuest = false;
	s.pendingRootRequest = null;
	s.questIdentityEstablished = true;
	s.researchRequired = true;
	s.researchComplete = false;
	s.reassessmentRequired = false;
	startResearchEpoch(s as any, "research");
	syncImplementationPermission(s as any);
	persist(pi as any, ctx as any);
	updateUIStatus(ctx as any);

	try { await verifyAndMarkSaved(pi as any, ctx as any, targetSlug); } catch {}

	return { success: true, qid, message: `Promoted draft '${targetSlug}' → ${destPath} (qid ${qid})` };
}

export async function handleQuestPromoteCommand(args: string, ctx: ExtensionContext, pi: ExtensionAPI): Promise<void> {
	const raw = args.trim();
	const s = getState(ctx);
	const slug = raw ? raw : s.activeDraft || "";
	if (!slug) {
		ctx.ui.notify("No active draft. Use /quest-draft or wait for auto-draft, then say 'go' after reviewer APPROVE.", "warning");
		return;
	}
	const res = await promoteDraft(slug, ctx, pi);
	ctx.ui.notify(res.message || (res.success ? "Promoted." : "Failed"), res.success ? "info" : "warning");
	if (res.success && res.qid) {
		const { sendInternalAgentMessage } = await import("../messaging.ts");
		sendInternalAgentMessage(pi, `✅ Draft '${slug}' promoted to current quest '${slug}' (qid ${res.qid}). Continue with research & plan as per workflow.`, "followUp");
	}
}
