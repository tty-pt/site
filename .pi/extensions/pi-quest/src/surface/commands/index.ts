// HIGH_LEVEL: #commands — three manual handles.
import type { Pi } from "../../hooks/events";
import { killQuest } from "./quest-del";
import { listQuests } from "./quests";
import { resumeQuest } from "./quest";

function notify(ctx: { ui: { notify: (m: string, t: "info" | "warning" | "error") => void } }, text: string): void {
  try {
    ctx.ui.notify(text, "info");
  } catch {
    // Notification is best-effort.
  }
}

export function installCommands(pi: Pi): void {
  pi.registerCommand("quest", {
    description: "Resume a quest or drafting phase, or show the active quest.",
    handler: async (args, ctx) => {
      notify(ctx, await resumeQuest(pi, ctx, args));
    },
  });
  pi.registerCommand("quests", {
    description: "List all quests with their states and the active marker.",
    handler: async (_args, ctx) => {
      const rows = await listQuests(ctx);
      try {
        ctx.ui.setWidget("quest", rows);
      } catch {
        // Widget is best-effort.
      }
      notify(ctx, rows.join("\n"));
    },
  });
  pi.registerCommand("quest-del", {
    description: "Archive (kill) the current or named quest.",
    handler: async (args, ctx) => {
      notify(ctx, await killQuest(pi, ctx, args));
    },
  });
}
