// HIGH_LEVEL: #tools (main agent) — quest_update_state.
// The agent's write path to the quest: findings, drafts, amendments, claims.
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, replaceState, updateState } from "../../app/store";
import { emitNow, sendSteer } from "../../app/interpreter";
import {
  acknowledgeChild,
  claimComplete,
  createDraft,
  createQuest,
  recordAmendment,
  recordRefinement,
  unfinishedChildren,
} from "../../domain/quest";
import { nextQid } from "../../domain/qid";
import { draftPath } from "../../domain/paths";
import { type Qid } from "../../domain/qid";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { ensureValidationFlow } from "../../validation/flow";
import { hashContent, maybeBootDraftReview, splicePlanSection } from "../../drafting/reviews";
import { ensureDraftFile, listKnownQids } from "../../files";
import { textResult } from "./reply";

import type { QuestState } from "../../domain/quest";

async function provisionRootQuest(ctx: PiCtx, objective: string): Promise<Qid> {
  const qid = nextQid(Date.now() / 1000, await listKnownQids(ctx.cwd));
  replaceState(createQuest(objective, qid));
  await ensureDraftFile(ctx, qid, qid, objective);
  return qid;
}

async function claimForValidation(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
): Promise<QuestState> {
  if (state.phase !== "implementing") {
    throw new Error(`cannot claim completion from phase ${state.phase}`);
  }
  const unfinished = unfinishedChildren(state);
  if (unfinished.length > 0) {
    throw new Error(`complete blocked: unfinished children ${unfinished.map((c) => c.qid).join(", ")}`);
  }
  const claimed = updateState((s) => claimComplete(s));
  emitNow(pi);
  sendSteer(pi, `Quest ${claimed.qid} claimed complete. Validator booting against the approved plan.`);
  void ensureValidationFlow(pi, ctx);
  return claimed;
}

async function writePlanToDraft(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  planText: string,
): Promise<QuestState> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) {
    throw new Error(`plan text needs an active draft (phase ${state.phase})`);
  }
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const updated = splicePlanSection(current, planText);
  if (updated === current) throw new Error("plan text identical to the draft file");
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  const next = updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, planAuthored: true, contentHash: hash },
    snapshotPending: true,
  });
  emitNow(pi);
  void maybeBootDraftReview(pi, ctx);
  return next;
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
    const qid = await provisionRootQuest(ctx, objective.trim());
    state = getState();
    applied.push(`created quest ${qid}`);
  }
  const draftName = params["draftName"];
  if (typeof draftName === "string" && draftName.trim() !== "" && state.phase === "provisional" && state.qid !== null) {
    const qid = state.qid;
    state = createDraft(state, draftName.trim());
    replaceState(state);
    await ensureDraftFile(ctx, qid, draftName.trim(), state.objective);
    state = getState();
    applied.push(`draft ${draftName.trim()} created at ${draftPath(qid)} — edit ONLY this file`);
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
  const plan = params["plan"];
  if (typeof plan === "string" && plan.trim() !== "" && state.qid) {
    try {
      state = await writePlanToDraft(pi, ctx, state, plan.trim());
      applied.push("plan recorded in the draft file");
    } catch (err) {
      return { applied, error: err instanceof Error ? err.message : String(err) };
    }
  }
  const continuePast = params["continuePast"];
  if (typeof continuePast === "string" && continuePast.trim() !== "" && state.qid) {
    try {
      const childQid = continuePast.trim();
      state = updateState((s) => acknowledgeChild(s, childQid as Qid));
      applied.push(`continued past child ${childQid}`);
    } catch (err) {
      return { applied, error: err instanceof Error ? err.message : String(err) };
    }
  }
  if (params["claimComplete"] === true && state.qid) {
    try {
      state = await claimForValidation(pi, ctx, state);
      applied.push("completion claimed; validator booting");
    } catch (err) {
      return { applied, error: err instanceof Error ? err.message : String(err) };
    }
    return { applied };
  }
  if (applied.length > 0) emitNow(pi);
  return { applied };
}

export function updateStateTool(pi: Pi): PiToolSpec {
  return {
    name: "quest_update_state",
    label: "Update Quest State",
    description: "Record findings, drafts, amendments, and state. The agent's write path to the quest: pass objective to create, draftName to draft, refinement/amendment/exactNextAction to record, plan to author the draft Implementation Plan section directly, claimComplete to finish.",
    parameters: {
      type: "object",
      properties: {
        objective: { type: "string" },
        draftName: { type: "string" },
        refinement: { type: "string" },
        plan: { type: "string", description: "Implementation Plan body, spliced into the draft file (drafting only)." },
        amendment: {
          type: "object",
          properties: { change: { type: "string" }, reasons: { type: "string" } },
        },
        exactNextAction: { type: "string" },
        claimComplete: { type: "boolean" },
        continuePast: { type: "string", description: "Returned child qid to explicitly continue past." },
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
