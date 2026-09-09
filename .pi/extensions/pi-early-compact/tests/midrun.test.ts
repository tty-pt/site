import { check, makeCtx, makeFakePi, makeConfig, settle } from "./fakes.ts";
import { installMidRunGuard } from "../src/midrun.ts";
import { PI_VCC_COMPACT_INSTRUCTION } from "../src/trigger.ts";
import type { EarlyCompactConfig } from "../src/config.ts";

function installWith(config: EarlyCompactConfig) {
  const pi = makeFakePi();
  installMidRunGuard({ pi, loadConfigFile: () => config });
  return pi;
}

function contextEvent() {
  return { messages: [] };
}

const OVER = 160_000; // adaptive 75% of 200k = 150k threshold
const WINDOW = 200_000;
const BELOW_WARN = 120_000; // warning band floor is 130k (65%)

Deno.test("midrun compacts once on crossing and queues a continuation", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, `compact once, got ${ctx.compactCalls}`);
  check(ctx.compactInstructions[0] === PI_VCC_COMPACT_INSTRUCTION, "compaction routed to pi-vcc via customInstructions");
  check(pi.sentMessages.length === 1, "continuation queued");
  const sent = pi.sentMessages[0];
  check(sent.message.customType === "pi-early-compact", "customType set");
  check(sent.message.display === true, "display true");
  check(sent.options.triggerTurn === true, "triggerTurn set");
  check(sent.options.deliverAs === "steer", "deliverAs steer");
});

Deno.test("midrun does not refire while still over threshold", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "single compact per crossing");
  check(pi.sentMessages.length === 1, "single continuation");
});

Deno.test("midrun re-arms only after falling below the warning band", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "first crossing compacts");

  ctx.usage.tokens = BELOW_WARN; // below warning (130k) -> re-arm
  await pi.emit("context", contextEvent(), ctx);
  ctx.usage.tokens = OVER;
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 2, `second crossing compacts, got ${ctx.compactCalls}`);
  check(pi.sentMessages.length === 2, "second continuation");
});

Deno.test("midrun stays disarmed between warning and threshold", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "crossing compacts");

  ctx.usage.tokens = 140_000; // inside warning band (130k..150k), not below
  await pi.emit("context", contextEvent(), ctx);
  ctx.usage.tokens = OVER;
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "no refire until re-arm below warning");
});

Deno.test("midrun respects disabled config", async () => {
  const pi = installWith(makeConfig({ enabled: false }));
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 0, "disabled -> no compact");
  check(pi.sentMessages.length === 0, "disabled -> no continuation");
});

Deno.test("midrun respects midRunCompact off", async () => {
  const pi = installWith(makeConfig({ midRunCompact: false }));
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 0, "midRunCompact off -> no compact");
  check(pi.sentMessages.length === 0, "no continuation");
});

Deno.test("midrun continueAfterCompact off compacts but does not resume", async () => {
  const pi = installWith(makeConfig({ continueAfterCompact: false }));
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "compacts at boundary");
  check(pi.sentMessages.length === 0, "no continuation queued");
});

Deno.test("midrun skips when no compact capability", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW, compact: false });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 0, "no capability -> no compact");
  check(pi.sentMessages.length === 0, "no continuation");
});

Deno.test("midrun skips when idle (no signal)", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW, signal: undefined });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 0, "idle -> no compact");
  check(pi.sentMessages.length === 0, "no continuation");
});

Deno.test("midrun skips when usage unknown", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: null, contextWindow: WINDOW });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 0, "unknown tokens -> no compact");
});

Deno.test("midrun resumes on soft compaction errors", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: OVER,
    contextWindow: WINDOW,
    onCompactError: () => new Error("pi-vcc: Nothing to compact (no live messages)"),
  });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "compact attempted");
  check(pi.sentMessages.length === 1, "soft error resumes the run");
  check(!ctx.notifyCalls.some((n) => n.msg.includes("failed mid-run")), "no failure notify");
});

Deno.test("midrun pauses on hard errors without continuation", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: OVER,
    contextWindow: WINDOW,
    onCompactError: () => new Error("boom"),
  });
  await pi.emit("context", contextEvent(), ctx);
  await settle();
  check(ctx.compactCalls === 1, "compact attempted");
  check(pi.sentMessages.length === 0, "hard error -> no continuation");
  check(ctx.notifyCalls.some((n) => n.msg.includes("failed mid-run")), "failure notified");
});

Deno.test("midrun routes through pi-vcc and treats its cancellation as soft (resumes, no failure notify)", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: OVER,
    contextWindow: WINDOW,
    onCompactError: () => new Error("Compaction cancelled"),
  });
  await pi.emit("context", contextEvent(), ctx);
  await settle();
  check(ctx.compactCalls === 1, "compact attempted");
  check(ctx.compactInstructions[0] === PI_VCC_COMPACT_INSTRUCTION, "routed to pi-vcc");
  check(pi.sentMessages.length === 1, "pi-vcc cancellation resumes the run");
  check(!ctx.notifyCalls.some((n) => n.msg.includes("failed mid-run")), "no failure notify for cancellation");
});

Deno.test("midrun auto-resumes on an incomplete summarization (token cap) instead of pausing", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: OVER,
    contextWindow: WINDOW,
    onCompactError: () =>
      new Error("Compaction failed: Summarization failed: generation hit the token cap and the summary is incomplete"),
  });
  await pi.emit("context", contextEvent(), ctx);
  await settle();
  check(ctx.compactCalls === 1, "compact attempted");
  check(ctx.compactInstructions[0] === PI_VCC_COMPACT_INSTRUCTION, "routed to pi-vcc (inert when absent)");
  check(pi.sentMessages.length === 1, "incomplete-summary failure auto-resumes the run");
  check(!ctx.notifyCalls.some((n) => n.msg.includes("failed mid-run")), "no failure notify for incomplete summary");
});

Deno.test("midrun auto-resumes on a summarization-aborted failure", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: OVER,
    contextWindow: WINDOW,
    onCompactError: () => new Error("Summarization aborted"),
  });
  await pi.emit("context", contextEvent(), ctx);
  await settle();
  check(ctx.compactCalls === 1, "compact attempted");
  check(pi.sentMessages.length === 1, "summarization abort auto-resumes the run");
  check(!ctx.notifyCalls.some((n) => n.msg.includes("failed mid-run")), "no failure notify for summarization abort");
});

Deno.test("session generation guards stale continuation", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW, pending: true });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "compact attempted");
  check(pi.sentMessages.length === 0, "nothing sent while compact pending");
  await pi.emit("session_start", {}, makeCtx({ tokens: 1 })); // new session mid-compact
  ctx.resolveCompact();
  check(pi.sentMessages.length === 0, "stale gen -> no continuation");
});

Deno.test("midrun hot-reloads config per event", async () => {
  const pi = makeFakePi();
  let cfg = makeConfig({ midRunCompact: false });
  installMidRunGuard({ pi, loadConfigFile: () => cfg });
  const ctx = makeCtx({ tokens: OVER, contextWindow: WINDOW });

  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 0, "off config -> no compact");

  cfg = makeConfig({ midRunCompact: true });
  await pi.emit("context", contextEvent(), ctx);
  check(ctx.compactCalls === 1, "re-read config applies immediately");
  check(pi.sentMessages.length === 1, "continuation after reload");
});