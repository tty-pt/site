import { check, makeCtx, makeFakePi, makeConfig } from "./fakes.ts";
import { installStatusFooter } from "../src/status.ts";
import type { EarlyCompactConfig } from "../src/config.ts";

function install(config: EarlyCompactConfig) {
  const pi = makeFakePi();
  installStatusFooter(pi, () => config);
  return pi;
}

Deno.test("status footer shows warning near threshold", () => {
  const pi = install(makeConfig({ thresholdPct: 50 })); // 50% of 200k = 100k
  const ctx = makeCtx({ tokens: 90_000, contextWindow: 200_000 }); // warning band (90k within delta)
  ctx.ui.theme.fg = (color: string, text: string) => `${color}>>${text}`;
  pi.emit("turn_end", {}, ctx);
  check(ctx.statusCalls.length >= 1, "status set");
  const last = ctx.statusCalls[ctx.statusCalls.length - 1];
  check(last.text !== undefined, "warning text set");
  check(last.text!.startsWith("warning>>●"), `yellow ball, got ${last.text}`);
});

Deno.test("status footer shows critical at threshold in red", () => {
  const pi = install(makeConfig({ thresholdPct: 50 }));
  const ctx = makeCtx({ tokens: 120_000, contextWindow: 200_000 });
  ctx.ui.theme.fg = (color: string, text: string) => `${color}>>${text}`;
  pi.emit("turn_end", {}, ctx);
  const last = ctx.statusCalls[ctx.statusCalls.length - 1];
  check(last.text!.startsWith("error>>●"), `red ball, got ${last.text}`);
});

Deno.test("status footer differentiates warning vs critical by color", () => {
  const pi = install(makeConfig({ thresholdPct: 50 }));
  const warn = makeCtx({ tokens: 90_000, contextWindow: 200_000 });
  const crit = makeCtx({ tokens: 120_000, contextWindow: 200_000 });
  (warn.ui.theme).fg = (c: string, t: string) => `${c}>>${t}`;
  (crit.ui.theme).fg = (c: string, t: string) => `${c}>>${t}`;
  pi.emit("turn_end", {}, warn);
  pi.emit("turn_end", {}, crit);
  const w = warn.statusCalls[warn.statusCalls.length - 1].text!;
  const c = crit.statusCalls[crit.statusCalls.length - 1].text!;
  check(w.startsWith("warning>>"), "warning colored yellow");
  check(c.startsWith("error>>"), "critical colored red");
});

Deno.test("status footer shows green ball when usage low", () => {
  const pi = install(makeConfig({ thresholdPct: 50 }));
  const ctx = makeCtx({ tokens: 10_000, contextWindow: 200_000 });
  ctx.ui.theme.fg = (color: string, text: string) => `${color}>>${text}`;
  pi.emit("turn_end", {}, ctx);
  const last = ctx.statusCalls[ctx.statusCalls.length - 1];
  check(last.text!.startsWith("success>>●"), `green ball, got ${last.text}`);
});

Deno.test("status footer keeps the ball when the theme throws on an unknown color", () => {
  const pi = install(makeConfig({ thresholdPct: 50 }));
  const ctx = makeCtx({ tokens: 10_000, contextWindow: 200_000 });
  ctx.ui.theme.fg = () => {
    throw new Error("Unknown theme color");
  };
  pi.emit("turn_end", {}, ctx);
  const last = ctx.statusCalls[ctx.statusCalls.length - 1];
  check(last.text === "●", "ball still rendered even when fg throws");
});

Deno.test("status footer clears when disabled", () => {
  const pi = install(makeConfig({ enabled: false }));
  const ctx = makeCtx({ tokens: 120_000, contextWindow: 200_000 });
  pi.emit("turn_end", {}, ctx);
  const last = ctx.statusCalls[ctx.statusCalls.length - 1];
  check(last.text === undefined, "cleared when disabled");
});

Deno.test("status footer no-ops without UI", () => {
  const pi = install(makeConfig({ thresholdPct: 50 }));
  const ctx = makeCtx({ tokens: 120_000, contextWindow: 200_000, hasUI: false });
  pi.emit("turn_end", {}, ctx);
  check(ctx.statusCalls.length === 0, "no status without UI");
});