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
  const ctx = fakeCtx(cwd, [], { input: async () => "green" });
  const withUI = { ...ctx, hasUI: true };
  const result = await askHumanTool(pi).execute("t1", { question: "Which color?", default: "blue" }, undefined, undefined, withUI);
  const text = result.content.map((c) => c.text ?? "").join("\n");
  check(text.includes('Human answered: "green"'), "user answer reported");
  const details = result.details as Record<string, unknown>;
  check(details["askingTool"] === "ask_user_question", "binding reported");
  check(details["askingAvailable"] === true, "live tool available");
  check(details["answer"] === "green" && details["source"] === "user", "answer details");
  replaceState(IDLE_STATE);
});

Deno.test("quest_ask_human honors the settings timeout without an explicit one", async () => {
  replaceState(createQuest("work", "abc123"));
  const cwd = await settingsCwd({ askTimeoutMs: 40, bindings: { asking: { tool: "ask_user_question" } } });
  const pi = fakePi();
  pi.toolNames = ["ask_user_question"];
  let captured = -1;
  const ctx = fakeCtx(cwd, [], {
    input: (_title, _placeholder, opts) => {
      captured = opts?.timeout ?? -1;
      return new Promise<never>(() => {});
    },
  });
  const withUI = { ...ctx, hasUI: true };
  const result = await askHumanTool(pi).execute("t2", { question: "Which color?", default: "blue" }, undefined, undefined, withUI);
  const text = result.content.map((c) => c.text ?? "").join("\n");
  check(text.includes('proceeding with default: "blue"'), "settings timeout lapses to default");
  check(captured === 40, "settings timeout forwarded");
  check(getState().humanAnswers.length === 1, "default recorded");
  replaceState(IDLE_STATE);
});
