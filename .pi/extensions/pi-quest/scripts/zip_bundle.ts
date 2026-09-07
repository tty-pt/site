// Minimal bundle renderer (re-derived): zips the package source tree +
// docs into pi-quest-bundle.zip at the project root. Quest-state rendering
// (run manifest, quest view, session range) lands with the views slice (S3).

const PKG = new URL("..", import.meta.url).pathname;
const ROOT = `${PKG}/../../..`;
const OUT = `${ROOT}/pi-quest-bundle.zip`;
const INPUTS = [
  "index.ts",
  "package.json",
  "AGENTS.md",
  "README.md",
  "src",
  "tests",
  "scripts",
  "docs",
  "skills",
];

let code = 1;
let stderr = new Uint8Array();

try {
  const cmd = new Deno.Command("zip", {
    args: ["-r", "-q", OUT, ...INPUTS],
    cwd: PKG,
    stdout: "piped",
    stderr: "piped",
  });
  const res = await cmd.output();
  code = res.code;
  stderr = res.stderr;
} catch {
  // Fallback to python3 -m zipfile if zip executable is not available
  const pyCmd = new Deno.Command("python3", {
    args: ["-m", "zipfile", "-c", OUT, ...INPUTS],
    cwd: PKG,
    stdout: "piped",
    stderr: "piped",
  });
  const res = await pyCmd.output();
  code = res.code;
  stderr = res.stderr;
}
if (code !== 0) {
  const err = new TextDecoder().decode(stderr).trim();
  console.error(`bundle zip packaging failed: ${err || `exit ${code}`}`);
  Deno.exit(1);
}
console.log(`pi-quest-bundle.zip: ${OUT}`);
