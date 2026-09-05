// HIGH_LEVEL: #commands — bare /quest picks from current quests when idle.
// HIGH_LEVEL: #quest creation — one active per session; the picker sets it.
import { emitNow } from "../../app/interpreter";
import { getState, replaceState } from "../../app/store";
import type { QuestState } from "../../domain/quest";
import { scanSiblingStates } from "../../durability/siblings";
import { newestSnapshotPerQid } from "../../durability/snapshots";
import type { Pi, PiCtx } from "../../hooks/events";
import { listDraftQids, readDraftObjective, resumeQuest, summarizeActive } from "./quest";

export interface QuestCandidate {
  qid: string;
  label: string;
  state: QuestState | null;
}

const LABEL_MAX = 80;

function label(qid: string, phase: string, name: string): string {
  const brief = name.trim().split("\n")[0].slice(0, 48);
  return `${qid} (${phase}) ${brief}`.slice(0, LABEL_MAX);
}

function candidateLabel(qid: string, phase: string, state: QuestState): string {
  return label(qid, phase, state.objective || state.name || qid);
}

export async function collectCandidates(ctx: PiCtx, sessionsDir?: string): Promise<QuestCandidate[]> {
  const found = new Map<string, QuestCandidate>();
  for (const [qid, state] of newestSnapshotPerQid(ctx.sessionManager.getEntries())) {
    found.set(qid, { qid, label: candidateLabel(qid, state.phase, state), state });
  }
  for (const [qid, state] of await scanSiblingStates(sessionsDir)) {
    if (!found.has(qid)) found.set(qid, { qid, label: candidateLabel(qid, state.phase, state), state });
  }
  for (const qid of await listDraftQids(ctx.cwd)) {
    if (found.has(qid)) continue;
    const objective = await readDraftObjective(ctx.cwd, qid);
    found.set(qid, { qid, label: label(qid, "draft", objective ?? qid), state: null });
  }
  return [...found.values()].sort((a, b) => (a.qid < b.qid ? -1 : 1));
}

async function adoptCandidate(pi: Pi, ctx: PiCtx, candidate: QuestCandidate): Promise<string> {
  if (candidate.state !== null) {
    replaceState({ ...candidate.state, snapshotPending: false });
    emitNow(pi);
    return `Resumed quest ${candidate.qid}: ${summarizeActive()}`;
  }
  return resumeQuest(pi, ctx, candidate.qid);
}

function renderList(candidates: QuestCandidate[]): string {
  return candidates.map((c) => `  ${c.label}`).join("\n");
}

export async function pickQuest(pi: Pi, ctx: PiCtx, sessionsDir?: string): Promise<string> {
  if (getState().qid !== null) return summarizeActive();
  const candidates = await collectCandidates(ctx, sessionsDir);
  if (candidates.length === 0) {
    return "No active quest. Describe a request to start one, or /quest <qid> to resume a known quest.";
  }
  if (candidates.length === 1) return adoptCandidate(pi, ctx, candidates[0]);
  if (!ctx.hasUI) {
    return `No active quest. Known quests:\n${renderList(candidates)}\nResume with /quest <qid>.`;
  }
  let picked: string | undefined;
  try {
    picked = await ctx.ui.select("Select quest", candidates.map((c) => c.label));
  } catch {
    picked = undefined;
  }
  if (picked === undefined) {
    return `Selection cancelled. Known quests:\n${renderList(candidates)}\nResume with /quest <qid>.`;
  }
  const hit = candidates.find((c) => c.label === picked);
  if (hit === undefined) return "Unknown selection. Resume with /quest <qid>.";
  return adoptCandidate(pi, ctx, hit);
}
