import { check } from "./check.ts";
import { DEFAULT_CONFIG, loadConfig } from "../src/config.ts";

Deno.test("config defaults match the spec", () => {
  check(DEFAULT_CONFIG.askTimeoutMs === 60000, "one minute default");
  check(DEFAULT_CONFIG.depthCap === 3, "depth cap 3");
  check(DEFAULT_CONFIG.draftThresholds.requirements === 2, "2 requirements");
  check(DEFAULT_CONFIG.draftThresholds.evidence === 7, "7 evidence");
  check(DEFAULT_CONFIG.bindings.asking.tool === "ask_questions", "asking binding");
  check(DEFAULT_CONFIG.bindings.reviewRunner.tool === "subagent", "runner binding");
});

Deno.test("config loads partial settings over defaults", () => {
  check(loadConfig(undefined).askTimeoutMs === 60000, "missing settings default");
  check(loadConfig(null).depthCap === 3, "null defaults");
  check(loadConfig(42).depthCap === 3, "non-object defaults");
  const partial = loadConfig({ askTimeoutMs: 5000, draftThresholds: { requirements: 1 } });
  check(partial.askTimeoutMs === 5000, "timeout overridden");
  check(partial.draftThresholds.requirements === 1, "threshold overridden");
  check(partial.draftThresholds.evidence === 7, "sibling threshold kept");
  check(partial.bindings.reviewRunner.tool === "subagent", "bindings kept");
  const bindings = loadConfig({ bindings: { asking: { tool: "custom_ask" } } });
  check(bindings.bindings.asking.tool === "custom_ask", "binding overridden");
});
