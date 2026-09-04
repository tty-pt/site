import { check } from "../check.ts";
import { createQuest } from "../../src/domain/quest.ts";
import {
  decodeSnapshot,
  encodeSnapshot,
  MAX_SNAPSHOT_BYTES,
  newestSnapshot,
  reconstruct,
  SNAPSHOT_TYPE,
  SNAPSHOT_VERSION,
} from "../../src/durability/snapshots.ts";
import type { TranscriptEntry } from "../../src/hooks/events.ts";

function entry(data: unknown, customType = SNAPSHOT_TYPE): TranscriptEntry {
  return { customType, data };
}

Deno.test("snapshot codec round-trips state", () => {
  const state = createQuest("verbatim request", "abc123");
  const snap = encodeSnapshot(state, "2026-01-01T00:00:00.000Z");
  check(snap.v === SNAPSHOT_VERSION, "version stamped");
  check(snap.qid === "abc123", "qid tagged");
  const back = decodeSnapshot(snap);
  check(back !== null, "decodes");
  check(back?.objective === "verbatim request", "verbatim survives");
  check(back?.pendingRootRequest === "verbatim request", "request survives");
  check(back?.snapshotPending === true, "pending flag survives");
});

Deno.test("snapshot decoder skips corrupt entries", () => {
  check(decodeSnapshot("not json{{{") === null, "garbage skipped");
  check(decodeSnapshot(null) === null, "null skipped");
  check(decodeSnapshot({ v: 999, state: {} }) === null, "version mismatch skipped");
  check(decodeSnapshot({ v: 1, state: { phase: "nope" } }) === null, "bad phase skipped");
  check(decodeSnapshot(JSON.stringify({ v: 1, state: { phase: "idle" } })) !== null, "string json accepted");
});

Deno.test("snapshot encoder caps size by trimming lists, never scalars", () => {
  const big = "x".repeat(4096);
  const state = {
    ...createQuest("keep me verbatim", "abc123"),
    setbacks: Array.from({ length: 100 }, (_, i) => ({
      reason: `setback ${i}`,
      evidence: Array.from({ length: 10 }, () => big),
    })),
  };
  const snap = encodeSnapshot(state);
  const bytes = JSON.stringify(snap).length;
  check(bytes <= MAX_SNAPSHOT_BYTES, `capped (${bytes} bytes)`);
  check(snap.state.setbacks.length <= 20, "setbacks trimmed");
  check(snap.state.objective === "keep me verbatim", "verbatim scalar kept");
  check(snap.state.qid === "abc123", "identity kept");
});

Deno.test("snapshot decoder normalizes older shapes", () => {
  const legacy = {
    v: 1,
    qid: "abc123",
    savedAt: "2026-01-01T00:00:00.000Z",
    state: { phase: "implementing", qid: "abc123", objective: "old" },
  };
  const back = decodeSnapshot(legacy);
  check(back !== null, "legacy decodes");
  check(back?.depth === 0, "depth defaults");
  check(back?.refinements.length === 0, "refinements default");
  check(back?.humanAnswers.length === 0, "answers default");
  check(back?.reviewDialogue.length === 0, "dialogue defaults");
  check(back?.objective === "old", "kept fields survive");
});

Deno.test("reconstruct takes the newest decodable snapshot", () => {
  const first = encodeSnapshot(createQuest("first", "abc123"));
  const second = encodeSnapshot({ ...createQuest("second", "abc123"), exactNextAction: "newer" });
  const entries = [
    entry({ note: "other extension" }, "other_type"),
    entry(first),
    entry("corrupt{{{"),
    entry(second),
  ];
  const state = reconstruct(entries);
  check(state.exactNextAction === "newer", "newest wins, corrupt skipped");
});

Deno.test("reconstruct cold-starts when no snapshot exists", () => {
  const state = reconstruct([entry({ note: "x" }, "other_type")]);
  check(state.phase === "idle", "cold start idle");
  check(state.qid === null, "no qid");
  check(newestSnapshot([]) === null, "empty branch null");
});
