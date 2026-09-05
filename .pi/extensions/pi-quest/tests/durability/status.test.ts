import { check } from "../check.ts";
import { IDLE_STATE } from "../../src/domain/quest.ts";
import type { Qid } from "../../src/domain/qid.ts";
import { questStatus } from "../../src/durability/index.ts";

Deno.test("status shows one icon per phase", () => {
  check(questStatus(IDLE_STATE) === undefined, "idle quest is silent");
  const cases = [
    ["provisional", "🔍"],
    ["drafting", "📝"],
    ["implementing", "🛠️"],
    ["validating", "🧪"],
    ["archived", "📦"],
  ] as const;
  for (const [phase, icon] of cases) {
    const text = questStatus({ ...IDLE_STATE, phase, qid: "abc123" as Qid });
    check(text === `${icon} abc123`, `${phase} maps to ${icon} without the phase word`);
  }
  check(questStatus({ ...IDLE_STATE, phase: "idle", qid: null }) === undefined, "no qid stays silent");
});

Deno.test("status text mode puts the phase where the icon goes", () => {
  const text = questStatus({ ...IDLE_STATE, phase: "drafting", qid: "abc123" as Qid }, "text");
  check(text === "drafting abc123", "phase word plus qid");
  check(questStatus(IDLE_STATE, "text") === undefined, "text mode silent without quest");
});
