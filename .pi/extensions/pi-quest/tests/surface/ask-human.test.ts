import { check } from "../check.ts";
import { getState, replaceState } from "../../src/app/store.ts";
import { createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import { askHumanTool } from "../../src/surface/tools/ask-human.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

async function settingsCwd(settings: unknown): Promise<string> {
  const dir = await Deno.makeTempDir();
  await Deno.mkdir(`${dir}/.pi`, { recursive: true });
  await Deno.writeTextFile(`${dir}/.pi/settings.json`, JSON.stringify({ "pi-quest": settings }));
  return dir;
}

Deno.test("quest_ask_human reports the live asking tool as available", async () => {
  replaceState(createQuest("work", "abc123"));
  const cwd = await settingsCwd({ bindings: { asking: { tool: "ask_user_question" } } });
  const pi = fakePi();
  pi.toolNames = ["ask_user_question"];
  pi.executeToolHandler = async (name, _params, _signal) => {
    if (name === "ask_user_question") return { content: [{ type: "text", text: "green" }] };
    return { content: [{ type: "text", text: "unknown" }] };
  };
  const ctx = fakeCtx(cwd);
  const result = await askHumanTool(pi).execute("t1", { question: "Which color?", default: "blue" }, undefined, undefined, ctx);
  const text = result.content.map((c) => c.text ?? "").join("\n");
  check(text.includes('Human answered: "green"'), "user answer reported");
  const details = result.details as Record<string, unknown>;
  check(details["askingTool"] === "ask_user_question", "binding reported");
  check(details["askingAvailable"] === true, "live tool available");
  check(details["answer"] === "green" && details["source"] === "user", "answer details");
  check(details["uiPresent"] === false, "ui presence reported false without a UI");
  replaceState(IDLE_STATE);
});

Deno.test("quest_ask_human reports absence and no UI when headless", async () => {
  replaceState(createQuest("work", "abc123"));
  const cwd = await settingsCwd({});
  const pi = fakePi();
  const ctx = fakeCtx(cwd);
  const result = await askHumanTool(pi).execute("t3", { question: "Which color?", default: "blue" }, undefined, undefined, ctx);
  const details = result.details as Record<string, unknown>;
  check(details["source"] === "default", "no UI defaults");
  check(details["uiPresent"] === false, "ui presence reported false without a UI");
  const text = result.content.map((c) => c.text ?? "").join("\n");
  check(text.includes("absence"), "absence surfaced in text");
  replaceState(IDLE_STATE);
});

Deno.test("quest_ask_human honors the settings timeout without an explicit one", async () => {
  replaceState(createQuest("work", "abc123"));
  const cwd = await settingsCwd({ askTimeoutMs: 50, bindings: { asking: { tool: "ask_user_question" } } });
  const pi = fakePi();
  pi.toolNames = ["ask_user_question"];
  let abortCalled = false;
  pi.executeToolHandler = async (name, _params, signal) => {
    if (name === "ask_user_question" && signal) {
      await new Promise((resolve) => {
        signal.addEventListener("abort", () => resolve(undefined));
        setTimeout(() => resolve(undefined), 10000);
      });
      if (signal.aborted) abortCalled = true;
      throw new Error("aborted");
    }
    return { content: [{ type: "text", text: "unknown" }] };
  };
  const ctx = fakeCtx(cwd);
  const result = await askHumanTool(pi).execute("t2", { question: "Which color?", default: "blue" }, undefined, undefined, ctx);
  const text = result.content.map((c) => c.text ?? "").join("\n");
  check(text.includes('proceeding with default: "blue"'), "settings timeout lapses to default");
  check(abortCalled, "abort signal fired");
  check(getState().humanAnswers.length === 1, "default recorded");
  replaceState(IDLE_STATE);
});
