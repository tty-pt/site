const PKG = new URL("../", import.meta.url).pathname.replace(/\/$/, "");
const ROOT = new URL("../../../../", import.meta.url).pathname.replace(/\/$/, "");
const OUT = `${ROOT}/.pi/extensions/pi-early-compact-bundle.zip`;
const INPUTS = [
  "index.ts",
  "package.json",
  "README.md",
  "src",
  "tests",
  "scripts",
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
console.log(`pi-early-compact-bundle.zip: ${OUT}`);