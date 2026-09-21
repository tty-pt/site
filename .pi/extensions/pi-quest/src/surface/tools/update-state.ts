// HIGH_LEVEL: #tools (main agent) — quest_update_state.
// HIGH_LEVEL: #plan revision — planRevision stages a re-reviewable revision. The agent's write path: findings, drafts, amendments, claims.
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import { getState, replaceState, updateState } from "../../app/store";
import { emitNow, sendSteer } from "../../app/interpreter";
import {
  createQuest,
  recordAmendment,
  recordRefinement,
  type QuestState,
} from "../../domain/quest";
import { claimComplete, createDraft, demoteToDrafting, demoteToImplementing } from "../../domain/transitions";
import { acknowledgeChild, unfinishedChildren } from "../../domain/children";
import { nextQid } from "../../domain/qid";
import { draftPath } from "../../domain/paths";
import { type Qid } from "../../domain/qid";
import type { Pi, PiCtx, PiToolSpec } from "../../hooks/events";
import { ensureValidationFlow } from "../../validation/flow";
import { checkAnalysisParam, checkPlanParam, profileForSavedDoc } from "./draft-profile";
import { checkPlanCitations } from "./claims";
import { carryRefinementsToDraft, writeAnalysisToDraft, writePlanRevision, writePlanToDraft } from "./draft-writer";
import { setDocStatus } from "../../quest-doc";
import { noteDraftUpdated } from "../../durability/status";
import { ensureDraftFile, listKnownQids } from "../../files";
import { textResult } from "./reply";

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
  // The claim is unverified until the validator passes: the doc tracks the
  // phase but never marks completion as true on the agent's say-so.
  await setDocStatus(ctx, qid, "validating", false);
  emitNow(pi);
  sendSteer(pi, `Quest ${claimed.qid} claimed complete. Validator booting against the approved plan.`);
  void ensureValidationFlow(pi, ctx);
  return getState();
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
  const kind = params["kind"] === "analysis" ? "analysis" : "standard";
  const analysis = kind === "analysis";
  const thin = state.refinements.length === 0 &&
    ((typeof plan !== "string" || plan.trim() === "") && !analysis);
  state = createDraft(state, name, kind);
  replaceState(state);
  await ensureDraftFile(ctx, qid, name, state.objective);
  state = await carryRefinementsToDraft(ctx, getState());
  applied.push(`draft ${name} created at ${draftPath(qid)} — edit ONLY this file`);
  if (thin) {
    applied.push("draft created thin — no findings recorded yet; file them via refinement or author the plan via {plan:}. A draft becomes reviewable at the maturity bar: 2 requirements, or 1 requirement + 7 evidence, with an actionable plan. Use {checkPlan: \"<plan body>\"} to preview the profile without booting a review.");
  }
  if (analysis) {
    applied.push("analysis quest — author the deliverable via {analysis: \"<body>\"} or by editing the ## Analysis section, and use {checkAnalysis: \"<body>\"} to preview the profile without booting a review.");
  }
  noteDraftUpdated(ctx);
  return state;
}

async function applyPlanParam(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  params: Record<string, unknown>,
  applied: string[],
): Promise<{ state: QuestState; error?: string }> {
  const plan = params["plan"];
  const planQid = state.qid;
  if (typeof plan !== "string" || plan.trim() === "" || planQid === null) return { state };
  try {
    const next = await writePlanToDraft(pi, ctx, state, plan.trim());
    applied.push("plan recorded in the draft file");
    const claims = await checkPlanCitations(ctx, plan.trim());
    if (claims !== "") applied.push(claims);
    const docAfter = await readFile(join(ctx.cwd, draftPath(planQid)), "utf8");
    applied.push(await profileForSavedDoc(ctx, docAfter, state.kind));
    return { state: next };
  } catch (err) {
    return { state, error: err instanceof Error ? err.message : String(err) };
  }
}

async function applyAnalysisParam(
  pi: Pi,
  ctx: PiCtx,
  state: QuestState,
  params: Record<string, unknown>,
  applied: string[],
): Promise<{ state: QuestState; error?: string }> {
  const analysis = params["analysis"];
  const qid = state.qid;
  if (typeof analysis !== "string" || analysis.trim() === "" || qid === null) return { state };
  try {
    const next = await writeAnalysisToDraft(pi, ctx, state, analysis.trim());
    applied.push("analysis recorded in the draft file");
    const claims = await checkPlanCitations(ctx, analysis.trim());
    if (claims !== "") applied.push(claims);
    const docAfter = await readFile(join(ctx.cwd, draftPath(qid)), "utf8");
    applied.push(await profileForSavedDoc(ctx, docAfter, state.kind));
    return { state: next };
  } catch (err) {
    return { state, error: err instanceof Error ? err.message : String(err) };
  }
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
    const claims = await checkPlanCitations(ctx, revision.trim());
    if (claims !== "") applied.push(claims);
    return { state: next };
  } catch (err) {
    return { state, error: err instanceof Error ? err.message : String(err) };
  }
}

