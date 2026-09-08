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

const SUPPORTED_BRIDGE_FIELDS = new Set([
  "requestId", "ownerRunId", "nodeId", "agent", "task", "context", "cwd",
  "model", "thinking", "timeoutMs", "toolBudget", "skill", "artifacts", "result",
]);

Deno.test("runner resolves review text through the bridge", async () => {
  const { pi, emitted, feed } = busPi();
  const runner = createRunner({ pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" });
  check(runner !== null, "runner created");
  const launched = runner!.launch("review this", new AbortController().signal);
  const request = emitted.find((e) => e.event === "prompt-template:subagent:request");
  check(request !== undefined, "request emitted");
  const record = request!.data as Record<string, unknown>;
  check(record["context"] === "fresh" && record["agent"] === "reviewer", "isolated reviewer delegation");
  const extras = Object.keys(record).filter((k) => !SUPPORTED_BRIDGE_FIELDS.has(k));
  check(extras.length === 0, `no unsupported fields (bridge rejects them): ${extras.join(",")}`);
  feed({ requestId: record["requestId"], status: "completed", result: { kind: "text", text: "VERDICT: PASS" } });
  const done = await launched;
  check(done.text === "VERDICT: PASS", "text resolved");
});

Deno.test("runner rejects failed delegations and honors abort", async () => {
  const first = busPi();
  const runner = createRunner({ pi: first.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" });
  const launched = runner!.launch("x", new AbortController().signal);
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
  const pending = runner2.launch("x", controller.signal);
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

Deno.test("runner sends the configured model and omits it otherwise", async () => {
  const first = busPi();
  const runner = createRunner({ pi: first.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123", model: "requesty/openai/o3-mini:high" });
  const launched = runner!.launch("x", new AbortController().signal);
  const request = first.emitted.find((e) => e.event === "prompt-template:subagent:request")!;
  check((request.data as Record<string, unknown>)["model"] === "requesty/openai/o3-mini:high", "explicit model sent");
  first.feed({ requestId: (request.data as Record<string, unknown>)["requestId"], status: "completed", result: { kind: "text", text: "ok" } });
  await launched;

  const second = busPi();
  const runner2 = createRunner({ pi: second.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" })!;
  const pending = runner2.launch("x", new AbortController().signal);
  const bare = second.emitted.find((e) => e.event === "prompt-template:subagent:request")!;
  check(!("model" in (bare.data as Record<string, unknown>)), "inherit omits model");
  second.feed({ requestId: (bare.data as Record<string, unknown>)["requestId"], status: "completed", result: { kind: "text", text: "ok" } });
  await pending;
});

Deno.test("runner sends thinking only when configured", async () => {
  const first = busPi();
  const runner = createRunner({ pi: first.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123", thinking: "off" });
  const launched = runner!.launch("x", new AbortController().signal);
  const request = first.emitted.find((e) => e.event === "prompt-template:subagent:request")!;
  check((request.data as Record<string, unknown>)["thinking"] === "off", "explicit thinking sent");
  first.feed({ requestId: (request.data as Record<string, unknown>)["requestId"], status: "completed", result: { kind: "text", text: "ok" } });
  await launched;

  const second = busPi();
  const runner2 = createRunner({ pi: second.pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" })!;
  const pending = runner2.launch("x", new AbortController().signal);
  const bare = second.emitted.find((e) => e.event === "prompt-template:subagent:request")!;
  check(!("thinking" in (bare.data as Record<string, unknown>)), "inherit omits thinking");
  second.feed({ requestId: (bare.data as Record<string, unknown>)["requestId"], status: "completed", result: { kind: "text", text: "ok" } });
  await pending;
});

Deno.test("runner rejection carries the bridge error text", async () => {
  const { pi, emitted, feed } = busPi();
  const runner = createRunner({ pi, ctx: fakeCtx("/tmp"), ownerRunId: "abc123" })!;
  const launched = runner.launch("x", new AbortController().signal);
  const request = emitted.find((e) => e.event === "prompt-template:subagent:request")!;
  feed({
    requestId: (request.data as Record<string, unknown>)["requestId"],
    status: "error",
    error: 'Model "antigravity/gemini-3.8-flash:high" not found. Use --list-models to see available models.',
  });
  let message = "";
  try {
    await launched;
  } catch (err) {
    message = err instanceof Error ? err.message : String(err);
  }
  check(message.includes("not found"), "bridge error text preserved for classification");
});

Deno.test("runner enforces the configured max duration, not the inactivity window", async () => {
  const { pi } = busPi();
  const runner = createRunner({
    pi,
    ctx: fakeCtx("/tmp"),
    ownerRunId: "abc123",
    maxDurationMs: 60,
    inactivityLimitMs: 5000,
  });
  const launched = runner!.launch("x", new AbortController().signal);
  let message = "";
  try {
    await launched;
  } catch (err) {
    message = err instanceof Error ? err.message : String(err);
  }
  check(message.includes("quest_journal_deadline"), "deadline enforced");
  check(!message.includes("inactivity"), "max duration fired, not the wider inactivity window");
});
