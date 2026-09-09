import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { askingToolAvailable, askWithDefault, noteLateAnswer, pendingQuestion } from "../../src/absence/ask.ts";
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

Deno.test("ask without UI surfaces a steer that no human saw it", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  await askWithDefault(pi, fakeCtx(tmp()), { question: "Which color?", defaultAnswer: "blue" });
  const steers = pi.sent.map((s) => String(s.message.content).toLowerCase());
  check(steers.some((s) => s.includes("not shown to a human")), "steers that no human checkpoint occurred");
  check(steers.some((s) => s.includes("default")), "steers the applied default");
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

Deno.test("asking availability probes every known tool name", () => {
  const pi = fakePi();
  check(!askingToolAvailable(pi), "none registered");
  check(!askingToolAvailable(pi, "ask_questions"), "default binding misses");
  pi.toolNames = ["ask_user_question"];
  check(askingToolAvailable(pi), "live name found");
  check(askingToolAvailable(pi, "ask_questions"), "default binding resolves to live name");
  pi.toolNames = ["custom_ask"];
  check(!askingToolAvailable(pi), "unrelated tool ignored");
  check(askingToolAvailable(pi, "custom_ask"), "explicit binding honored");
});

Deno.test("ask with zero timeout never prompts", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  let prompted = false;
  const ctx = fakeCtx(tmp(), [], {
    input: async () => {
      prompted = true;
      return "green";
    },
  });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const result = await askWithDefault(pi, withUI, { question: "Q?", defaultAnswer: "blue", timeoutMs: 0 });
  check(result.answer === "blue" && result.source === "default", "zero timeout defaults");
  check(!prompted, "no prompt issued");
  replaceState(IDLE_STATE);
});

Deno.test("ask forwards the timeout to the UI prompt", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  let captured = -1;
  const ctx = fakeCtx(tmp(), [], {
    input: async (_title, _placeholder, opts) => {
      captured = opts?.timeout ?? -1;
      return "green";
    },
  });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const result = await askWithDefault(pi, withUI, { question: "Q?", defaultAnswer: "blue", timeoutMs: 5000 });
  check(result.answer === "green" && result.source === "user", "user answer wins");
  check(captured === 5000, "timeout forwarded");
  replaceState(IDLE_STATE);
});

Deno.test("ask waits indefinitely on negative timeout", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  let captured: number | undefined = -1;
  let release: (value: string | undefined) => void = () => {};
  let settled = false;
  const ctx = fakeCtx(tmp(), [], {
    input: (_title, _placeholder, opts) => {
      captured = opts?.timeout;
      return new Promise<string | undefined>((resolve) => {
        release = resolve;
      });
    },
  });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const pending = askWithDefault(pi, withUI, { question: "Q?", defaultAnswer: "blue", timeoutMs: -1 })
    .then((result) => {
      settled = true;
      return result;
    });
  await new Promise((resolve) => setTimeout(resolve, 30));
  check(!settled, "no timer fires");
  release("patient green");
  const result = await pending;
  check(settled && result.answer === "patient green" && result.source === "user", "late input wins");
  check(captured === undefined, "no timeout opt passed");
  replaceState(IDLE_STATE);
});

Deno.test("ask defaults when the prompt rejects or cancels", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const rejecting = fakeCtx(tmp(), [], { input: async () => { throw new Error("dismissed"); } });
  const cancelled = fakeCtx(tmp(), [], { input: async () => undefined });
  const failed = await askWithDefault(pi, { ...rejecting, hasUI: true }, { question: "Q?", defaultAnswer: "blue" });
  check(failed.answer === "blue" && failed.source === "default", "rejection defaults");
  const empty = await askWithDefault(pi, { ...cancelled, hasUI: true }, { question: "Q?", defaultAnswer: "blue" });
  check(empty.answer === "blue" && empty.source === "default", "cancellation defaults");
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

Deno.test("ask notifies user when no UI", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp());
  await askWithDefault(pi, ctx, { question: "Which color?", defaultAnswer: "blue" });
  check(ctx.notifications.calls.length === 1, "one notify call");
  check(ctx.notifications.calls[0].type === "warning", "warning type");
  check(ctx.notifications.calls[0].message.includes("no UI"), "no-UI message");
  check(ctx.notifications.calls[0].message.includes("Which color?"), "question text in notify");
  check(ctx.notifications.calls[0].message.includes("blue"), "default in notify");
  replaceState(IDLE_STATE);
});

Deno.test("ask notifies user on zero timeout", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp(), [], { input: async () => "green" });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  await askWithDefault(pi, withUI, { question: "Q?", defaultAnswer: "blue", timeoutMs: 0 });
  check(withUI.notifications.calls.length === 1, "one notify call");
  check(withUI.notifications.calls[0].type === "warning", "warning type");
  check(withUI.notifications.calls[0].message.includes("zero wait"), "zero-wait message");
  replaceState(IDLE_STATE);
});

Deno.test("ask notifies user on timeout fire", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp(), [], { input: () => new Promise<never>(() => {}) });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  await askWithDefault(pi, withUI, { question: "Which style?", defaultAnswer: "minimal", timeoutMs: 20 });
  check(withUI.notifications.calls.length === 1, "one notify call");
  check(withUI.notifications.calls[0].type === "warning", "warning type");
  check(withUI.notifications.calls[0].message.includes("timed out"), "timeout message");
  check(withUI.notifications.calls[0].message.includes("minimal"), "default in notify");
  replaceState(IDLE_STATE);
});
