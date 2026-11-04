import { execSync } from "node:child_process";
import assert from "node:assert/strict";

console.log("=== Verifying Published Corm Axis Packages on NPM (R1) ===");

const expectedPackages = [
  { name: "@tty-pt/joint", version: "1.6.3" },
  { name: "@tty-pt/islet", version: "1.3.2" },
  { name: "@tty-pt/lemma", version: "1.2.2" },
  { name: "@tty-pt/sepal", version: "1.3.2" }
];

for (const pkg of expectedPackages) {
  try {
    const raw = execSync(`npm view ${pkg.name} --json`, { encoding: "utf8" });
    const info = JSON.parse(raw);
    console.log(`[PASS] ${pkg.name}: version=${info.version}, dist-tags=${JSON.stringify(info["dist-tags"])}`);
    assert.equal(info.version, pkg.version, `Version for ${pkg.name} must match`);
    assert.equal(info["dist-tags"].latest, pkg.version, `Latest tag for ${pkg.name} must match`);
  } catch (err) {
    console.error(`[FAIL] ${pkg.name} query failed:`, err.message);
    process.exit(1);
  }
}

console.log("\n=== Checking Xylem Registry & Tarball Packaging Readiness (R2) ===");
try {
  const xylemView = execSync("npm view @tty-pt/xylem version 2>/dev/null || echo 'not published'", { encoding: "utf8" }).trim();
  console.log(`[INFO] @tty-pt/xylem registry status: ${xylemView}`);

  const packRaw = execSync("cd external/libxylem/npm && npm pack --dry-run --json", { encoding: "utf8" });
  const packInfo = JSON.parse(packRaw);
  assert.equal(packInfo[0].name, "@tty-pt/xylem");
  assert.equal(packInfo[0].version, "1.7.2");
  assert.ok(packInfo[0].files.length >= 10, "Tarball must contain package files");
  console.log(`[PASS] @tty-pt/xylem tarball readiness verified: filename=${packInfo[0].filename}, files=${packInfo[0].files.length}`);
} catch (err) {
  console.error("[FAIL] Xylem check failed:", err.message);
  process.exit(1);
}

console.log("\nALL R1 AND R2 VERIFICATION CHECKS PASSED CLEANLY!");
