// HIGH_LEVEL: #tools (main agent) — quest_update_state.
// The agent's write path to the quest: findings, drafts, amendments, claims.
import { mkdir, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, replaceState, updateState } from "../../app/store";
import { emitNow, sendSteer } from "../../app/interpreter";
import {
  claimComplete,
  createDraft,
  createQuest,
  recordAmendment,
  recordRefinement,
  unfinishedChildren,
} from "../../domain/quest";
import { nextQid } from "../../domain/qid";
import { draftPath, FUTURE_DIR } from "../../domain/paths";
import { isQid, type Qid } from "../../domain/qid";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { ensureValidationFlow } from "../../validation/flow";
import { renderDraftTemplate } from "../../views/draft-template";
import { listKnownQids } from "../../files";
import { textResult } from "./reply";

async function ensureDraftFile(ctx: PiCtx, rawQid: string, name: string, objective: string): Promise<void> {
  if (!isQid(rawQid)) throw new Error(`invalid qid: ${rawQid}`);
  const qid: Qid = rawQid;
  await mkdir(join(ctx.cwd, FUTURE_DIR), { recursive: true });
  try {
    await writeFile(join(ctx.cwd, draftPath(qid)), renderDraftTemplate(name, objective), { flag: "wx" });
  } catch (err) {
    if ((err as { code?: string }).code !== "EEXIST") throw err;
  }
}

export async function applyUpdate(
  pi: Pi,
  ctx: PiCtx,
  params: Record<string, unknown>,
): Promise<{ applied: string[]; error?: string }> {
  const applied: string[] = [];
  let state = getState();
  if (state.qid === null) {
    const objective = params["objective"];
    if (typeof objective !== "string" || objective.trim() === "") {
      return { applied, error: "no active quest; pass objective to create one" };
    }
    const qid = nextQid(Date.now() / 1000, await listKnownQids(ctx.cwd));
    state = createQuest(objective.trim(), qid);
    replaceState(state);
    applied.push(`created quest ${qid}`);
  }
  const draftName = params["draftName"];
  if (typeof draftName === "string" && draftName.trim() !== "" && state.phase === "provisional" && state.qid !== null) {
    const qid = state.qid;
    state = createDraft(state, draftName.trim());
    replaceState(state);
    await ensureDraftFile(ctx, qid, draftName.trim(), state.objective);
    state = getState();
    applied.push(`draft ${draftName.trim()} created`);
  }
  const refinement = params["refinement"];
  if (typeof refinement === "string" && refinement.trim() !== "" && state.qid) {
    state = updateState((s) => recordRefinement(s, refinement.trim()));
    applied.push("refinement recorded");
  }
  const amendment = params["amendment"];
  const change = typeof amendment === "string" ? amendment : (amendment as Record<string, unknown> | undefined)?.["change"];
  const reasons = ((amendment as Record<string, unknown> | undefined)?.["reasons"] as string | undefined) ?? "";
  if (typeof change === "string" && change.trim() !== "" && state.qid) {
    try {
      state = updateState((s) => recordAmendment(s, change.trim(), typeof reasons === "string" ? reasons : ""));
      applied.push("amendment recorded");
    } catch (err) {
      return { applied, error: err instanceof Error ? err.message : String(err) };
    }
  }
  const next = params["exactNextAction"];
  if (typeof next === "string" && next.trim() !== "" && state.qid) {
    const text = next.trim();
    state = updateState((s) => ({ ...s, exactNextAction: text, snapshotPending: true }));
    applied.push("next action updated");
  }
  if (params["claimComplete"] === true && state.qid) {
    if (state.phase !== "implementing") {
      return { applied, error: `cannot claim completion from phase ${state.phase}` };
    }
    const unfinished = unfinishedChildren(state);
    if (unfinished.length > 0) {
      return { applied, error: `complete blocked: unfinished children ${unfinished.map((c) => c.qid).join(", ")}` };
    }
    state = updateState((s) => claimComplete(s));
    emitNow(pi);
    applied.push("completion claimed; validator booting");
    sendSteer(pi, `Quest ${state.qid} claimed complete. Validator booting against the approved plan.`);
    void ensureValidationFlow(pi, ctx);
    return { applied };
  }
  if (applied.length > 0) emitNow(pi);
  return { applied };
}

export function updateStateTool(pi: Pi): PiToolSpec {
  return {
    name: "quest_update_state",
    label: "Update Quest State",
    description: "Record findings, drafts, amendments, and state. The agent's write path to the quest: pass objective to create, draftName to draft, refinement/amendment/exactNextAction to record, claimComplete to finish.",
    parameters: {
      type: "object",
      properties: {
        objective: { type: "string" },
        draftName: { type: "string" },
        refinement: { type: "string" },
        amendment: {
          type: "object",
          properties: { change: { type: "string" }, reasons: { type: "string" } },
        },
        exactNextAction: { type: "string" },
        claimComplete: { type: "boolean" },
      },
      additionalProperties: false,
    },
    execute: async (_toolCallId, params, _signal, _onUpdate, ctx) => {
      try {
        const result = await applyUpdate(pi, ctx, params);
        if (result.error) return textResult(`Update failed: ${result.error}`, { error: result.error });
        if (result.applied.length === 0) return textResult("Nothing to update.", {});
        return textResult(`Updated: ${result.applied.join("; ")}.`, { applied: result.applied });
      } catch (err) {
        const detail = err instanceof Error ? err.message : String(err);
        return textResult(`Update failed: ${detail}`, { error: detail });
      }
    },
  };
}
