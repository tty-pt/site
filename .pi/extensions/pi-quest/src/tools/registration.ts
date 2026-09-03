import { withContext } from "../context.ts";
import { QUEST_ARCHIVE_DIR } from "../constants.ts";
import { ExtensionAPI, ExtensionContext } from "../types.ts";
import { executeArchiveTool } from "./archive_operation.ts";
import { executeAskHumanTool } from "./ask_human_operation.ts";
import { executeRebutTool } from "./rebut_operation.ts";
import { executeSubquestTool } from "./subquest_operation.ts";
import { executeMarkTool, executeUpdateStateTool } from "./update_operation.ts";
import {
  QUEST_MARK_SAVED_SCHEMA,
  QUEST_UPDATE_STATE_SCHEMA,
} from "./validation.ts";

export function installArchiveTool(pi: ExtensionAPI) {
  const archiveHandler = withContext((
    _toolCallId: string,
    params: any,
    _signal: any,
    _onUpdate: any,
    ctx: ExtensionContext,
  ) => executeArchiveTool(params, pi, ctx));

  pi.registerTool({
    name: "quest_archive",
    label: "Archive Quest",
    description:
      `Archive the active (or specified) quest into ${QUEST_ARCHIVE_DIR}/<qid>.zip and optionally trigger session context compaction. Use abandon:true to archive with unresolved reassessment (records contradiction).`,
    parameters: {
      type: "object",
      properties: {
        questName: {
          type: "string",
          description:
            "Quest name to archive. Defaults to currently active quest.",
        },
        compact: {
          type: "boolean",
          description:
            "Whether to immediately trigger session context compaction after archiving (defaults to true).",
        },
        abandon: {
          type: "boolean",
          description:
            "If true, archive even with reassessment pending — records unresolved contradiction and bypasses gate (for human escalation).",
        },
      },
      additionalProperties: false,
    },
    execute: archiveHandler,
  });
}

export function installRebutTool(pi: ExtensionAPI) {
  const handler = withContext((
    _toolCallId: string,
    params: any,
    _signal: any,
    _onUpdate: any,
    ctx: ExtensionContext,
  ) => executeRebutTool(params, pi, ctx));
  pi.registerTool({
    name: "quest_rebut",
    label: "Rebut Review",
    description:
      "Present evidence-based rebuttal to the last critical review. Triggers a fresh reviewer pass with your rebuttal appended; unlimited rounds, verdict reversal reopens the gate, persisted to ## Review Dialogue.",
    parameters: {
      type: "object",
      properties: {
        rebuttal: {
          type: "string",
          description:
            "Evidence-based rebuttal addressing each reviewer finding with file:line citations.",
        },
        questName: {
          type: "string",
          description: "Quest name. Defaults to active quest.",
        },
      },
      required: ["rebuttal"],
      additionalProperties: false,
    },
    execute: handler,
  });
}

export function installAskHumanTool(pi: ExtensionAPI) {
  const handler = withContext((
    _toolCallId: string,
    params: any,
    _signal: any,
    _onUpdate: any,
    ctx: ExtensionContext,
  ) => executeAskHumanTool(params, pi, ctx));
  pi.registerTool({
    name: "quest_ask_human",
    label: "Ask Human",
    description:
      "Escalate to human for decision — sends full dialogue transcript and question, sets awaitingUserConfirmation, human can uphold reviewer / side with implementer / guide.",
    parameters: {
      type: "object",
      properties: {
        question: {
          type: "string",
          description: "Question for the human, include context and options.",
        },
        context: {
          type: "string",
          description: "Additional context or transcript excerpt.",
        },
      },
      required: ["question"],
      additionalProperties: false,
    },
    execute: handler,
  });
}

export function installSubQuestTool(pi: ExtensionAPI) {
  const subquestHandler = withContext((
    _toolCallId: string,
    params: any,
    _signal: any,
    _onUpdate: any,
    ctx: ExtensionContext,
  ) => executeSubquestTool(params, pi, ctx));

  const subquestParams = {
    type: "object",
    properties: {
      name: {
        type: "string",
        description: "Sub-quest name/slug. Optional if goal is provided.",
      },
      goal: {
        type: "string",
        description:
          "Goal or description of what this sub-quest will accomplish.",
      },
      parentName: {
        type: "string",
        description: "Parent quest name. Defaults to currently active quest.",
      },
      switchNow: {
        type: "boolean",
        description:
          "Whether to immediately switch the active session quest to this sub-quest (default: true). Set to false when planning sub-quests upfront.",
      },
    },
    required: ["goal"],
    additionalProperties: false,
  };

  pi.registerTool({
    name: "quest_subquest",
    label: "Create Sub-Quest",
    description:
      "Create or plan a sub-quest for a genuinely separable workstream, distinct architectural subsystem, self-contained investigation, or independent verification boundary. Use switchNow: false during initial quest planning to pre-create planned sub-quests and link them into the parent coordination plan without switching away from the active parent quest, or switchNow: true (default) to create and immediately switch focus to the sub-quest. Updates the parent quest, and records parent reference.",
    parameters: subquestParams,
    execute: subquestHandler,
  });
}

export function installMarkTool(pi: ExtensionAPI) {
  const markHandler = withContext((
    _toolCallId: string,
    params: any,
    _signal: any,
    _onUpdate: any,
    ctx: ExtensionContext,
  ) => executeMarkTool(params, pi, ctx));

  pi.registerTool({
    name: "quest_mark_saved",
    label: "Mark Quest Saved",
    description:
      "Record that the active quest file has been written to disk. Call after updating the quest file.",
    parameters: QUEST_MARK_SAVED_SCHEMA,
    execute: markHandler,
  });

  const updateStateHandler = withContext((
    _toolCallId: string,
    params: any,
    _signal: any,
    _onUpdate: any,
    ctx: ExtensionContext,
  ) => executeUpdateStateTool(params, pi, ctx));

  pi.registerTool({
    name: "quest_update_state",
    label: "Update Quest State",
    description:
      "Update the active quest state on disk with structured fields (status, findings, decisions, remaining work, next step). Formats and saves the quest file deterministically.",
    parameters: QUEST_UPDATE_STATE_SCHEMA,
    execute: updateStateHandler,
  });
}
