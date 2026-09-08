import { check } from "./fakes.ts";
import { makeConfig } from "./fakes.ts";
import {
  buildPressure,
  clampToMaxContextPct,
  formatTokens,
  parsePct,
  parseTokens,
  resolveThreshold,
  resolveWarningThreshold,
  selectTierPct,
  isPctInput,
} from "../src/policy.ts";

function usage(window: number) {
  return { contextWindow: window };
}

Deno.test("selectTierPct interpolates linearly between anchors", () => {
  // default curve: 250k@75, 500k@60, 1M@40
  check(selectTierPct(100_000) === 75, "100k window (below first) -> 75%");
  check(selectTierPct(250_000) === 75, "250k window -> 75%");
  check(selectTierPct(375_000) === 67.5, "375k window midpoint -> 67.5%");
  check(selectTierPct(500_000) === 60, "500k window -> 60%");
  check(selectTierPct(750_000) === 50, "750k window midpoint -> 50%");
  check(selectTierPct(1_000_000) === 40, "1M window -> 40%");
  check(selectTierPct(2_000_000) === 40, ">1M window clamps to last pct (40%)");
});

Deno.test("selectTierPct honors project tiers (200k@80, 1M@50)", () => {
  const tiers = [{ upTo: 200_000, pct: 80 }, { upTo: 1_000_000, pct: 50 }];
  check(selectTierPct(128_000, tiers) === 80, "below first anchor -> 80%");
  check(selectTierPct(200_000, tiers) === 80, "200k -> 80%");
  check(selectTierPct(600_000, tiers) === 65, "600k midpoint -> 65%");
  check(selectTierPct(1_000_000, tiers) === 50, "1M -> 50%");
  check(selectTierPct(2_000_000, tiers) === 50, "2M clamps to 50%");
});

Deno.test("selectTierPct converts token anchors to percent at their window", () => {
  const tiers = [{ upTo: 200_000, tokens: 160_000 }, { upTo: 1_000_000, pct: 50 }];
  check(selectTierPct(200_000, tiers) === 80, "160k tokens at 200k window -> 80%");
  check(selectTierPct(600_000, tiers) === 65, "600k midpoint -> 65%");
});

Deno.test("adaptive default resolves pct x window with no ceiling", () => {
  // small window: 75% of 128k = 96k
  const small = resolveThreshold({ config: makeConfig(), usage: usage(128_000) });
  check(small.threshold === 96_000, `128k -> 96k, got ${small.threshold}`);
  check(small.thresholdPct === 75, "128k pct 75");

  // mid window: 60% of 500k = 300k
  const mid = resolveThreshold({ config: makeConfig(), usage: usage(500_000) });
  check(mid.threshold === 300_000, `500k -> 300k, got ${mid.threshold}`);

  // large window: 40% of 2M = 800k, no ceiling clamp
  const large = resolveThreshold({ config: makeConfig(), usage: usage(2_000_000) });
  check(large.threshold === 800_000, `2M -> 800k, got ${large.threshold}`);

  // unknown window -> derived from first anchor (187.5k for 250k@75)
  const unknown = resolveThreshold({ config: makeConfig(), usage: usage(0) });
  check(unknown.threshold === 187_500, `unknown window -> first-anchor fallback, got ${unknown.threshold}`);
});

Deno.test("project tiers drive adaptive threshold resolution", () => {
  const settings = { thresholdPct: null, thresholdTokens: null, tiers: [
    { upTo: 200_000, pct: 80 },
    { upTo: 1_000_000, pct: 50 },
  ] };
  const at200k = resolveThreshold({ config: makeConfig(), usage: usage(200_000) }, undefined, settings);
  check(at200k.threshold === 160_000, `200k -> 80% of 200k = 160k, got ${at200k.threshold}`);
  check(at200k.thresholdPct === 80, "200k pct 80");
  check(at200k.tierApplied, "200k tier applied");

  const midpoint = resolveThreshold({ config: makeConfig(), usage: usage(600_000) }, undefined, settings);
  check(midpoint.threshold === 390_000, `600k -> 65% of 600k = 390k, got ${midpoint.threshold}`);

  const at1M = resolveThreshold({ config: makeConfig(), usage: usage(1_000_000) }, undefined, settings);
  check(at1M.threshold === 500_000, `1M -> 50% of 1M = 500k, got ${at1M.threshold}`);

  const above = resolveThreshold({ config: makeConfig(), usage: usage(2_000_000) }, undefined, settings);
  check(above.threshold === 1_000_000, `2M -> 50% clamp = 1M, got ${above.threshold}`);
});

Deno.test("explicit pct overrides tiers", () => {
  const r = resolveThreshold({ config: makeConfig({ thresholdPct: 30 }), usage: usage(200_000) });
  check(r.threshold === 60_000, `explicit 30% on 200k -> 60k, got ${r.threshold}`);
  check(!r.tierApplied, "explicit pct is not tier-applied");
  check(r.thresholdPct === 30, "explicit pct reported");
});

Deno.test("explicit tokens override everything", () => {
  const r = resolveThreshold({ config: makeConfig({ thresholdTokens: 250_000 }), usage: usage(1_000_000) });
  check(r.threshold === 250_000, "explicit tokens win");
});

