// HIGH_LEVEL: #tools (main agent) — quest_update_state.
// HIGH_LEVEL: #plan revision — planRevision stages a re-reviewable revision.
// The agent's write path to the quest: findings, drafts, amendments, claims.
import { readFile, writeFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, replaceState, updateState } from "../../app/store";
import { emitNow, sendSteer } from "../../app/interpreter";
import {
  claimComplete,
  createDraft,
  createQuest,
  demoteToImplementing,
  recordAmendment,
  recordRefinement,
} from "../../domain/quest";
import { recordPlanRevision } from "../../domain/plan-revision";
import { acknowledgeChild, unfinishedChildren } from "../../domain/children";
import { bootPlanRevisionReview } from "../../implementing/plan-revision";
import { nextQid } from "../../domain/qid";
import { draftPath } from "../../domain/paths";
import { type Qid } from "../../domain/qid";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { ensureValidationFlow } from "../../validation/flow";
import { hashContent, maybeBootDraftReview, parseDraftSections, splicePlanSection } from "../../drafting/reviews";
import { setDocStatus } from "../../quest-doc";
import { noteDraftUpdated } from "../../durability/status";
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
  const qid = state.qid;
  if (qid === null) {
    throw new Error("cannot claim completion without an active quest");
  }
  const unfinished = unfinishedChildren(state);
  if (unfinished.length > 0) {
    throw new Error(`complete blocked: unfinished children ${unfinished.map((c) => c.qid).join(", ")}`);
  }
  const claimed = updateState((s) => claimComplete(s));
  await setDocStatus(ctx, qid, "validating", true);
  emitNow(pi);
  sendSteer(pi, `Quest ${claimed.qid} claimed complete. Validator booting against the approved plan.`);
  void ensureValidationFlow(pi, ctx);
  return getState();
}

async function writePlanToDraft(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  planText: string,
): Promise<QuestState> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) {
    throw new Error(`plan text needs an active draft (phase ${state.phase}); the plan is drafting-only — record post-approval deviations via amendment or refinement`);
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
  noteDraftUpdated(ctx);
  void maybeBootDraftReview(pi, ctx);
  return next;
}

// Mid-implementation plan revision: the objective is immutable here — a new
// goal is a scope change and belongs in a new quest. The revision stages new
// content and boots a re-review; the approved binding moves only on PASS.
async function writePlanRevision(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  planText: string,
  note: string,
  objective: unknown,
): Promise<QuestState> {
  if (state.phase !== "implementing" || state.draft === null || state.qid === null) {
    throw new Error(`plan revision needs an implementing quest (phase ${state.phase})`);
  }
  if (typeof objective === "string" && objective.trim() !== "") {
    const current = state.pendingRootRequest ?? state.objective;
    if (objective.trim() !== current) {
      throw new Error("plan revision keeps the quest objective — a new goal is a scope change, start a new quest");
    }
  }
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const previousPlan = parseDraftSections(current).plan;
  const updated = splicePlanSection(current, planText);
  if (updated === current) throw new Error("plan revision identical to the draft file");
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  const previousHash = state.draft.contentHash;
  const next = updateState((s) => recordPlanRevision(s, previousHash, hash, note === "" ? "plan revision" : note, previousPlan));
  emitNow(pi);
  noteDraftUpdated(ctx);
  void bootPlanRevisionReview(pi, ctx, hash);
  return next;
}

async function carryRefinementsToDraft(ctx: PiCtx, state: QuestState): Promise<QuestState> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) return state;
  if (state.refinements.length === 0) return state;
  const path = join(ctx.cwd, draftPath(state.qid));
  const current = await readFile(path, "utf8");
  const items = state.refinements.map((refinement) => `- ${refinement}`).join("\n");
  const body = current.endsWith("\n") ? current : `${current}\n`;
  const updated = `${body}\n## Findings (pre-draft investigation)\n\n${items}\n`;
  await writeFile(path, updated, "utf8");
  const hash = hashContent(updated);
  return updateState((s) => s.draft === null ? s : {
    ...s,
    draft: { ...s.draft, contentHash: hash },
    snapshotPending: true,
  });
}

