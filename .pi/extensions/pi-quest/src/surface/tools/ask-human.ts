// HIGH_LEVEL: #tools (main agent) — quest_ask_human.
// Ask with a recommended default and timeout; never blocks.
// Delegates to the native asking tool when available, falls back to ctx.ui.input.
import { getState, updateState } from "../../app/store";
import { emitNow } from "../../app/interpreter";
import { recordHumanAnswer } from "../../domain/quest";
import { askWithDefault, askingToolAvailable } from "../../absence/ask";
import { readQuestConfig } from "../../config";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { textResult } from "./reply";

function extractAnswerFromNative(result: { content: Array<{ type: string; text?: string }> }): string | null {
  for (const part of result.content) {
    if (part.type === "text" && typeof part.text === "string" && part.text.trim() !== "") {
      return part.text.trim();
    }
  }
  return null;
}

function recordAnswer(pi: Pi, question: string, answer: string, late: boolean): void {
  updateState((s) => recordHumanAnswer(s, question, answer, late));
  emitNow(pi);
}

function buildResult(answer: string, source: "user" | "default", askingTool: string, toolAvailable: boolean, ctx: PiCtx) {
  return textResult(
    source === "user" ? `Human answered: "${answer}"` : `No human answer (absence) — proceeding with default: "${answer}"`,
    { answer, source, askingTool, askingAvailable: toolAvailable, uiPresent: ctx.hasUI },
  );
}

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
      const toolAvailable = askingToolAvailable(pi, askingTool);
      const trimmed = question.trim();

      // Delegate to the native asking tool when available.
      if (typeof pi.executeTool === "function" && toolAvailable) {
        const ac = new AbortController();
        const nativeCall = pi.executeTool(
          askingTool,
          { questions: [{ header: "Quest question", question: trimmed, options: [{ label: def, description: "Default" }] }] },
          ac.signal,
          undefined,
          ctx,
        );
        const effectiveTimeout = timeoutMs ?? config.askTimeoutMs;

        // Race the native call against the timeout.
        const settled = await new Promise<"timeout" | "answer" | "error">((resolve) => {
          if (effectiveTimeout < 0) {
            void nativeCall.then(
              () => resolve("answer"),
              () => resolve("error"),
            );
            return;
          }
          const timer = setTimeout(() => { ac.abort(); resolve("timeout"); }, effectiveTimeout);
          void nativeCall.then(
            () => { clearTimeout(timer); resolve("answer"); },
            () => { clearTimeout(timer); resolve("error"); },
          );
        });

        if (settled === "answer") {
          const nativeResult = await nativeCall.catch(() => null);
          if (nativeResult !== null) {
            const nativeAnswer = extractAnswerFromNative(nativeResult);
            if (nativeAnswer !== null) {
              recordAnswer(pi, trimmed, nativeAnswer, false);
              return buildResult(nativeAnswer, "user", askingTool, toolAvailable, ctx);
            }
          }
        }

        if (settled === "timeout") {
          recordAnswer(pi, trimmed, def, false);
          ctx.ui.notify(`Question timed out: ${trimmed.slice(0, 120)} — defaulting to "${def}"`, "warning");
          return buildResult(def, "default", askingTool, toolAvailable, ctx);
        }
      }

      // Fallback: use ctx.ui.input directly.
      const result = await askWithDefault(pi, ctx, { question: trimmed, defaultAnswer: def, timeoutMs });
      return buildResult(result.answer, result.source, askingTool, toolAvailable, ctx);
    },
  };
}
