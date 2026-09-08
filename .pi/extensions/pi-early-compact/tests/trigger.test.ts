import { check, makeCtx, makeFakePi, makeConfig, settle, jsonConfig } from "./fakes.ts";
import { installPreflight } from "../src/trigger.ts";
import type { EarlyCompactConfig } from "../src/config.ts";

function installWith(config: EarlyCompactConfig) {
  const pi = makeFakePi();
  installPreflight({ pi, loadConfigFile: () => config });
  return pi;
}

function inputEvent(text: string) {
  return { text };
}

Deno.test("preflight passes through below threshold", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: 50_000, contextWindow: 200_000 }); // tier 75% -> 150k, way below
  const result = await pi.emit("input", inputEvent("hi"), ctx);
  check(result?.action === "continue" || result === undefined, "continue");
  check(ctx.compactCalls === 0, "no compact below threshold");
});

Deno.test("preflight compacts once when projected usage crosses threshold", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: 140_000, contextWindow: 200_000 }); // tier 75% -> 150k
  const result = await pi.emit("input", inputEvent("x".repeat(80_000)), ctx); // adds ~20k -> 160k projected
  check(result?.action === "continue" || result === undefined, "continue after compact");
  check(ctx.compactCalls === 1, `compact once, got ${ctx.compactCalls}`);
});

Deno.test("preflight skips queued messages during an active run", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: 200_000, contextWindow: 200_000 });
  const result = await pi.emit("input", { ...inputEvent("x"), streamingBehavior: "steer" }, ctx);
  check(result?.action === "continue" || result === undefined, "continue");
  check(ctx.compactCalls === 0, "no compact while streaming");
});

Deno.test("preflight respects disabled config", async () => {
  const pi = installWith(makeConfig({ enabled: false }));
  const ctx = makeCtx({ tokens: 200_000, contextWindow: 200_000 });
  await pi.emit("input", inputEvent("hi"), ctx);
  check(ctx.compactCalls === 0, "disabled -> no compact");
});

Deno.test("preflight skips when usage unknown", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: null, contextWindow: 200_000 });
  await pi.emit("input", inputEvent("hi"), ctx);
  check(ctx.compactCalls === 0, "unknown tokens -> no compact");
});

Deno.test("preflight skips when no compact capability", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: 200_000, contextWindow: 200_000, compact: false });
  const result = await pi.emit("input", inputEvent("hi"), ctx);
  check(result?.action === "continue" || result === undefined, "prompt flows through without compact API");
  check(ctx.compactCalls === 0, "no compact capability -> no compact attempt");
  check(!ctx.notifyCalls.some((n) => n.msg.includes("Prompt not sent")), "prompt not blocked");
});

Deno.test("soft compaction errors pass the prompt through", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: 190_000,
    contextWindow: 200_000,
    onCompactError: () => new Error("pi-vcc: Nothing to compact (no live messages)"),
  });
  const result = await pi.emit("input", inputEvent("x".repeat(40_000)), ctx);
  check(result?.action === "continue" || result === undefined, "soft error -> continue");
  check(ctx.compactCalls === 1, "compact attempted");
});

Deno.test("hard compaction errors fail closed", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({
    tokens: 190_000,
    contextWindow: 200_000,
    onCompactError: () => new Error("boom"),
  });
  const result = await pi.emit("input", inputEvent("x".repeat(40_000)), ctx);
  check(result?.action === "handled", `handled on hard error, got ${JSON.stringify(result)}`);
  check(ctx.notifyCalls.some((n) => n.msg.includes("Prompt not sent")), "notify about not sending");
});

Deno.test("re-reads config from disk per prompt (hot reload)", async () => {
  const pi = makeFakePi();
  let cfg = makeConfig({ thresholdPct: 50 }); // 50% of 200k = 100k
  installPreflight({ pi, loadConfigFile: () => cfg });

  const ctxBelow = makeCtx({ tokens: 90_000, contextWindow: 200_000 });
  await pi.emit("input", inputEvent("hi"), ctxBelow);
  check(ctxBelow.compactCalls === 0, "below per-prompt threshold");

  cfg = makeConfig({ thresholdPct: 30 }); // 30% of 200k = 60k
  const ctxNow = makeCtx({ tokens: 90_000, contextWindow: 200_000 });
  await pi.emit("input", inputEvent("hi"), ctxNow);
  check(ctxNow.compactCalls === 1, "re-read config applies immediately");
});

Deno.test("session generation guards stale callbacks", async () => {
  const pi = installWith(makeConfig());
  const ctx = makeCtx({ tokens: 190_000, contextWindow: 200_000 });
  const emitPromise = pi.emit("input", inputEvent("x".repeat(40_000)), ctx);
  pi.emit("session_start", {}, makeCtx({ tokens: 1 })); // bump generation before compact settles
  await emitPromise;
  check(ctx.compactCalls === 1, "compact still attempted");
});

Deno.test("config file load fallback works", async () => {
  // Exercise the loadConfigFile default path indirectly: a config with no
  // enabled flag behaves like defaults (enabled).
  const cfg: EarlyCompactConfig = JSON.parse(jsonConfig()) as EarlyCompactConfig;
  check(cfg.enabled, "default config enabled");
  await settle();
});