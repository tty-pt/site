import { check } from "../check.ts";
import { reduce } from "../../src/domain/effects.ts";
import { IDLE_STATE } from "../../src/domain/quest.ts";

Deno.test("reduce emits a snapshot on turn end when pending", () => {
  const dirty = { ...IDLE_STATE, snapshotPending: true };
  const out = reduce(dirty, { type: "TurnEnded" });
  check(out.effects.length === 1 && out.effects[0].kind === "EmitSnapshot", "one emit");
  check(out.state.snapshotPending, "reduce is pure: flag cleared by interpreter");
});

Deno.test("reduce stays quiet without pending changes", () => {
  check(reduce(IDLE_STATE, { type: "TurnEnded" }).effects.length === 0, "clean turn quiet");
  check(reduce(IDLE_STATE, { type: "StateMutated" }).effects.length === 0, "clean mutation quiet");
});

Deno.test("reduce emits on mutation and ignores session boundaries", () => {
  const dirty = { ...IDLE_STATE, snapshotPending: true };
  const out = reduce(dirty, { type: "StateMutated" });
  check(out.effects.length === 1, "mutation emits");
  check(reduce(dirty, { type: "SessionStarted" }).effects.length === 0, "session start quiet");
  check(reduce(dirty, { type: "TurnStarted" }).effects.length === 0, "turn start quiet");
});
