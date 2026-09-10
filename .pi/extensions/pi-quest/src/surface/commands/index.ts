// HIGH_LEVEL: #commands — three manual handles.
import type { Pi } from "../../hooks/events";
import { killQuest } from "./quest-del";
import { listQuests } from "./quests";
import { viewActivePlan, editActivePlan } from "./plan";
import { pickQuest } from "./pick";
import { resumeQuest } from "./quest";
import { refreshStatus } from "../../durability/status";

function notify(ctx: { ui: { notify: (m: string, t: "info" | "warning" | "error") => void } }, text: string): void {
  try {
    ctx.ui.notify(text, "info");
  } catch {
    // Notification is best-effort.
  }
}

export function installCommands(pi: Pi): void {
  pi.registerCommand("quest", {
    description: "Resume a quest or drafting phase, show the active quest, or pick one when idle.",
    handler: async (args, ctx) => {
      const out = args.trim() === "" ? await pickQuest(pi, ctx) : await resumeQuest(pi, ctx, args);
      refreshStatus(ctx);
      notify(ctx, out);
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
      refreshStatus(ctx);
      notify(ctx, rows.join("\n"));
    },
  });
  pi.registerCommand("quest-del", {
    description: "Archive (kill) the current or named quest.",
    handler: async (args, ctx) => {
      const out = await killQuest(pi, ctx, args);
      refreshStatus(ctx);
      notify(ctx, out);
    },
  });
}
