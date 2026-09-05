import { check } from "../check.ts";
import { replaceState } from "../../src/app/store.ts";
import { injectQuestContext } from "../../src/durability/injection.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Pi } from "../../src/hooks/events.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

Deno.test("injection names the draft file while drafting", () => {
  const handlers = new Map<string, (event: unknown, ctx: unknown) => unknown>();
  const pi = fakePi();
  (pi as { on: unknown }).on = (event: string, handler: unknown) => {
    handlers.set(event, handler as (event: unknown, ctx: unknown) => unknown);
  };
  injectQuestContext(pi as unknown as Pi);
  const drafting = createDraft(createQuest("work", "abc123"), "work");
  replaceState(drafting);
  const injected = handlers.get("before_agent_start")!({}, fakeCtx("/tmp")) as
    | { message?: { content?: unknown } }
    | undefined;
  const content = String(injected?.message?.content ?? "");
  check(content.includes("future/abc123.md"), "draft path injected");
  check(content.includes("drafting"), "phase injected");
  replaceState(IDLE_STATE);
  const idle = handlers.get("before_agent_start")!({}, fakeCtx("/tmp"));
  check(idle === undefined, "idle injects nothing");
});