async function provisionDraft(
  ctx: PiCtx,
  state: QuestState,
  params: Record<string, unknown>,
  applied: string[],
): Promise<QuestState> {
  const draftName = params["draftName"];
  if (typeof draftName !== "string" || draftName.trim() === "" || state.phase !== "provisional" || state.qid === null) {
    return state;
  }
  const qid = state.qid;
  const name = draftName.trim();
  const plan = params["plan"];
  const thin = state.refinements.length === 0 && (typeof plan !== "string" || plan.trim() === "");
  state = createDraft(state, name);
  replaceState(state);
  await ensureDraftFile(ctx, qid, name, state.objective);
  state = await carryRefinementsToDraft(ctx, getState());
  applied.push(`draft ${name} created at ${draftPath(qid)} — edit ONLY this file`);
  if (thin) {
    applied.push("draft created thin — no findings recorded yet; file them via refinement or author the plan via {plan:}");
  }
  noteDraftUpdated(ctx);
  return state;
}

async function applyPlanRevisionParam(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  params: Record<string, unknown>,
  applied: string[],
): Promise<{ state: QuestState; error?: string }> {
  const revision = params["planRevision"];
  if (typeof revision !== "string" || revision.trim() === "" || !state.qid) return { state };
  try {
    const note = params["note"];
    const next = await writePlanRevision(pi, ctx, state, revision.trim(), typeof note === "string" ? note.trim() : "", params["objective"]);
    applied.push("plan revision staged in the draft file; re-review booted");
    return { state: next };
  } catch (err) {
    return { state, error: err instanceof Error ? err.message : String(err) };
  }
}

// A validating quest with no validator to answer it goes back to work
// instead of stalling: the agent keeps implementing and claims again. The
// in-file ## Status marker is reset so a fresh claim re-arms the trigger.
async function applyContinueWorkParam(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  params: Record<string, unknown>,
  applied: string[],
): Promise<{ state: QuestState; error?: string }> {
  if (params["continueWork"] !== true || !state.qid) return { state };
  try {
    const qid = state.qid;
    const next = updateState((s) => demoteToImplementing(s));
    await setDocStatus(ctx, qid, "implementing", false);
    applied.push("returned to implementing to continue the work");
    return { state: next };
  } catch (err) {
    return { state, error: err instanceof Error ? err.message : String(err) };
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
    const qid = await provisionRootQuest(ctx, objective.trim());
    state = getState();
    applied.push(`created quest ${qid}`);
  }
  const draftName = params["draftName"];
  state = await provisionDraft(ctx, state, params, applied);
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
  const revised = await applyPlanRevisionParam(pi, ctx, state, params, applied);
  if (revised.error !== undefined) return { applied, error: revised.error };
  state = revised.state;
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
  const continued = await applyContinueWorkParam(pi, ctx, state, params, applied);
  if (continued.error !== undefined) return { applied, error: continued.error };
  state = continued.state;
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
    description: "Record findings, drafts, amendments, and state. The agent's write path to the quest: pass objective to create, draftName to draft, refinement/amendment/exactNextAction to record, plan to author the draft Implementation Plan section directly (drafting only), planRevision with an optional note to revise the approved plan mid-implementation (boots a re-review), claimComplete to finish, continueWork to return a validating quest to implementing.",
    parameters: {
      type: "object",
      properties: {
        objective: { type: "string" },
        draftName: { type: "string" },
        refinement: { type: "string" },
        plan: { type: "string", description: "Implementation Plan body, spliced into the draft file (drafting only)." },
        planRevision: { type: "string", description: "Revised Implementation Plan body, staged from implementing (boots a re-review; objective unchanged)." },
        note: { type: "string", description: "Why the plan revision was needed; kept in the append-only history." },
        amendment: {
          type: "object",
          properties: { change: { type: "string" }, reasons: { type: "string" } },
        },
        exactNextAction: { type: "string" },
        claimComplete: { type: "boolean" },
        continueWork: { type: "boolean", description: "Return a validating quest to implementing to continue the work." },
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
