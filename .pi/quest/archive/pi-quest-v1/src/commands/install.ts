import { withContext } from "../context.ts";
import { ExtensionAPI } from "../types.ts";
import {
  getQuestCompletions,
  handleQuestCommand,
  handleQuestDelCommand,
  handleQuestRefineCommand,
  handleQuestSaveCommand,
} from "./quest.ts";
import {
  handleQuestEconomyCommand,
  handleQuestSubquestThresholdCommand,
  handleQuestWarningCommand,
} from "./economy.ts";
import { handleQuestsCommand, handleQuestStatusCommand } from "./status.ts";
import { handleSubquestCommand } from "./subquest.ts";
import { handleQuestDraftCommand } from "./draft.ts";
import { handleQuestPromoteCommand } from "./promote.ts";

export function installCommands(pi: ExtensionAPI): void {
  const economyCompletions = async (prefix: string) => {
    const options = [
      "50%",
      "40%",
      "80%",
      "75%",
      "70%",
      "333k",
      "400k",
      "500k",
      "off",
      "default",
    ];
    const filtered = options.filter((o) =>
      o.toLowerCase().startsWith(prefix.toLowerCase())
    );
    return filtered.map((value) => ({ value, label: value }));
  };
  const warningCompletions = async (prefix: string) => {
    const options = ["15k", "20k", "25k", "30k", "35k", "40k", "default"];
    const filtered = options.filter((o) =>
      o.toLowerCase().startsWith(prefix.toLowerCase())
    );
    return filtered.map((value) => ({ value, label: value }));
  };
  const subquestThresholdCompletions = async (prefix: string) => {
    const options = ["20k", "30k", "40k", "50k", "60k", "off", "default"];
    const filtered = options.filter((o) =>
      o.toLowerCase().startsWith(prefix.toLowerCase())
    );
    return filtered.map((value) => ({ value, label: value }));
  };
  pi.registerCommand("quest", {
    description:
      `Set the active quest (e.g. /quest cx). Creates .pi/quest/current/<qid>/quest.md.`,
    getArgumentCompletions: getQuestCompletions,
    handler: withContext((args: string, ctx) =>
      handleQuestCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-save", {
    description: "Persist the active quest file now.",
    handler: withContext((args: string, ctx) =>
      handleQuestSaveCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-refine", {
    description:
      "Refine the active quest mid-workflow or add post-implementation requirements (e.g. /quest-refine Add edge case handling).",
    handler: withContext((args: string, ctx) =>
      handleQuestRefineCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-del", {
    description: `Archive the current or named quest.`,
    handler: withContext((args: string, ctx) =>
      handleQuestDelCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-draft", {
    description: "Draft a future quest or proposal without making it active.",
    handler: handleQuestDraftCommand,
  });
  pi.registerCommand("quest-promote", {
    description:
      "Promote the active draft (future) to current after reviewer APPROVE. Usage: /quest-promote [slug]; or just say 'go'.",
    handler: withContext((args: string, ctx) =>
      handleQuestPromoteCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-economy", {
    description:
      "Configure or check token economy auto-compaction threshold (e.g. /quest-economy 50%, /quest-economy 333k 30k, /quest-economy off).",
    getArgumentCompletions: economyCompletions,
    handler: withContext((args: string, ctx) =>
      handleQuestEconomyCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-warning", {
    description:
      "Configure pre-compaction warning margin (e.g. /quest-warning 30k).",
    getArgumentCompletions: warningCompletions,
    handler: withContext((args: string, ctx) =>
      handleQuestWarningCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-subquest-threshold", {
    description:
      "Configure the minimum token threshold for auto-compacting when launching a sub-quest (e.g. /quest-subquest-threshold 40k).",
    getArgumentCompletions: subquestThresholdCompletions,
    handler: withContext((args: string, ctx) =>
      handleQuestSubquestThresholdCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("quest-status", {
    description: "Show the active quest and whether its file is fresh.",
    handler: withContext((args: string, ctx) =>
      handleQuestStatusCommand(args, ctx)
    ),
  });
  pi.registerCommand("quests", {
    description: "List current and future quests.",
    handler: withContext((args: string, ctx) => handleQuestsCommand(args, ctx)),
  });
  pi.registerCommand("subquest", {
    description:
      "Create and switch to a sub-quest linked to the current active quest (e.g. /subquest error-handling Handle network disconnects).",
    getArgumentCompletions: getQuestCompletions,
    handler: withContext((args: string, ctx) =>
      handleSubquestCommand(args, ctx, pi)
    ),
  });
  pi.registerCommand("sub-quest", {
    description: "Alias for /subquest.",
    getArgumentCompletions: getQuestCompletions,
    handler: withContext((args: string, ctx) =>
      handleSubquestCommand(args, ctx, pi)
    ),
  });
}
