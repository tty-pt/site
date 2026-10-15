import assert from "node:assert";
import { runRealEmbeddingSmokeTest } from "../scripts/test-real-embedding.ts";

Deno.test("real_embedding: smoke test runs or truthfully skips based on environment", async () => {
  const siteRoot = (() => {
    const cwd = Deno.cwd();
    if (cwd.includes(".pi/extensions/pi-mm")) {
      return cwd.split(".pi/extensions/pi-mm")[0].replace(/\/$/, "");
    }
    return cwd;
  })();

  const result = await runRealEmbeddingSmokeTest(siteRoot);
  assert.notStrictEqual(result.status, "FAIL", `Real embedding smoke test failed: ${result.reason}`);
  assert.ok(result.status === "PASS" || result.status === "SKIPPED");
  if (result.status === "SKIPPED") {
    assert.ok(typeof result.reason === "string", "Skip reason must be reported");
  }
});
