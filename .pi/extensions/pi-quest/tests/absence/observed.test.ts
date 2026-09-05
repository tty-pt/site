import { check } from "../check.ts";
import { replaceState } from "../../src/app/store.ts";
import { installObservedQuestions, refreshAskingTools } from "../../src/absence/observed.ts";
import { DEFAULT_CONFIG } from "../../src/config.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Pi, ToolCallEvent, ToolResultEvent } from "../../src/hooks/events.ts";
import { getState } from "../../src/app/store.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

interface Captured {
  calls: Array<(event: ToolCallEvent) => unknown>;
  results: Array<(event: ToolResultEvent) => unknown>;
}

function wire(): { pi: Pi; captured: Captured } {
  const pi = fakePi();
  const captured: Captured = { calls: [], results: [] };
  const calls: Array<{ event: string; handler: (e: never, ctx: never) => unknown }> = [];
  (pi as { on: unknown }).on = (event: string, handler: (e: never, ctx: never) => unknown) => {
    calls.push({ event, handler });
  };
  installObservedQuestions(pi as unknown as Pi);
  const ctx = fakeCtx("/tmp");
  for (const sub of calls) {
    if (sub.event === "tool_call") captured.calls.push((e) => sub.handler(e as never, ctx as never));
    if (sub.event === "tool_result") captured.results.push((e) => sub.handler(e as never, ctx as never));
  }
  return { pi: pi as unknown as Pi, captured };
}

function call(toolName: string, toolCallId: string, input: Record<string, unknown>): ToolCallEvent {
  return { type: "tool_call", toolCallId, toolName, input };
}

function result(toolName: string, toolCallId: string, input: Record<string, unknown>, content: string, details?: unknown): ToolResultEvent {
  return {
    type: "tool_result",
    toolCallId,
    toolName,
    input,
    content: [{ type: "text", text: content }],
    isError: false,
    details,
  };
}

Deno.test("observed direct asks land in history", () => {
  replaceState(createQuest("work", "abc123"));
  refreshAskingTools(DEFAULT_CONFIG);
  const { captured } = wire();
  captured.calls[0](call("ask_user_question", "c1", {
    questions: [{ header: "Phase D", question: "Pick the fix:", options: [] }],
  }));
  captured.results[0](result("ask_user_question", "c1", {}, "User has answered...", {
    answers: [{ question: "Pick the fix:", answer: "(a) enum" }],
    cancelled: false,
  }));
  const answers = getState().humanAnswers;
  check(answers.length === 1, "answer recorded");
  check(answers[0].answer.includes("(a) enum"), "precise answer kept");
  check(answers[0].late === false, "direct answers are on time");
  replaceState(IDLE_STATE);
});

Deno.test("observed asks handle the flat shape and cancellations", () => {
  replaceState(createQuest("work", "abc123"));
  refreshAskingTools(DEFAULT_CONFIG);
  const { captured } = wire();
  captured.calls[0](call("ask_questions", "c2", { question: "Proceed?" }));
  captured.results[0](result("ask_questions", "c2", {}, "user said yes"));
  check(getState().humanAnswers.length === 1, "flat shape recorded");
  captured.calls[0](call("ask_user_question", "c3", { questions: [{ header: "H", question: "Q?" }] }));
  captured.results[0](result("ask_user_question", "c3", {}, "dismissed", { cancelled: true }));
  check(getState().humanAnswers.length === 1, "cancelled records nothing");
  captured.calls[0](call("bash", "c4", { command: "ls" }));
  captured.results[0](result("bash", "c4", {}, "files"));
  check(getState().humanAnswers.length === 1, "other tools ignored");
  replaceState(IDLE_STATE);
});

Deno.test("observed asks honor the configured binding", () => {
  replaceState(createQuest("work", "abc123"));
  refreshAskingTools({ ...DEFAULT_CONFIG, bindings: { asking: { tool: "custom_ask" }, reviewRunner: { tool: "subagent" } } });
  const { captured } = wire();
  captured.calls[0](call("custom_ask", "c5", { question: "Custom?" }));
  captured.results[0](result("custom_ask", "c5", {}, "custom yes"));
  check(getState().humanAnswers.length === 1, "configured tool observed");
  refreshAskingTools(DEFAULT_CONFIG);
  replaceState(IDLE_STATE);
});
