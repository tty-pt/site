// HIGH_LEVEL: #tools (main agent) — quest_rebut.
// HIGH_LEVEL: #rebuttal — recorded as a new event; may reopen the question.
import { getState, updateState } from "../../app/store";
import { emitNow } from "../../app/interpreter";
import { recordRebuttal, resolveDialogueRound } from "../../domain/quest";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { bootDraftReview } from "../../drafting/reviews";
import { ensureValidationFlow } from "../../validation/flow";
import { implementationFingerprint } from "../../review/flow";
import { cancelReview } from "../../review/tracker";
import { readQuestConfig } from "../../config";
import { textResult } from "./reply";

export function rebutTool(pi: Pi): PiToolSpec {
  return {
    name: "quest_rebut",
    label: "Rebut Review",
    description: "Answer a review with evidence; a successful rebuttal reopens the question and boots a fresh review.",
    parameters: {
      type: "object",
      properties: {
        rebuttal: { type: "string", description: "Evidence addressing each finding with file:line citations." },
      },
      required: ["rebuttal"],
      additionalProperties: false,
    },
    execute: async (_toolCallId, params, signal, _onUpdate, ctx: PiCtx) => {
      const rebuttal = params["rebuttal"];
      if (typeof rebuttal !== "string" || rebuttal.trim().length < 10) {
        return textResult("quest_rebut needs a substantive rebuttal (at least 10 chars).", { error: "rebuttal_too_short" });
      }
      const state = getState();
      if ((state.phase !== "drafting" && state.phase !== "validating") || state.qid === null) {
        return textResult("Nothing under review to rebut.", { error: "nothing_to_rebut" });
      }
      if (state.lastReview === null) {
        return textResult("No review verdict recorded yet.", { error: "no_review" });
      }
      const stopRebuttalReview = state.qid;
      if (signal?.aborted) return textResult("Rebuttal aborted.", { error: "aborted" });
      try {
        updateState((s) =>
          recordRebuttal(s, rebuttal.trim(), state.lastReview!.verdict, state.lastReview!.findings).state
        );
        emitNow(pi);
        const round = getState().reviewDialogue.length;
        if (state.phase === "drafting") {
          const target = getState().draft?.contentHash;
          if (target === null || target === undefined) {
            return textResult("Draft has no reviewed revision yet.", { error: "nothing_reviewed" });
          }
          const onAbort = () => {
            if (stopRebuttalReview) cancelReview(stopRebuttalReview);
          };
          signal?.addEventListener("abort", onAbort, { once: true });
          await bootDraftReview(pi, ctx, target, await readQuestConfig(ctx.cwd));
          const after = getState();
          if (after.lastReview?.target === target) {
            updateState((s) => resolveDialogueRound(s, round, after.lastReview!.verdict));
            emitNow(pi);
          }
        } else {
          await ensureValidationFlow(pi, ctx);
          const after = getState();
          const fingerprint = implementationFingerprint(after);
          if (after.lastReview?.target === fingerprint) {
            updateState((s) => resolveDialogueRound(s, round, after.lastReview!.verdict));
            emitNow(pi);
          }
        }
        const verdict = getState().lastReview?.verdict ?? "UNKNOWN";
        return textResult(`Rebuttal round ${round} submitted. Review verdict: ${verdict}.`, { round, verdict });
      } catch (err) {
        const detail = err instanceof Error ? err.message : String(err);
        return textResult(`Rebuttal failed: ${detail}`, { error: detail });
      }
    },
  };
}
