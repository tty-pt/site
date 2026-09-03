import { archiveQuestFile } from "../lifecycle.ts";
import { logEvent } from "../logging.ts";
import { reportAgentError } from "../messaging.ts";
import { questPath, slugify } from "../paths.ts";
import { persist } from "../persistence.ts";
import { state } from "../state.ts";
import { sendChildReturnParentPrompt } from "../subquest.ts";
import { ExtensionAPI, ExtensionContext, QuestErrorCode } from "../types.ts";
import { formatArchiveResponse } from "./formatting.ts";

export async function executeArchiveTool(
  params: any,
  pi: ExtensionAPI,
  ctx: ExtensionContext,
) {
  const targetName = slugify(
    params?.questName || params?.name || state.active || "",
  );
  if (!targetName) {
    return {
      content: [{
        type: "text",
        text: "Error: No active quest to archive and no questName provided.",
      }],
      details: { error: "no_quest" },
    };
  }
  // B1'': abandon path — record unresolved contradiction and allow archive despite reassessmentRequired
  if (params?.abandon === true && state.reassessmentRequired) {
    const reason = state.reassessmentReason || "unresolved contradiction";
    logEvent(
      "QUEST_ABANDONED",
      `quest abandoned with unresolved contradiction: ${reason}`,
      { quest: targetName, reason },
    );
    // Persist abandon note to quest.md if possible
    try {
      const { readFile, writeFile } = await import("node:fs/promises");
      const { parseMarkdownSections, spliceMarkdownSections } = await import(
        "../markdown.ts"
      );
      const { questPath } = await import("../paths.ts");
      const { fileExists } = await import("../paths.ts");
      const qPath = state.questId
        ? questPath(state.questId)
        : questPath(targetName);
      if (await fileExists(qPath)) {
        const content = await readFile(qPath, "utf8");
        const sections = parseMarkdownSections(content);
        const existing = sections.get("abandoned")?.body || "";
        const newBody = existing
          ? `${existing.trim()}\n\nAbandoned at ${
            new Date().toISOString()
          }: ${reason}\n`
          : `Abandoned at ${new Date().toISOString()}: ${reason}\nDialogue: ${
            JSON.stringify(state.reviewDialogue || []).slice(0, 1000)
          }\n`;
        const updates = new Map<string, string>();
        updates.set("abandoned", newBody);
        const updated = spliceMarkdownSections(content, updates);
        await writeFile(qPath, updated, "utf8");
      }
    } catch {}
    // Clear gate to allow archive
    state.reassessmentRequired = false;
    state.reassessmentReason = null;
    state.reassessmentEvidence = null;
    state.dirty = false;
    try {
      persist(pi, ctx);
    } catch {}
  }

  const res = await archiveQuestFile(
    targetName,
    pi,
    ctx,
    params?.abandon === true ? { abandon: true } : undefined,
  );
  if (!res.success) {
    reportAgentError(
      pi,
      ctx,
      `Failed to archive quest '${targetName}': ${res.message}`,
      {
        code: QuestErrorCode.ARCHIVE_FAILURE,
        requiredNextAction: `Ensure ${
          questPath(state.questId)
        } exists on disk and is accessible, then retry quest_archive.`,
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
    sendChildReturnParentPrompt(
      pi,
      res.nextActive,
      targetName,
      res.childSummary || `- Completed sub-quest ${targetName}.`,
      ctx,
    );
  }

  if (ctx.hasUI) ctx.ui.notify(res.message, "info");
  return formatArchiveResponse(targetName, res, shouldCompact);
}
