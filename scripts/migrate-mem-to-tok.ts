import { join } from "node:path";
import { existsSync, copyFileSync } from "node:fs";
import { encodeHtokPayload, decodePayload } from "../.pi/extensions/pi-mm/src/payload.ts";
import { DaemonCormRunner } from "../.pi/extensions/pi-mm/src/daemon.ts";
import { buildGetInvocation, buildStoreInvocation, buildListInvocation, filespecFor, ShellCormRunner } from "../.pi/extensions/pi-mm/src/corm.ts";

const homeDir = Deno.env.get("HOME") || "";
const repoRoot = Deno.env.get("REPO_ROOT") || (import.meta.dirname ? join(import.meta.dirname, "..") : ".");
const memDir = Deno.env.get("PI_MM_DIR") || join(repoRoot, ".pi", "mm");
const cormBin = Deno.env.get("CORM_BIN") || (homeDir ? join(homeDir, ".local", "bin", "corm") : "corm");
const axisPath = Deno.env.get("CORM_AXIS_PATH") || (homeDir ? join(homeDir, ".local", "lib") : "/usr/local/lib");
const sockPath = join(memDir, "cormd.sock");

if (!existsSync(join(memDir, "mem.db"))) {
  console.log("No mem.db found at", memDir);
  Deno.exit(0);
}

console.log("=== Starting migration from JSON strings to HTOK binary tokens ===");

const shellRunner = new ShellCormRunner(async (cmd, args, opts) => {
  const p = new Deno.Command(cmd, {
    args,
    cwd: opts?.cwd,
    env: opts?.env,
    stdin: opts?.stdin ? "piped" : "null",
    stdout: "piped",
    stderr: "piped",
  });
  if (opts?.stdin) {
    const child = p.spawn();
    const w = child.stdin.getWriter();
    await w.write(opts.stdin);
    await w.close();
    const out = await child.output();
    return {
      code: out.code,
      stdout: new TextDecoder().decode(out.stdout),
      stderr: new TextDecoder().decode(out.stderr),
      stdoutBytes: out.stdout,
    };
  }
  const out = await p.output();
  return {
    code: out.code,
    stdout: new TextDecoder().decode(out.stdout),
    stderr: new TextDecoder().decode(out.stderr),
    stdoutBytes: out.stdout,
  };
});

const runner = new DaemonCormRunner(sockPath, shellRunner);

const fspec = filespecFor(memDir, true);

// 1. List existing records
const listInv = buildListInvocation(cormBin, fspec, axisPath, memDir);
const listRes = await runner.run(listInv);
if (listRes.code !== 0) {
  console.error("Failed to list records:", listRes.stderr);
  Deno.exit(1);
}

const refs = listRes.stdout
  .split("\n")
  .map((s) => s.trim())
  .filter((s) => /^\d+$/.test(s))
  .map(Number);

console.log(`Found ${refs.length} active records in ${fspec}.`);

// Backup old files first
copyFileSync(join(memDir, "mem.db"), join(memDir, "mem.db.bak.legacy"));

let migrated = 0;
for (const ref of refs) {
  const getInv = buildGetInvocation(cormBin, fspec, ref, axisPath, memDir);
  const getRes = await runner.run(getInv);
  if (getRes.code !== 0) {
    console.warn(`Warning: failed to get ref ${ref}:`, getRes.stderr);
    continue;
  }

  const rawBytes = getRes.stdoutBytes ?? new TextEncoder().encode(getRes.stdout);
  const { date, envelope } = decodePayload(rawBytes);

  const htokBytes = encodeHtokPayload(date, envelope);
  const putInv = buildStoreInvocation(cormBin, fspec, ref, htokBytes, axisPath, memDir);
  const putRes = await runner.run(putInv);
  if (putRes.code !== 0) {
    console.error(`Failed to store HTOK for ref ${ref}:`, putRes.stderr);
    Deno.exit(1);
  }
  migrated++;
}

console.log(`Successfully migrated ${migrated}/${refs.length} records in-place to HTOK.`);