// A validating quest with no validator goes back to work instead of stalling; the in-file ## Status marker resets to stay honest.
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
    if (state.kind === "analysis") {
      // An analysis quest cannot implement: it has no implementation plan, so
      // returning to work means revising the analysis deliverable (D1/D3).
      const next = updateState((s) => demoteToDrafting(s));
      await setDocStatus(ctx, qid, "drafting", false);
      applied.push("returned to drafting to revise the delivered analysis");
      return { state: next };
    }
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
  const kind = params["kind"];
  if (kind !== undefined && kind !== "standard" && kind !== "analysis") {
    return { applied, error: `unknown quest kind ${JSON.stringify(kind)} — expected "standard" or "analysis"` };
  }
  // checkPlan/checkAnalysis: pure read-only probes (see draft-profile.ts) —
  // before any create path so they never side-create.
  const probe = (await Promise.all([checkPlanParam(ctx, params), checkAnalysisParam(ctx, params)])).find((p) => p.applied.length > 0 || p.error !== undefined);
  if (probe !== undefined) return { applied: probe.applied, error: probe.error };
  if (state.qid === null) {
    const objective = params["objective"];
    if (typeof objective !== "string" || objective.trim() === "") {
      return { applied, error: "no active quest; pass objective to create one" };
    }
    const qid = await provisionRootQuest(ctx, objective.trim());
    state = getState();
    applied.push(`created quest ${qid}`);
  }
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
    state = updateState((s) => ({ ...s, exactNextAction: (next as string).trim(), snapshotPending: true }));
    applied.push("next action updated");
  }
  const planParam = await applyPlanParam(pi, ctx, state, params, applied);
  if (planParam.error !== undefined) return { applied, error: planParam.error };
  state = planParam.state;
  const analysisParam = await applyAnalysisParam(pi, ctx, state, params, applied);
  if (analysisParam.error !== undefined) return { applied, error: analysisParam.error };
  state = analysisParam.state;
  const revised = await applyPlanRevisionParam(pi, ctx, state, params, applied);
  if (revised.error !== undefined) return { applied, error: revised.error };
  state = revised.state;
  const continuePast = params["continuePast"];
  if (typeof continuePast === "string" && continuePast.trim() !== "" && state.qid) {
    try {
      state = updateState((s) => acknowledgeChild(s, continuePast.trim() as Qid));
      applied.push(`continued past child ${continuePast.trim()}`);
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
    description: "Record findings, drafts, amendments, and state. The agent's write path to the quest: pass objective to create, kind with \"analysis\" to mark a purely-analytical quest (deliverable is the ## Analysis; no implementation phase), draftName to draft, refinement/amendment/exactNextAction to record, plan to author the draft Implementation Plan section directly (drafting only, standard quests), analysis to author the draft Analysis section directly (drafting only, analysis quests), planRevision with an optional note to revise the approved plan mid-implementation (boots a re-review), checkPlan to preview the maturity profile of a would-be plan WITHOUT writing or booting a review (drafting only), checkAnalysis likewise for a would-be analysis, claimComplete to finish, continueWork to return a validating quest to a work phase.",
    parameters: {
      type: "object",
      properties: {
        objective: { type: "string" },
        kind: { type: "string", description: "Quest kind on creation: \"standard\" (default) or \"analysis\"." },
        draftName: { type: "string" },
        refinement: { type: "string" },
        plan: { type: "string", description: "Implementation Plan body, spliced into the draft file (drafting only, standard quests)." },
        analysis: { type: "string", description: "Analysis body, spliced into the draft file (drafting only, analysis quests)." },
        checkPlan: { type: "string", description: "Preview the draft profile (requirements/evidence counts, maturity-bar verdict, citation resolution) for a would-be plan body — read-only, no save, no review boot (drafting only)." },
        checkAnalysis: { type: "string", description: "Preview the draft profile for a would-be analysis body — read-only, no save, no review boot (drafting only, analysis quests)." },
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