Deno.test("settings hints apply after env, before tiers", () => {
  const settings = { thresholdPct: 60, thresholdTokens: null, tiers: null };
  const r = resolveThreshold({ config: makeConfig(), usage: usage(200_000) }, undefined, settings);
  check(r.threshold === 120_000, `settings 60% on 200k -> 120k, got ${r.threshold}`);

  // config outranks settings
  const cfgWins = resolveThreshold(
    { config: makeConfig({ thresholdPct: 30 }), usage: usage(200_000) },
    undefined,
    settings,
  );
  check(cfgWins.threshold === 60_000, `config 30% beats settings 60%, got ${cfgWins.threshold}`);
});

Deno.test("resolveWarningThreshold honors settings margin after env", () => {
  const r = resolveThreshold({ config: makeConfig(), usage: usage(200_000) }, undefined, { thresholdPct: null, thresholdTokens: null, tiers: null });
  check(r.threshold === 150_000, `tier 75% on 200k -> 150k, got ${r.threshold}`);
  const w = resolveWarningThreshold(
    { config: makeConfig(), usage: usage(200_000), threshold: r },
    undefined,
    { warningMarginTokens: 20_000, warningPct: null, warningTokens: null },
  );
  check(w === 130_000, `margin 20k -> warning at 130k, got ${w}`);
});

Deno.test("clampToMaxContextPct caps at 90", () => {
  check(clampToMaxContextPct(95) === 90, "95 -> 90");
  check(clampToMaxContextPct(50) === 50, "50 stays");
});

Deno.test("manual mode uses DEFAULT_PCT without ceiling", () => {
  const r = resolveThreshold({ config: makeConfig({ adaptive: false }), usage: usage(2_000_000) });
  check(r.threshold === 1_000_000, `manual on 2M -> 50% = 1M, got ${r.threshold}`);
  const s = resolveThreshold({ config: makeConfig({ adaptive: false }), usage: usage(200_000) });
  check(s.threshold === 100_000, `manual on 200k -> 50%, got ${s.threshold}`);
});

Deno.test("warning threshold: default delta below compact", () => {
  const cfg = makeConfig();
  const u = usage(128_000);
  const t = resolveThreshold({ config: cfg, usage: u });
  const w = resolveWarningThreshold({ config: cfg, usage: u, threshold: t });
  // 75% compact -> 10pp delta on 128k = 12.8k margin -> 96k - 12.8k = 83200
  check(w === 83_200, `128k default warning, got ${w}`);
});

Deno.test("warning threshold: explicit warningPct", () => {
  const cfg = makeConfig({ thresholdPct: 50, warningPct: 40 });
  const u = usage(200_000);
  const t = resolveThreshold({ config: cfg, usage: u });
  const w = resolveWarningThreshold({ config: cfg, usage: u, threshold: t });
  check(w === 80_000, `explicit warning 40% on 200k, got ${w}`);
});

Deno.test("warning threshold: explicit warningMarginTokens", () => {
  const cfg = makeConfig({ thresholdTokens: 200_000, warningMarginTokens: 30_000 });
  const u = usage(1_000_000);
  const t = resolveThreshold({ config: cfg, usage: u });
  const w = resolveWarningThreshold({ config: cfg, usage: u, threshold: t });
  check(w === 170_000, `margin 30k below 200k, got ${w}`);
});

Deno.test("buildPressure: none/warning/critical with fraction", () => {
  const none = buildPressure({ tokens: 50_000 }, 100_000, 80_000);
  check(none.pressure === "none", "below warning");
  check(none.fraction === 0, "none fraction 0");

  const warn = buildPressure({ tokens: 90_000 }, 100_000, 80_000);
  check(warn.pressure === "warning", "in warning band");
  check(Math.abs(warn.fraction - 0.5) < 1e-9, `warning fraction 0.5, got ${warn.fraction}`);

  const crit = buildPressure({ tokens: 110_000 }, 100_000, 80_000);
  check(crit.pressure === "critical", "critical");
  check(Math.abs(crit.fraction - 1.5) < 1e-9, `critical fraction 1.5, got ${crit.fraction}`);
});

Deno.test("parsePct and parseTokens accept v1 forms", () => {
  check(parsePct("75%") === 75, "parse 75%");
  check(parsePct("50.5%") === 50.5, "parse 50.5%");
  check(parsePct("100%") === null, "100% rejected (>90 cap)");
  check(parsePct("off") === null, "non-pct rejected");
  check(parseTokens("333k") === 333_000, "333k");
  check(parseTokens("50000") === 50_000, "plain tokens");
  check(parseTokens("1.5M") === null, "M suffix not supported");
  check(isPctInput("75%"), "isPctInput 75%");
  check(!isPctInput("75"), "isPctInput excludes bare number");
});

Deno.test("formatTokens renders k/M", () => {
  check(formatTokens(96_000) === "96k", "96k");
  check(formatTokens(1_000_000) === "1M", "1M");
  check(formatTokens(400_000) === "400k", "400k");
});