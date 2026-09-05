import { check } from "../check.ts";
import type { Pi } from "../../src/hooks/events.ts";
import { createRunner, isReviewerAvailable } from "../../src/review/runner.ts";
import { fakeCtx, fakePi } from "../fake-pi.ts";

function busPi(toolNames: string[] = ["subagent"]): { pi: Pi; emitted: Array<{ event: string; data: unknown }>; feed: (data: unknown) => void } {
  const pi = fakePi();
  pi.toolNames = toolNames;
  const emitted: Array<{ event: string; data: unknown }> = [];
  const handlers = new Map<string, Array<(data: unknown) => void>>();
  const bus = {
    on: (event: string, handler: (data: unknown) => void) => {
      const list = handlers.get(event) ?? [];
      list.push(handler);
      handlers.set(event, list);
      return () => {};
    },
    emit: (event: string, data: unknown) => {
      emitted.push({ event, data });
    },
  };
  (pi as { events: unknown }).events = bus;
  const feed = (data: unknown) => {
    for (const handler of handlers.get("prompt-template:subagent:response") ?? []) handler(data);
  };
  return { pi, emitted, feed };
}

Deno.test("runner availability follows the configured tool", () => {
  const { pi } = busPi(["subagent"]);
  check(isReviewerAvailable(pi), "default tool present");
  check(isReviewerAvailable(pi, "custom-reviewer") === false, "custom tool absent");
  const custom = busPi(["custom-reviewer"]);
  check(isReviewerAvailable(custom.pi, "custom-reviewer"), "custom tool honored");
  const none = fakePi();
  check(!isReviewerAvailable(none), "no bridge degrades");
});

Deno.test("runner resolves review text through the bridge", async () => {
  const { pi, emitted, feed } = busPi();
  const runner = createRunner({ pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" });
  check(runner !== null, "runner created");
  const brief = { qid: "abc123" as never, kind: "draft" as const, target: "h1", evidence: [], criteria: [] };
  const launched = runner!.launch({ brief, prompt: "review this" }, new AbortController().signal);
  const request = emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "request emitted");
  const record = request!.data as Record<string, unknown>;
  check(record["context"] === "fresh" && record["agent"] === "reviewer", "isolated reviewer delegation");
  feed({ requestId: record["requestId"], status: "completed", result: { kind: "text", text: "VERDICT: PASS" }, childSessionId: "child-9" });
  const done = await launched;
  check(done.text === "VERDICT: PASS", "text resolved");
  check(done.childSessionId === "child-9", "child session reported");
});

Deno.test("runner rejects failed delegations and honors abort", async () => {
  const first = busPi();
  const runner = createRunner({ pi: first.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" });
  const brief = { qid: "abc123" as never, kind: "draft" as const, target: "h1", evidence: [], criteria: [] };
  const launched = runner!.launch({ brief, prompt: "x" }, new AbortController().signal);
  const request = first.emitted.find((e) => e.event === "prompt-template:subagent:request")!;
  first.feed({ requestId: (request.data as Record<string, unknown>)["requestId"], status: "error", error: "boom" });
  let threw = false;
  try {
    await launched;
  } catch {
    threw = true;
  }
  check(threw, "failed delegation rejects");

  const second = busPi();
  const runner2 = createRunner({ pi: second.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" })!;
  const controller = new AbortController();
  const pending = runner2.launch({ brief, prompt: "x" }, controller.signal);
  controller.abort();
  let aborted = false;
  try {
    await pending;
  } catch {
    aborted = true;
  }
  check(aborted, "abort rejects");
  check(second.emitted.some((e) => e.event === "prompt-template:subagent:cancel"), "cancel emitted");
});

Deno.test("runner refuses without a registered tool", () => {
  const pi = fakePi();
  check(createRunner({ pi, ctx: fakeCtx("/tmp"), ownerRunId: "x" }) === null, "null without tool");
});
