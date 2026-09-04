import { ExtensionAPI, StoredState } from "./types.ts";
import { withContext } from "./context.ts";
import { reconstruct } from "./reconstruction.ts";
import { registerStateChangeHandler } from "./state.ts";
import { updateUIStatus } from "./ui.ts";
import { formatQuestShort, Text } from "./utils.ts";
import { CUSTOM_TYPE, LEGACY_CUSTOM_TYPE } from "./constants.ts";
import { installToolCallGate } from "./tool_gating.ts";
import {
  installArchiveTool,
  installAskHumanTool,
  installMarkTool,
  installRebutTool,
  installSubQuestTool,
} from "./tools.ts";
import { installCommands } from "./commands.ts";
import {
  installAfterCompact,
  installBeforeCompact,
  installBeforeSwitch,
  installContextListener,
  installFileWatch,
  installShutdownSave,
  installToolResultListener,
  installTurnEnd,
  installTurnStart,
  installWorkflowSystemPrompt,
  registerQuestJournalCRBHook,
} from "./hooks.ts";

export * from "./types.ts";
export * from "./constants.ts";
export * from "./utils.ts";
export * from "./paths.ts";
export * from "./markdown.ts";
export * from "./state.ts";
export * from "./context.ts";
export * from "./validation.ts";
export * from "./reconstruction.ts";
export * from "./messaging.ts";
export * from "./obligations.ts";
export * from "./research.ts";
export * from "./persistence.ts";
export * from "./compaction.ts";
export * from "./gates.ts";
export * from "./roles.ts";
export * from "./ui.ts";
export * from "./tool_gating.ts";
export * from "./subquest.ts";
export * from "./lifecycle.ts";
export * from "./classification.ts";
export * from "./hooks.ts";
export * from "./logging.ts";
export * from "./diagnostic.ts";
export * from "./critical_agent.ts";
export * from "./utils/mutex.ts";
export * from "./tools.ts";
export * from "./commands.ts";

export default function (pi: ExtensionAPI) {
  // Automatically wrap callbacks and command/tool handlers with asyncContext
  const originalOn = pi.on.bind(pi);
  pi.on = (event: string, handler: any) => {
    originalOn(event, withContext(handler));
  };

  const originalRegisterTool = pi.registerTool.bind(pi);
  pi.registerTool = (tool: any) => {
    if (tool && typeof tool.execute === "function") {
      tool.execute = withContext(tool.execute);
    }
    originalRegisterTool(tool);
  };

  const originalRegisterCommand = pi.registerCommand.bind(pi);
  pi.registerCommand = (name: string, command: any) => {
    if (command && typeof command.handler === "function") {
      command.handler = withContext(command.handler);
    }
    originalRegisterCommand(name, command);
  };

  registerStateChangeHandler(updateUIStatus);
  registerQuestJournalCRBHook();
  pi.on("session_start", async (event: any, ctx: any) => {
    reconstruct(ctx);
    updateUIStatus(ctx);
    // vim-plugin style self-install: ensure quest-journal skill is discoverable via global .pi/skills (like vim pack)
    try {
      const { existsSync, mkdirSync, copyFileSync, readFileSync } =
        await import("node:fs");
      const { join, dirname } = await import("node:path");
      const extSkill = join(
        dirname(new URL(import.meta.url).pathname),
        "../skills/quest-journal/SKILL.md",
      );
      const globalSkill = join(
        ctx?.cwd || (globalThis as any).process?.cwd?.() || ".",
        ".pi/skills/quest-journal/SKILL.md",
      );
      if (existsSync(extSkill)) {
        const src = readFileSync(extSkill, "utf8");
        let needCopy = true;
        try {
          needCopy = readFileSync(globalSkill, "utf8") !== src;
        } catch {
          needCopy = true;
        }
        if (needCopy) {
          mkdirSync(dirname(globalSkill), { recursive: true });
          copyFileSync(extSkill, globalSkill);
        }
      }
    } catch {}
  });
  pi.on("session_tree", async (_event: any, ctx: any) => reconstruct(ctx));

  installWorkflowSystemPrompt(pi);
  installTurnStart(pi);
  installToolCallGate(pi);
  installToolResultListener(pi);
  installContextListener(pi);
  installTurnEnd(pi);
  installBeforeCompact(pi);
  installAfterCompact(pi);
  installBeforeSwitch(pi);
  installFileWatch(pi);
  installMarkTool(pi);
  installArchiveTool(pi);
  installSubQuestTool(pi);
  installRebutTool(pi);
  installAskHumanTool(pi);
  installCommands(pi);
  installShutdownSave(pi);

  // Durable in-session marker, not sent to the LLM.
  const renderEntry = (entry: any, _o: any, theme: any) => {
    const data = entry.data ?? ({} as StoredState);
    const fresh = (data.saveCount ?? 0) > (data.compactCount ?? 0);
    // reuse short token — icon conveys fresh/dirty/research etc.
    const short = formatQuestShort(data, fresh);
    return new Text(short, 0, 0);
  };
  pi.registerEntryRenderer<StoredState>(CUSTOM_TYPE, renderEntry);
  pi.registerEntryRenderer<StoredState>(LEGACY_CUSTOM_TYPE, renderEntry);
}
