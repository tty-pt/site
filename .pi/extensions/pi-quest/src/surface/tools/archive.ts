// HIGH_LEVEL: #tools (main agent) — quest_archive.
// HIGH_LEVEL: #surface — slim archive: quest view, session reference, manifest.
import { getState, replaceState, updateState } from "../../app/store";
import { emitNow, sendSteer } from "../../app/interpreter";
import type { ArchivedOutcome } from "../../domain/quest";
import { archive } from "../../domain/quest";
import { settleChild } from "../../domain/children";
import { newestSnapshotFor } from "../../durability/snapshots";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { implementationFingerprint } from "../../review/flow";
import { cancelReview } from "../../review/tracker";
import { archiveQuestFiles } from "../../views/quest-view";
import { textResult } from "./reply";

export type ArchiveOutcomeParam = "completed" | "failed";

function toOutcome(param: ArchiveOutcomeParam): ArchivedOutcome {
  if (param === "completed") return "COMPLETED";
  return "FAILED";
}

export function hasValidationPass(): boolean {
  const state = getState();
  if (state.qid === null) return false;
  return state.lastReview?.verdict === "PASS" &&
    state.lastReview.target === implementationFingerprint(state);
}

export async function archiveActiveQuest(
  pi: Pi,
  ctx: PiCtx,
  outcome: ArchivedOutcome,
  summary: string | null,
  opts?: { confirmDiscard?: boolean },
): Promise<{ archivedQid: string; zipPath: string; returnedToParent: string | null }> {
  const state = getState();
  if (state.qid === null) throw new Error("no active quest to archive");
  const qid = state.qid;
  const validated = hasValidationPass();
  if (outcome === "COMPLETED") {
    if (!validated) {
      throw new Error(
        "COMPLETED requires a current validation PASS — run quest_update_state {claimComplete:true} to enter validating, await the validation PASS (or reply CONFIRM when no validator is available), then archive again",
      );
    }
  }
  if (outcome === "FAILED") {
    const target = implementationFingerprint(state);
    const review = state.lastReview;
    if (review === null || review.target !== target || review.verdict !== "FAIL") {
      throw new Error(
        "FAILED needs a validation FAIL on the current work — keep implementing (continueWork:true returns from validating), claim completion to validate, or hold for the user to reply CONFIRM when no validator is available",
      );
    }
  }
  // ABANDONED stays user-only: the quest_archive tool no longer offers it.
  // /quest-del is the single path here, passing confirmDiscard explicitly.
  if (outcome === "ABANDONED" && !validated) {
    const confirmed = opts?.confirmDiscard === true;
    const noted = typeof summary === "string" && summary.trim() !== "";
    if (!confirmed || !noted) {
      throw new Error(
        `archiving as ${outcome} discards unvalidated work — pass confirmDiscard:true with a summary describing what was discarded and why, or run quest_update_state {claimComplete:true} to validate first`,
      );
    }
  }
  cancelReview(qid);
  const zipPath = await archiveQuestFiles(pi, ctx.cwd, state, outcome, summary);
  const archived = updateState((s) => archive(s, outcome));
  emitNow(pi);
  if (archived.parentQid !== null) {
    const parentQid = archived.parentQid;
    const parent = newestSnapshotFor(ctx.sessionManager.getEntries(), parentQid);
    if (parent === null) {
      replaceState({ ...archived, parentQid: null });
      sendSteer(pi, `Quest ${qid} archived as ${outcome} (${zipPath}). Parent ${parentQid} has no snapshot — staying on the archived quest.`);
      return { archivedQid: qid, zipPath, returnedToParent: null };
    }
    const findings = summary ?? `Child ${qid} archived as ${outcome}.`;
    replaceState({ ...parent, snapshotPending: true });
    updateState((s) => settleChild(s, qid, outcome === "COMPLETED" ? "returned" : "failed", findings));
    emitNow(pi);
    sendSteer(pi, `Child quest ${qid} archived as ${outcome} (${zipPath}). Returned to parent ${parentQid} with findings: ${findings}`);
    return { archivedQid: qid, zipPath, returnedToParent: parentQid };
  }
  replaceState({ ...archived, phase: "idle", qid: null, parentQid: null, snapshotPending: true });
  emitNow(pi);
  sendSteer(pi, `Quest ${qid} archived as ${outcome} (${zipPath}). No active quest.`);
  return { archivedQid: qid, zipPath, returnedToParent: null };
}

export function archiveTool(pi: Pi): PiToolSpec {
  return {
    name: "quest_archive",
    label: "Archive Quest",
    description: "Finish the active quest as completed or failed: renders the quest view, writes the run manifest, stores a session-range reference. COMPLETED requires a current validation PASS (claim via quest_update_state {claimComplete:true} first; usually automatic on validation PASS). FAILED requires a validation FAIL on the current work. Abandoning a quest is user-only via /quest-del.",
    parameters: {
      type: "object",
      properties: {
        outcome: { type: "string", enum: ["completed", "failed"] },
        summary: { type: "string", description: "Findings summary, returned to the parent for sub-quests." },
      },
      required: ["outcome"],
      additionalProperties: false,
    },
    execute: async (_toolCallId, params, _signal, _onUpdate, ctx) => {
      const outcome = params["outcome"];
      if (outcome !== "completed" && outcome !== "failed") {
        return textResult("quest_archive needs outcome completed|failed.", { error: "bad_outcome" });
      }
      const summary = typeof params["summary"] === "string" ? params["summary"] as string : null;
      try {
        const done = await archiveActiveQuest(pi, ctx, toOutcome(outcome), summary);
        return textResult(
          `Quest ${done.archivedQid} archived (${outcome}, ${done.zipPath}).${done.returnedToParent ? ` Returned to parent ${done.returnedToParent}.` : ""}`,
          { archived: done.archivedQid, outcome, zip: done.zipPath },
        );
      } catch (err) {
        const detail = err instanceof Error ? err.message : String(err);
        return textResult(`Archive failed: ${detail}`, { error: "archive_failed" });
      }
    },
  };
}
