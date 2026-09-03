import { mkdir, writeFile } from "node:fs/promises";
import { FUTURE_DIR } from "../constants.ts";
import { FUTURE_QUEST_TEMPLATE } from "../markdown.ts";
import {
  createFutureDraftFromPrompt,
  fileExists,
  futureDraftPath,
  generateSlugFromPrompt,
  resolveQuestRecordBySlug,
} from "../paths.ts";
import { ExtensionContext } from "../types.ts";

export async function handleQuestDraftCommand(
  args: string,
  ctx: ExtensionContext,
): Promise<void> {
  let desc = args.trim();
  if (!desc && ctx.mode === "tui") {
    desc = ((await ctx.ui.input(
      "Describe the future quest / proposal (e.g. cx ergonomics):",
    )) ?? "").trim();
  }
  if (!desc) {
    ctx.ui.notify("Usage: /quest-draft <description>", "warning");
    return;
  }
  const name = generateSlugFromPrompt(desc, 45);
  const existingRecord = await resolveQuestRecordBySlug(name);
  if (existingRecord) {
    ctx.ui.notify(
      `Quest '${name}' is already active/current in ${existingRecord.path}. Cannot create a draft for an active quest.`,
      "warning",
    );
    return;
  }
  await mkdir(FUTURE_DIR, { recursive: true });
  const path = futureDraftPath(name);
  if (!(await fileExists(path))) {
    await writeFile(path, FUTURE_QUEST_TEMPLATE(name, desc), "utf8");
    if (ctx.hasUI) ctx.ui.notify(`Created draft proposal at ${path}`, "info");
  } else {if (ctx.hasUI) {
      ctx.ui.notify(`Draft already exists at ${path}`, "warning");
    }}
}

export async function createAutoDraft(
  slug: string,
  prompt: string,
): Promise<string> {
  return createFutureDraftFromPrompt(slug, prompt);
}
