import { check } from "./fakes.ts";
import { readSettingsHints, type EarlyCompactConfig } from "../src/config.ts";

function withEnv(key: string, value: string, fn: () => void): void {
  const prev = Deno.env.get(key);
  try {
    Deno.env.set(key, value);
    fn();
  } finally {
    if (prev === undefined) Deno.env.delete(key);
    else Deno.env.set(key, prev);
  }
}

Deno.test("readSettingsHints parses project tiers", async () => {
  const dir = await Deno.makeTempDir();
  const file = `${dir}/settings.json`;
  await Deno.writeTextFile(file, JSON.stringify({
    "pi-early-compact": {
      "tiers": [
        { "upTo": 200000, "pct": 80 },
        { "upTo": 1000000, "pct": 50 },
      ],
    },
  }));
  withEnv("PI_EARLY_COMPACT_SETTINGS_PATH", file, async () => {
    const hints = readSettingsHints();
    check(hints.tiers !== null, "tiers parsed");
    check(hints.tiers!.length === 2, "two tiers");
    check(hints.tiers![0].upTo === 200_000 && hints.tiers![0].pct === 80, "first anchor 200k@80");
    check(hints.tiers![1].upTo === 1_000_000 && hints.tiers![1].pct === 50, "second anchor 1M@50");
  });
  await Deno.remove(dir, { recursive: true });
});

Deno.test("readSettingsHints accepts token-tier anchors", async () => {
  const dir = await Deno.makeTempDir();
  const file = `${dir}/settings.json`;
  await Deno.writeTextFile(file, JSON.stringify({
    "compaction": {
      "tiers": [
        { "upTo": 200000, "tokens": 160000 },
        { "upTo": 1000000, "pct": 50 },
      ],
    },
  }));
  withEnv("PI_EARLY_COMPACT_SETTINGS_PATH", file, async () => {
    const hints = readSettingsHints();
    check(hints.tiers !== null, "tiers parsed");
    check(hints.tiers![0].tokens === 160_000, "token anchor kept");
    check(hints.tiers![0].pct === undefined, "pct absent on token anchor");
  });
  await Deno.remove(dir, { recursive: true });
});

Deno.test("readSettingsHints rejects malformed tier arrays (falls back to defaults)", async () => {
  const dir = await Deno.makeTempDir();
  const file = `${dir}/settings.json`;
  await Deno.writeTextFile(file, JSON.stringify({
    "pi-early-compact": {
      "tiers": [
        { "upTo": 200000, "pct": 80, "tokens": 100000 },
      ],
    },
  }));
  withEnv("PI_EARLY_COMPACT_SETTINGS_PATH", file, async () => {
    const hints = readSettingsHints();
    check(hints.tiers === null, "both pct and tokens -> whole array rejected");
  });
  await Deno.remove(dir, { recursive: true });
});

Deno.test("config save/load round-trips mid-run flags", async () => {
  const dir = await Deno.makeTempDir();
  const file = `${dir}/pi-early-compact.json`;
  const cfg = await import("../src/config.ts");
  const saved: EarlyCompactConfig = {
    ...cfg.defaultConfig(),
    adaptive: false,
    midRunCompact: false,
    continueAfterCompact: true,
  } as EarlyCompactConfig;
  cfg.saveConfig(saved, file);
  const loaded = cfg.loadConfig(file);
  check(loaded.midRunCompact === false, "midRunCompact round-trips");
  check(loaded.continueAfterCompact === true, "continueAfterCompact round-trips");
  check(loaded.adaptive === false, "adaptive round-trips");
  check(loaded.enabled === true, "enabled defaults true");
  await Deno.remove(dir, { recursive: true });
});

Deno.test("readSettingsHints sorts tiers by upTo", async () => {
  const dir = await Deno.makeTempDir();
  const file = `${dir}/settings.json`;
  await Deno.writeTextFile(file, JSON.stringify({
    "pi-early-compact": {
      "tiers": [
        { "upTo": 1000000, "pct": 50 },
        { "upTo": 200000, "pct": 80 },
      ],
    },
  }));
  withEnv("PI_EARLY_COMPACT_SETTINGS_PATH", file, async () => {
    const hints = readSettingsHints();
    check(hints.tiers !== null, "tiers parsed");
    check(hints.tiers![0].upTo === 200_000, "sorted ascending");
    check(hints.tiers![1].upTo === 1_000_000, "sorted ascending second");
  });
  await Deno.remove(dir, { recursive: true });
});