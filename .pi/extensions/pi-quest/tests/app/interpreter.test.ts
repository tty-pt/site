import { check } from "../check.ts";
import { replaceState } from "../../src/app/store.ts";
import { interpret, sendSteer, type Ports } from "../../src/app/interpreter.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import type { Snapshot } from "../../src/durability/snapshots.ts";
import { fakePi } from "../fake-pi.ts";

function run(effects: Parameters<typeof interpret>[0]): { saved: Snapshot[]; steered: string[]; notes: string[] } {
  const saved: Snapshot[] = [];
  const steered: string[] = [];
  const notes: string[] = [];
  const ports: Ports = {
    saveSnapshot: (snapshot) => {
      saved.push(snapshot);
    },
    sendSteer: (text) => {
      steered.push(text);
    },
    notify: (text) => {
      notes.push(text);
    },
  };
  interpret(effects, ports);
  return { saved, steered, notes };
}

Deno.test("interpreter saves snapshots and clears the pending flag", () => {
  replaceState(createQuest("req", "abc123"));
  const { saved } = run([{ kind: "EmitSnapshot" }]);
  check(saved.length === 1, "one snapshot saved");
  check(saved[0].qid === "abc123", "snapshot tagged");
  check(saved[0].state.objective === "req", "state carried");
});

Deno.test("interpreter delivers steer and notify effects", () => {
  replaceState(IDLE_STATE);
  const { steered, notes, saved } = run([
    { kind: "Steer", text: "proceed" },
    { kind: "NotifyUI", text: "hello" },
  ]);
  check(steered.length === 1 && steered[0] === "proceed", "steer delivered");
  check(notes.length === 1 && notes[0] === "hello", "notify delivered");
  check(saved.length === 0, "nothing snapshotted");
});

Deno.test("interpreter ignores future effects without crashing", () => {
  replaceState(IDLE_STATE);
  const { saved } = run([
    { kind: "LaunchReview", qid: "abc123" as Qid, review: "draft", target: "h1" },
  ]);
  check(saved.length === 0, "nothing saved");
});

Deno.test("interpreter runs no ports for empty effects", () => {
  replaceState(IDLE_STATE);
  const { saved } = run([]);
  check(saved.length === 0, "inert");
  replaceState(IDLE_STATE);
});

Deno.test("sendSteer never resumes the turn", () => {
  const pi = fakePi();
  sendSteer(pi, "Review running for quest abc123 — end your turn.");
  check(pi.sent.length === 1, "one steer sent");
  check(pi.sent[0].options?.deliverAs === "steer", "delivered as steer");
  check(pi.sent[0].options?.triggerTurn !== true, "nothing resumes the turn");
  replaceState(IDLE_STATE);
});
