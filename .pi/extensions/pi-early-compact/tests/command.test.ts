import { check, makeCtx, makeFakePi, makeConfig } from "./fakes.ts";
import { installCommand } from "../src/command.ts";
import type { EarlyCompactConfig } from "../src/config.ts";
import { defaultConfig } from "../src/config.ts";

function install(loader?: () => EarlyCompactConfig) {
  const pi = makeFakePi();
  let cfg = loader?.() ?? defaultConfig();
  const deps = {
    loadConfigFile: () => cfg,
    saveConfigFile: (next: EarlyCompactConfig) => {
      cfg = next;
    },
    resetConfigFileFn: () => {
      cfg = defaultConfig();
    },
  };
  installCommand(pi, deps);
  return { pi, getCfg: () => cfg };
}

async function runCommand(pi: ReturnType<typeof install>["pi"], args: string, ctx = makeCtx()) {
  const def = pi.commands.find((c) => c.name === "early-compact")?.def;
  check(Boolean(def), "early-compact registered");
  await def.handler(args, ctx);
  return def;
}

Deno.test("command registers early-compact", () => {
  const { pi } = install();
  check(pi.commands.some((c) => c.name === "early-compact"), "command present");
});

Deno.test("command shows status on no args", async () => {
  const { pi } = install(() => makeConfig({ thresholdPct: 75 }));
  const ctx = makeCtx({ tokens: 100_000, contextWindow: 200_000 });
  await runCommand(pi, "", ctx);
  check(ctx.notifyCalls.length === 1, "status notified");
  check(ctx.notifyCalls[0].msg.includes("75%"), "status mentions threshold");
});

Deno.test("command sets percentage threshold", async () => {
  const { pi, getCfg } = install();
  await runCommand(pi, "75%");
  const cfg = getCfg();
  check(cfg.thresholdPct === 75, "thresholdPct set");
  check(cfg.thresholdTokens === null, "tokens cleared");
});

Deno.test("command sets token threshold with warning margin", async () => {
  const { pi, getCfg } = install();
  await runCommand(pi, "333k 30k");
  const cfg = getCfg();
  check(cfg.thresholdTokens === 333_000, "threshold tokens set");
  check(cfg.thresholdPct === null, "pct cleared");
  check(cfg.warningMarginTokens === 30_000, "warning margin set");
});

Deno.test("command off disables", async () => {
  const { pi, getCfg } = install();
  await runCommand(pi, "off");
  check(getCfg().enabled === false, "disabled");
});

Deno.test("command on enables", async () => {
  const { pi, getCfg } = install();
  await runCommand(pi, "on");
  check(getCfg().enabled === true, "enabled");
});

Deno.test("command default resets", async () => {
  const { pi, getCfg } = install(() => makeConfig({ thresholdPct: 75 }));
  await runCommand(pi, "default");
  const cfg = getCfg();
  check(cfg.thresholdPct === null, "pct reset");
  check(cfg.thresholdTokens === null, "tokens reset");
  check(cfg.adaptive === true, "adaptive default");
});

Deno.test("command manual turns off adaptive", async () => {
  const { pi, getCfg } = install();
  await runCommand(pi, "manual");
  check(getCfg().adaptive === false, "manual mode");
});

Deno.test("command adaptive turns it back on", async () => {
  const { pi, getCfg } = install(() => makeConfig({ adaptive: false }));
  await runCommand(pi, "adaptive");
  check(getCfg().adaptive === true, "adaptive re-enabled");
});

Deno.test("command midrun on enables mid-run compaction", async () => {
  const { pi, getCfg } = install(() => makeConfig({ midRunCompact: false }));
  await runCommand(pi, "midrun on");
  check(getCfg().midRunCompact === true, "mid-run enabled");
});

Deno.test("command midrun off disables mid-run compaction", async () => {
  const { pi, getCfg } = install();
  await runCommand(pi, "midrun off");
  check(getCfg().midRunCompact === false, "mid-run disabled");
});

Deno.test("command midrun show reports state", async () => {
  const { pi } = install();
  const ctx = makeCtx();
  await runCommand(pi, "midrun show", ctx);
  check(ctx.notifyCalls[0].msg.includes("mid-run on"), "reports on");
  check(ctx.notifyCalls[0].msg.includes("auto-continue"), "mentions auto-continue");
});

Deno.test("command midrun rejects garbage", async () => {
  const { pi } = install();
  const ctx = makeCtx();
  await runCommand(pi, "midrun maybe", ctx);
  check(ctx.notifyCalls[0].type === "warning", "warning type");
  check(ctx.notifyCalls[0].msg.includes("Usage: /early-compact midrun"), "usage hint");
});

Deno.test("command rejects garbage", async () => {
  const { pi } = install();
  const ctx = makeCtx();
  await runCommand(pi, "xyz", ctx);
  check(ctx.notifyCalls.length === 1, "warn notified");
  check(ctx.notifyCalls[0].type === "warning", "warning type");
});

Deno.test("completions offered", () => {
  const { pi } = install();
  const def = pi.commands.find((c) => c.name === "early-compact")?.def;
  check(Boolean(def?.getArgumentCompletions), "completions fn");
  const comps = def!.getArgumentCompletions!("7");
  check(comps.some((c: { value: string }) => c.value === "75%"), "offers 75%");
});