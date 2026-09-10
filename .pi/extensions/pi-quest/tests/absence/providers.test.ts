import { check } from "../check.ts";
import { mkdtempSync } from "node:fs";
import { tmpdir } from "node:os";
import { join } from "node:path";
import { getState, replaceState } from "../../src/app/store.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { selectProvider, type QuestionProvider } from "../../src/absence/providers.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function tmp(): string {
  return mkdtempSync(join(tmpdir(), "pi-quest-providers-"));
}

function bridge(answer: string | null, available = true): QuestionProvider {
  return {
    name: "bridge",
    available: () => available,
    ask: async () => answer === null
      ? { answer: "unused", source: "default" }
      : { answer, source: "user" },
  };
}

Deno.test("selectProvider prefers an available bridge provider", async () => {
  const pi = fakePi();
  const provider = selectProvider(pi, [bridge("green")]);
  check(provider.name === "bridge", "bridge wins over input");
  const result = await provider.ask(pi, fakeCtx(tmp()), { question: "Q?", defaultAnswer: "blue" });
  check(result.answer === "green" && result.source === "user", "bridge answer flows through");
});

Deno.test("selectProvider skips unavailable bridges for input", () => {
  const pi = fakePi();
  const provider = selectProvider(pi, [bridge("green", false)]);
  check(provider.name === "input", "input fallback selected");
});

Deno.test("selectProvider survives a faulting available()", () => {
  const pi = fakePi();
  const faulty: QuestionProvider = {
    name: "faulty",
    available: () => { throw new Error("boom"); },
    ask: async () => ({ answer: "unused", source: "default" }),
  };
  const provider = selectProvider(pi, [faulty]);
  check(provider.name === "input", "fault falls through to input");
});

Deno.test("input provider returns the user's answer", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp(), [], { input: async () => "green" });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const provider = selectProvider(pi);
  check(provider.name === "input", "no bridges means input");
  const result = await provider.ask(pi, withUI, { question: "Which color?", defaultAnswer: "blue" });
  check(result.answer === "green" && result.source === "user", "user answer wins");
  check(getState().humanAnswers.length === 1, "answer recorded");
  replaceState(IDLE_STATE);
});

Deno.test("input provider defaults on timeout", async () => {
  replaceState(createQuest("work", "abc123"));
  const pi = fakePi();
  const ctx = fakeCtx(tmp(), [], { input: () => new Promise<never>(() => {}) });
  const withUI: typeof ctx = { ...ctx, hasUI: true };
  const result = await selectProvider(pi).ask(pi, withUI, {
    question: "Which color?",
    defaultAnswer: "blue",
    timeoutMs: 20,
  });
  check(result.answer === "blue" && result.source === "default", "timeout defaults");
  check(getState().humanAnswers.length === 1, "default recorded");
  replaceState(IDLE_STATE);
});
