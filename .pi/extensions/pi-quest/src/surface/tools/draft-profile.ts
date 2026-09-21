// HIGH_LEVEL: #tools (main agent) — checkPlan/checkAnalysis report the
// deterministic draft profile without writing or booting a review, so the
// agent builds up to the maturity bar before paying for a reviewer run.
import { readFile } from "node:fs/promises";
import { join } from "node:path";
import type { PiCtx } from "../../hooks/events";
import type { QuestKind, QuestState } from "../../domain/quest";
import { draftPath } from "../../domain/paths";
import { getState } from "../../app/store";
import { readQuestConfig } from "../../config";
import { draftProfileText, parseDraftSections, spliceAnalysisSection, splicePlanSection } from "../../drafting/plan-text";
import { checkPlanCitations } from "./claims";

async function deliverableProfile(
  ctx: PiCtx,
  state: QuestState,
  splice: (text: string, body: string) => string,
  body: string,
  kind: QuestKind,
  param: string,
): Promise<{ text?: string; error?: string }> {
  if (state.phase !== "drafting" || state.draft === null || state.qid === null) {
    return { error: `${param} needs a draft in drafting (phase ${state.phase}); author one via draftName first` };
  }
  const path = join(ctx.cwd, draftPath(state.qid));
  let current: string;
  try {
    current = await readFile(path, "utf8");
  } catch {
    return { error: `draft file missing at ${draftPath(state.qid)}` };
  }
  // Splice the would-be body onto the current draft so the profile reflects
  // exactly what a save would review.
  const updated = splice(current, body);
  const sections = parseDraftSections(updated);
  const config = await readQuestConfig(ctx.cwd);
  const claims = await checkPlanCitations(ctx, body);
  return { text: draftProfileText(sections, config.draftThresholds, claims, kind) };
}

export async function applyPlanCheck(
  ctx: PiCtx,
  state: QuestState,
  planBody: string,
): Promise<{ text?: string; error?: string }> {
  return deliverableProfile(ctx, state, splicePlanSection, planBody, "standard", "checkPlan");
}

export async function applyAnalysisCheck(
  ctx: PiCtx,
  state: QuestState,
  analysisBody: string,
): Promise<{ text?: string; error?: string }> {
  return deliverableProfile(ctx, state, spliceAnalysisSection, analysisBody, "analysis", "checkAnalysis");
}

export async function profileForSavedDoc(ctx: PiCtx, docText: string, kind: QuestKind = "standard"): Promise<string> {
  const config = await readQuestConfig(ctx.cwd);
  return draftProfileText(parseDraftSections(docText), config.draftThresholds, "", kind);
}

// The quest_update_state { checkPlan } parameter: a pure read-only probe that
// never writes or boots a review and never creates a quest.
export async function checkPlanParam(
  ctx: PiCtx,
  params: Record<string, unknown>,
): Promise<{ applied: string[]; error?: string }> {
  const body = params["checkPlan"];
  if (typeof body !== "string" || body.trim() === "") return { applied: [] };
  const check = await applyPlanCheck(ctx, getState(), body.trim());
  if (check.error !== undefined) return { applied: [], error: check.error };
  return { applied: [check.text ?? ""] };
}

export async function checkAnalysisParam(
  ctx: PiCtx,
  params: Record<string, unknown>,
): Promise<{ applied: string[]; error?: string }> {
  const body = params["checkAnalysis"];
  if (typeof body !== "string" || body.trim() === "") return { applied: [] };
  const check = await applyAnalysisCheck(ctx, getState(), body.trim());
  if (check.error !== undefined) return { applied: [], error: check.error };
  return { applied: [check.text ?? ""] };
}