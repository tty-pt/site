import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { askWithDefault, noteLateAnswer, pendingQuestion } from "../../src/absence/ask.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-ask-"));
}

Deno.test("ask defaults immediately without UI", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const result = await askWithDefault(pi, fakeCtx(tmp()), { question: "Which color?", defaultAnswer: "blue" });
  check(result.answer === "blue" && result.source === "default", "default on no UI");
  check(getState().humanAnswers.length === 1, "answer recorded");
  check(getState().humanAnswers[0].late === false, "on-time");
  replaceState(IDLE_STATE);
});

Deno.test("ask takes the user's answer when given", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp(), [], { input: async () => "green" });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const result = await askWithDefault(pi, withUI, { question: "Which color?", defaultAnswer: "blue" });
  check(result.answer === "green" && result.source === "user", "user answer wins");
  replaceState(IDLE_STATE);
});

Deno.test("ask times out to the default", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp(), [], { input: () => new Promise<never>(() => {}) });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const result = await askWithDefault(pi, withUI, { question: "Which color?", defaultAnswer: "blue", timeoutMs: 20 });
  check(result.answer === "blue" && result.source === "default", "timeout defaults");
  replaceState(IDLE_STATE);
});

Deno.test("late answers apply as refinements with guards", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  await askWithDefault(pi, fakeCtx(tmp()), { question: "Which color?", defaultAnswer: "blue" });
  check(pendingQuestion("abc123") !== undefined, "question remembered");
  check(noteLateAnswer(pi, "actually red"), "late answer applies");
  const answers = getState().humanAnswers;
  check(answers.length === 2 && answers[1].late && answers[1].answer === "actually red", "late recorded");
  check(!noteLateAnswer(pi, "/quests"), "commands ignored");
  check(!noteLateAnswer(pi, "go"), "approvals ignored");
  check(!noteLateAnswer(pi, "x".repeat(600)), "essays ignored");
  replaceState(IDLE_STATE);
});
