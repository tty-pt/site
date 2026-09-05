// HIGH_LEVEL: #surviving
// HIGH_LEVEL: #durability
// HIGH_LEVEL: #storage
// SPEC: B1.6, snapshot contract §4.1.
import { IDLE_STATE, type QuestState } from "../domain/quest";
import type { TranscriptEntry } from "../hooks/events";

export const SNAPSHOT_TYPE = "quest_journal";
export const SNAPSHOT_VERSION = 1;
export const MAX_SNAPSHOT_BYTES = 1048576;

export interface Snapshot {
  v: number;
  qid: string | null;
  savedAt: string;
  state: QuestState;
}

function shrinkLists(state: QuestState): QuestState {
  if (state.setbacks.length > 0) {
    const keep = state.setbacks.slice(-20).map((s) =>
      s.evidence.length > 5 ? { ...s, evidence: s.evidence.slice(-5) } : s
    );
    return { ...state, setbacks: keep };
  }
  if (state.amendments.length > 20) return { ...state, amendments: state.amendments.slice(-20) };
  if (state.children.length > 50) return { ...state, children: state.children.slice(-50) };
  return state;
}

function pack(state: QuestState, savedAt: string): string {
  return JSON.stringify({ v: SNAPSHOT_VERSION, qid: state.qid, savedAt, state });
}

export function encodeSnapshot(state: QuestState, savedAt: string = new Date().toISOString()): Snapshot {
  let candidate = state;
  let guard = 0;
  while (pack(candidate, savedAt).length > MAX_SNAPSHOT_BYTES && guard < 10) {
    const shrunk = shrinkLists(candidate);
    if (shrunk === candidate) break;
    candidate = shrunk;
    guard += 1;
  }
  if (pack(candidate, savedAt).length > MAX_SNAPSHOT_BYTES) {
    throw new Error("snapshot exceeds cap after truncation");
  }
  return { v: SNAPSHOT_VERSION, qid: state.qid, savedAt, state: candidate };
}

const PHASES = ["idle", "provisional", "drafting", "implementing", "validating", "archived"];

export function normalizeState(raw: Record<string, unknown>): QuestState {
  const base = IDLE_STATE;
  const get = <K extends keyof QuestState>(key: K, fallback: QuestState[K]): QuestState[K] => {
    const value = raw[key as string];
    return (value === undefined ? fallback : value) as QuestState[K];
  };
  return {
    ...base,
    ...raw,
    phase: PHASES.includes(raw["phase"] as string) ? (raw["phase"] as QuestState["phase"]) : "idle",
    qid: typeof raw["qid"] === "string" ? (raw["qid"] as QuestState["qid"]) : null,
    parentQid: typeof raw["parentQid"] === "string" ? (raw["parentQid"] as QuestState["parentQid"]) : null,
    depth: typeof raw["depth"] === "number" ? (raw["depth"] as number) : 0,
    refinements: Array.isArray(raw["refinements"]) ? (raw["refinements"] as string[]) : [],
    humanAnswers: Array.isArray(raw["humanAnswers"]) ? (raw["humanAnswers"] as QuestState["humanAnswers"]) : [],
    setbacks: Array.isArray(raw["setbacks"]) ? (raw["setbacks"] as QuestState["setbacks"]) : [],
    amendments: Array.isArray(raw["amendments"]) ? (raw["amendments"] as QuestState["amendments"]) : [],
    children: Array.isArray(raw["children"])
      ? (raw["children"] as Array<Record<string, unknown>>).map((c) => ({
        qid: c["qid"],
        brief: c["brief"],
        status: c["status"],
        findings: c["findings"] ?? null,
        acknowledged: c["acknowledged"] ?? false,
      })) as QuestState["children"]
      : [],
    reviewDialogue: Array.isArray(raw["reviewDialogue"]) ? (raw["reviewDialogue"] as QuestState["reviewDialogue"]) : [],
    snapshotPending: get("snapshotPending", false),
  };
}

export function decodeSnapshot(raw: unknown): QuestState | null {
  try {
    const value = typeof raw === "string" ? JSON.parse(raw) : raw;
    if (typeof value !== "object" || value === null) return null;
    const record = value as Record<string, unknown>;
    if (record["v"] !== SNAPSHOT_VERSION) return null;
    const state = record["state"];
    if (typeof state !== "object" || state === null) return null;
    if (!PHASES.includes((state as Record<string, unknown>)["phase"] as string)) return null;
    return normalizeState(state as Record<string, unknown>);
  } catch {
    return null;
  }
}

export function newestSnapshot(entries: readonly TranscriptEntry[]): QuestState | null {
  for (let i = entries.length - 1; i >= 0; i -= 1) {
    const entry = entries[i];
    if (entry.customType !== SNAPSHOT_TYPE) continue;
    const state = decodeSnapshot(entry.data);
    if (state !== null) return state;
  }
  return null;
}

export function reconstruct(entries: readonly TranscriptEntry[]): QuestState {
  return newestSnapshot(entries) ?? IDLE_STATE;
}

export function newestSnapshotFor(entries: readonly TranscriptEntry[], qid: string): QuestState | null {
  for (let i = entries.length - 1; i >= 0; i -= 1) {
    const entry = entries[i];
    if (entry.customType !== SNAPSHOT_TYPE) continue;
    const state = decodeSnapshot(entry.data);
    if (state !== null && state.qid === qid) return state;
  }
  return null;
}
