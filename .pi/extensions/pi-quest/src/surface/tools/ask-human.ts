// HIGH_LEVEL: #tools (main agent) — quest_ask_human.
// Ask with a recommended default and timeout; never blocks.
import { askWithDefault, askingToolAvailable } from "../../absence/ask";
import { readQuestConfig } from "../../config";
import type { Pi, PiToolSpec } from "../../hooks/events";
import { textResult } from "./reply";

export function askHumanTool(pi: Pi): PiToolSpec {
  return {
    name: "quest_ask_human",
    label: "Ask Human",
    description: "Ask the user with a recommended default and timeout (one minute default, configurable). Never blocks: absence, cancellation, or timeout proceeds with the default, and a late answer still applies.",
    parameters: {
      type: "object",
      properties: {
        question: { type: "string", description: "Question for the human, with context and options." },
        default: { type: "string", description: "Recommended default used on absence, timeout, or cancellation." },
        timeoutMs: { type: "number", description: "Wait duration in ms. 0 = no wait, negative = indefinite." },
      },
      required: ["question", "default"],
      additionalProperties: false,
    },
    execute: async (_toolCallId, params, _signal, _onUpdate, ctx) => {
      const question = params["question"];
      const def = params["default"];
      if (typeof question !== "string" || question.trim() === "") {
        return textResult("quest_ask_human needs a question.", { error: "missing_question" });
      }
      if (typeof def !== "string") {
        return textResult("quest_ask_human needs a default answer.", { error: "missing_default" });
      }
      const timeoutMs = typeof params["timeoutMs"] === "number" ? params["timeoutMs"] as number : undefined;
      const config = await readQuestConfig(ctx.cwd);
      const askingTool = config.bindings.asking.tool;
      const result = await askWithDefault(pi, ctx, { question: question.trim(), defaultAnswer: def, timeoutMs });
      return textResult(
        result.source === "user"
          ? `Human answered: "${result.answer}"`
          : `No human answer (absence) — proceeding with default: "${result.answer}"`,
        { answer: result.answer, source: result.source, askingTool, askingAvailable: askingToolAvailable(pi, askingTool) },
      );
    },
  };
}
