import { check } from "../check.ts";
import { replaceState } from "../../src/app/store.ts";
import { installDraftGate } from "../../src/drafting/gate.ts";
import { createDraft, createQuest, IDLE_STATE } from "../../src/domain/quest.ts";
import type { Pi, ToolCallEvent } from "../../src/hooks/events.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

type Handler = (event: ToolCallEvent, ctx: unknown) => { block?: boolean; reason?: string; terminate?: boolean } | undefined;

function captureGate(): { pi: Pi; handler: () => Handler } {
  const pi = fakePi();
  let captured: Handler | undefined;
  (pi as { on: unknown }).on = (_event: string, handler: unknown) => {
    captured = handler as Handler;
  };
  installDraftGate(pi as unknown as Pi);
  return {
    pi: pi as unknown as Pi,
    handler: () => {
      if (!captured) throw new Error("gate never subscribed");
      return captured;
    },
  };
}

const bashProbe = { type: "tool_call", toolCallId: "1", toolName: "bash", input: { command: "ls mods" } } as ToolCallEvent;

Deno.test("gate ends the turn while a review runs", () => {
  const { handler } = captureGate();
  const drafting = createDraft(createQuest("work", "abc123"), "work");
  const midReview = {
    ...drafting,
    draft: { ...drafting.draft!, planAuthored: true },
    activeReview: { kind: "draft" as const, target: "h1" },
  };
  replaceState(midReview);
  const blocked = handler()(bashProbe, fakeCtx("/tmp"));
  check(blocked?.block === true, "reads block mid-review");
  check(blocked?.terminate === true, "turn ends mid-review");
  check(String(blocked?.reason).includes("end your turn"), "reason says so");
  replaceState(IDLE_STATE);
});

Deno.test("gate leaves ordinary blocks unterminated", () => {
  const { handler } = captureGate();
  replaceState(createDraft(createQuest("work", "abc123"), "work"));
  const probe = { type: "tool_call", toolCallId: "1", toolName: "bash", input: { command: "rm -rf dist" } } as ToolCallEvent;
  const blocked = handler()(probe, fakeCtx("/tmp"));
  check(blocked?.block === true, "drafting blocks mutating tools");
  check(blocked?.terminate !== true, "no turn-end without a review");
  replaceState(IDLE_STATE);
});
