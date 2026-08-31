import { archiveQuestFile } from "../lifecycle.ts";
import { logEvent } from "../logging.ts";
import { reportAgentError } from "../messaging.ts";
import { questPath, slugify } from "../paths.ts";
import { persist } from "../persistence.ts";
import { state } from "../state.ts";
import { sendChildReturnParentPrompt } from "../subquest.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode } from "../types.ts";
import { formatArchiveResponse } from "./formatting.ts";

export async function executeArchiveTool(params: any, pi: ExtensionAPI, ctx: ExtensionContext) {
	const targetName = slugify(params?.questName || params?.name || state.active || "");
	if (!targetName) {
		return {
			content: [{ type: "text", text: "Error: No active quest to archive and no questName provided." }],
			details: { error: "no_quest" },
		};
	}

	const res = await archiveQuestFile(targetName, pi, ctx);
	if (!res.success) {
		reportAgentError(
			pi,
			ctx,
			`Failed to archive quest '${targetName}': ${res.message}`,
			{
				code: QuestErrorCode.ARCHIVE_FAILURE,
				requiredNextAction: `Ensure ${questPath(state.questId)} exists on disk and is accessible, then retry quest_archive.`,
				details: { Quest: targetName },
			},
		);
		return {
			content: [{ type: "text", text: res.message }],
			details: { error: "archive_failed" },
		};
	}

	const shouldCompact = params?.compact !== false;
	if (shouldCompact && typeof ctx.compact === "function") {
		state.archiveCompactionPending = targetName;
		persist(pi, ctx);
	} else if (res.nextActive) {
		sendChildReturnParentPrompt(pi, res.nextActive, targetName, res.childSummary || `- Completed sub-quest ${targetName}.`, ctx);
	}

	if (ctx.hasUI) ctx.ui.notify(res.message, "info");
	return formatArchiveResponse(targetName, res, shouldCompact);
}
