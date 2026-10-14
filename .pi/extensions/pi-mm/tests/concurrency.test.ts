import assert from "node:assert";
import { makeStoreTool } from "../src/tools/store.ts";
import { makeConsolidateTool } from "../src/tools/consolidate.ts";
import { flushSessionBuffer, activeSessionBuffer } from "../src/hooks/events.ts";
import { withLock } from "../src/lock.ts";
import { DEFAULT_CONFIG } from "../src/config.ts";
import { give } from "../src/tools/index.ts";
import type { QmapRunner } from "../src/qmap.ts";
import type { PiExecResult } from "../src/hooks/events.ts";
import { join } from "node:path";
import { writeFile, readFile } from "node:fs/promises";

class ThreadSafeFakeStore {
  public records = new Map<number, string>();
  public nextId = 1;
  public delayMs = 10;

  createRunner(): QmapRunner {
    return {
      run: async (inv): Promise<PiExecResult> => {
        const { args } = inv;
        // Simulate a small asynchronous I/O delay
        await new Promise((r) => setTimeout(r, this.delayMs));

        if (args[0] === "-g") {
          // List refs
          const lines = Array.from(this.records.keys()).map((k) => String(k)).join("\n");
          return { code: 0, stdout: lines, stderr: "" };
        }
        if (args[0] === "-p") {
          // Store ref:payload
          const refArg = args[1];
          const colonIdx = refArg.indexOf(":");
          const ref = Number(refArg.slice(0, colonIdx));
          const payload = refArg.slice(colonIdx + 1);

          if (this.records.has(ref)) {
            // Ref collision detection!
            return { code: 1, stdout: "", stderr: `Collision: ref ${ref} already exists!` };
          }
          this.records.set(ref, payload);
          return { code: 0, stdout: String(ref), stderr: "" };
        }
        if (args[0] === "-r" && args[1] === "-g") {
          // Get ref
          const ref = Number(args[2]);
          const payload = this.records.get(ref);
          if (payload) return { code: 0, stdout: payload, stderr: "" };
          return { code: 1, stdout: "", stderr: "not found" };
        }
        if (args[0] === "-q" || args[0] === "-k") {
          const ref = Number(args[1]);
          const payload = this.records.get(ref);
          if (payload) return { code: 0, stdout: payload, stderr: "" };
          return { code: 1, stdout: "", stderr: "not found" };
        }
        return { code: 0, stdout: "", stderr: "" };
      },
    };
  }
}

Deno.test("concurrency: simultaneous manual stores receive unique refs without collision", async () => {
  const store = new ThreadSafeFakeStore();
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-concurrency-test-" });
  const ctx = { cwd: tmpDir, hasUI: false, mode: "agent" };

  const envFn = give({
    cwd: tmpDir,
    runner: store.createRunner(),
    nowProvider: () => new Date("2026-10-15T10:00:00Z"),
    cfg: { ...DEFAULT_CONFIG, qmapBin: "/usr/bin/qmap", memDir: tmpDir },
  });

  const tool = makeStoreTool(envFn);

  // Launch 8 concurrent store operations
  const tasks = Array.from({ length: 8 }, (_, i) =>
    tool.execute(`call_${i}`, { summary: `Concurrent memory item ${i}` }, undefined, undefined, ctx)
  );

  const results = await Promise.all(tasks);

  // Collect all allocated refs
  const refs = results.map((r: { details?: unknown }) => (r.details as { ref: number }).ref);
  assert.strictEqual(refs.length, 8);

  // Ensure all 8 refs are strictly unique (no collision)
  const uniqueRefs = new Set(refs);
  assert.strictEqual(uniqueRefs.size, 8);
  assert.strictEqual(store.records.size, 8);
});

Deno.test("concurrency: simultaneous automatic episode flush vs manual store allocate distinct refs", async () => {
  const store = new ThreadSafeFakeStore();
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-concurrency-flush-" });
  const ctx = { cwd: tmpDir, hasUI: false, mode: "agent" };

  const envFn = give({
    cwd: tmpDir,
    runner: store.createRunner(),
    nowProvider: () => new Date("2026-10-15T10:00:00Z"),
    cfg: { ...DEFAULT_CONFIG, qmapBin: "/usr/bin/qmap", memDir: tmpDir },
  });

  // Seed activeSessionBuffer with an event
  activeSessionBuffer.clear();
  activeSessionBuffer.record({
    id: "evt_concurrency_1",
    timestamp: "2026-10-15T10:00:00Z",
    toolName: "bash",
    summary: "cargo test: all 12 tests passed",
    isError: false,
    approach: "cargo test",
    outcome: "worked",
  });

  const storeTool = makeStoreTool(envFn);

  // Run auto-flush and manual store concurrently
  const [flushRef, storeRes] = await Promise.all([
    flushSessionBuffer(envFn, ctx),
    storeTool.execute("call_manual", { summary: "Manual store concurrent with flush" }, undefined, undefined, ctx),
  ]);

  const manualRef = (storeRes.details as { ref: number }).ref;
  assert.notStrictEqual(flushRef, null);
  assert.notStrictEqual(flushRef, manualRef);

  // Verify both exist in store
  assert.strictEqual(store.records.has(flushRef!), true);
  assert.strictEqual(store.records.has(manualRef), true);
  assert.strictEqual(store.records.size, 2);
});

Deno.test("concurrency: simultaneous consolidation attempts allocate unique refs without collision", async () => {
  const store = new ThreadSafeFakeStore();
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-concurrency-consolidate-" });
  const ctx = { cwd: tmpDir, hasUI: false, mode: "agent" };

  const envFn = give({
    cwd: tmpDir,
    runner: store.createRunner(),
    nowProvider: () => new Date("2026-10-15T10:00:00Z"),
    cfg: { ...DEFAULT_CONFIG, qmapBin: "/usr/bin/qmap", memDir: tmpDir },
  });

  // Seed store with an L0 source episode at ref 1
  store.records.set(
    1,
    `2026-10-15:{"level":"L0","type":"episode","summary":"Source episode","lifecycle":"observed"}`
  );

  const consolidateTool = makeConsolidateTool(envFn);

  // Run two consolidation calls concurrently targeting different abstractions
  const [res1, res2] = await Promise.all([
    consolidateTool.execute("call_c1", {
      targetLevel: "L1",
      sourceRefs: [1],
      summary: "First concurrent investigation",
    }, undefined, undefined, ctx),
    consolidateTool.execute("call_c2", {
      targetLevel: "L1",
      sourceRefs: [1],
      summary: "Second distinct concurrent investigation",
    }, undefined, undefined, ctx),
  ]);

  const ref1 = (res1.details as { ref: number }).ref;
  const ref2 = (res2.details as { ref: number }).ref;

  assert.notStrictEqual(ref1, ref2);
  assert.strictEqual(store.records.has(ref1), true);
  assert.strictEqual(store.records.has(ref2), true);
  assert.strictEqual(store.records.size, 3);
});

Deno.test("concurrency: withLock fails safely on prolonged lock contention instead of bypassing", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-lock-timeout-" });
  const lockFile = join(tmpDir, ".mm.lock");

  // Create an artificial active lock file with an active alive PID (our own process)
  await writeFile(lockFile, JSON.stringify({ pid: Deno.pid, createdAt: Date.now() }));

  let executionAttempted = false;
  let caughtError: Error | null = null;

  try {
    // Attempt with small timeout (100ms)
    await withLock(
      tmpDir,
      () => {
        executionAttempted = true;
        return Promise.resolve();
      },
      { timeoutMs: 100, staleThresholdMs: 10000 },
    );
  } catch (err) {
    caughtError = err as Error;
  }

  // MUST throw lock contention error and MUST NEVER execute fn() unsafely
  assert.strictEqual(executionAttempted, false);
  assert.notStrictEqual(caughtError, null);
  assert.strictEqual(caughtError!.message.includes("lock contention"), true);
});

Deno.test("concurrency: withLock safely recovers stale lock older than threshold", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-lock-stale-" });
  const lockFile = join(tmpDir, ".mm.lock");

  // Create an artificial stale lock file with older timestamp
  await writeFile(lockFile, JSON.stringify({ pid: 12345, createdAt: Date.now() - 10000 }));
  // Modify mtime to be 10 seconds ago
  const oldTime = new Date(Date.now() - 10000);
  await Deno.utime(lockFile, oldTime, oldTime);

  let executed = false;
  await withLock(
    tmpDir,
    () => {
      executed = true;
      return Promise.resolve();
    },
    { timeoutMs: 500, staleThresholdMs: 2000 },
  );

  assert.strictEqual(executed, true);
});

Deno.test("concurrency: multiple separate OS processes synchronize safely via withLock", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-proc-lock-" });
  const counterFile = join(tmpDir, "counter.txt");
  await writeFile(counterFile, "0");

  const cwd = Deno.cwd();
  const lockPath = cwd.includes(".pi/extensions/pi-mm")
    ? `${cwd}/src/lock.ts`
    : `${cwd}/.pi/extensions/pi-mm/src/lock.ts`;

  const workerScript = `
    import { withLock } from "${lockPath}";
    import { readFile, writeFile } from "node:fs/promises";
    import { join } from "node:path";

    const dir = Deno.args[0];
    const file = join(dir, "counter.txt");

    await withLock(dir, async () => {
      const val = parseInt((await readFile(file, "utf-8")).trim(), 10) || 0;
      await new Promise(r => setTimeout(r, 20));
      await writeFile(file, String(val + 1));
    });
  `;

  const scriptPath = join(tmpDir, "worker.ts");
  await writeFile(scriptPath, workerScript);

  // Spawn 4 genuinely separate OS processes concurrently
  const commands = Array.from({ length: 4 }, () => {
    const cmd = new Deno.Command(Deno.execPath(), {
      args: ["run", "--allow-all", "--sloppy-imports", scriptPath, tmpDir],
      stdout: "piped",
      stderr: "piped",
    });
    return cmd.output();
  });

  const outputs = await Promise.all(commands);
  for (const out of outputs) {
    assert.strictEqual(out.code, 0, new TextDecoder().decode(out.stderr));
  }

  const finalVal = parseInt((await Deno.readTextFile(counterFile)).trim(), 10);
  assert.strictEqual(finalVal, 4);
});

Deno.test("concurrency: active process lock is not pruned even if staleThreshold is exceeded", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-active-lock-" });
  const lockFile = join(tmpDir, ".mm.lock");

  // Write a lock file containing our own PID (which is definitely alive!)
  await writeFile(lockFile, JSON.stringify({ pid: Deno.pid, createdAt: Date.now() - 10000 }));
  const oldTime = new Date(Date.now() - 10000);
  await Deno.utime(lockFile, oldTime, oldTime);

  let executed = false;
  let caughtError: Error | null = null;
  try {
    // Attempt with small timeout (100ms) and short stale threshold (50ms)
    await withLock(
      tmpDir,
      () => {
        executed = true;
        return Promise.resolve();
      },
      { timeoutMs: 100, staleThresholdMs: 50 },
    );
  } catch (err) {
    caughtError = err as Error;
  }

  // Because PID is alive, withLock MUST NOT prune it, and must fail with timeout!
  assert.strictEqual(executed, false);
  assert.notStrictEqual(caughtError, null);
  assert.strictEqual(caughtError!.message.includes("lock contention"), true);
});

Deno.test("concurrency: withLock is strictly fail-closed on filesystem setup error", async () => {
  // A path under a regular file cannot have subdirectories created
  const tmpFile = await Deno.makeTempFile({ prefix: "mm-fail-closed-" });
  const badDir = join(tmpFile, "invalid_subdir");

  let executed = false;
  let caughtError: Error | null = null;
  try {
    await withLock(badDir, () => {
      executed = true;
      return Promise.resolve();
    });
  } catch (err) {
    caughtError = err as Error;
  }

  // MUST throw filesystem error and MUST NEVER execute fn()
  assert.strictEqual(executed, false);
  assert.notStrictEqual(caughtError, null);
});

Deno.test("concurrency: isPidAlive fails closed on ambiguous error (only ESRCH is dead)", async () => {
  const { isPidAlive } = await import("../src/lock.ts");

  // Current process is alive
  assert.strictEqual(isPidAlive(Deno.pid), true);

  // An impossible PID is dead (ESRCH)
  assert.strictEqual(isPidAlive(99999999), false);

  // Negative or zero PID is dead
  assert.strictEqual(isPidAlive(0), false);
  assert.strictEqual(isPidAlive(-1), false);
});

Deno.test("concurrency: replacement lock created during recovery is preserved and never clobbered", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-replacement-lock-" });
  const lockFile = join(tmpDir, ".mm.lock");

  // A live replacement lock exists
  const livePayload = { pid: Deno.pid, createdAt: Date.now() };
  await writeFile(lockFile, JSON.stringify(livePayload));

  // Running withLock from another call must NOT remove or overwrite this lock
  let executed = false;
  try {
    await withLock(tmpDir, () => {
      executed = true;
      return Promise.resolve();
    }, { timeoutMs: 80, staleThresholdMs: 30 });
  } catch {
    // Expected timeout contention
  }

  assert.strictEqual(executed, false);
  // Verify the lock file still exists and still belongs to the live PID!
  const remainingContent = JSON.parse(await readFile(lockFile, "utf-8"));
  assert.strictEqual(remainingContent.pid, Deno.pid);
});

Deno.test("concurrency: malformed lock file is treated as unknown ownership and never pruned", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-malformed-lock-" });
  const lockFile = join(tmpDir, ".mm.lock");

  // Write corrupt/malformed content and backdate mtime
  await writeFile(lockFile, "MALFORMED_NON_JSON_DATA{{{");
  const oldTime = new Date(Date.now() - 20000);
  await Deno.utime(lockFile, oldTime, oldTime);

  let executed = false;
  try {
    await withLock(tmpDir, () => {
      executed = true;
      return Promise.resolve();
    }, { timeoutMs: 80, staleThresholdMs: 30 });
  } catch {
    // Expected timeout
  }

  assert.strictEqual(executed, false);
  // Malformed lock MUST NOT be pruned!
  const remaining = await readFile(lockFile, "utf-8");
  assert.strictEqual(remaining, "MALFORMED_NON_JSON_DATA{{{");
});

Deno.test("concurrency: in-process queue releases mutex on filesystem setup failure", async () => {
  const tmpFile = await Deno.makeTempFile({ prefix: "mm-mutex-release-" });
  const badDir = join(tmpFile, "invalid_subdir");

  // First call fails at mkdir
  let firstFailed = false;
  try {
    await withLock(badDir, () => Promise.resolve());
  } catch {
    firstFailed = true;
  }
  assert.strictEqual(firstFailed, true);

  // Subsequent call on a valid directory MUST NOT hang or deadlock!
  const goodDir = await Deno.makeTempDir({ prefix: "mm-mutex-recovered-" });
  let secondSucceeded = false;
  await withLock(goodDir, () => {
    secondSucceeded = true;
    return Promise.resolve();
  });
  assert.strictEqual(secondSucceeded, true);
});

Deno.test("concurrency: two OS contenders racing over a stale lock never overlap critical sections", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-stale-race-" });
  const lockFile = join(tmpDir, ".mm.lock");
  const logFile = join(tmpDir, "contender_timeline.log");

  // Create an initial stale lock owned by an impossible dead PID
  await writeFile(lockFile, JSON.stringify({ pid: 99999999, createdAt: Date.now() - 10000 }));
  const oldTime = new Date(Date.now() - 10000);
  await Deno.utime(lockFile, oldTime, oldTime);

  const cwd = Deno.cwd();
  const lockModule = cwd.includes(".pi/extensions/pi-mm")
    ? `${cwd}/src/lock.ts`
    : `${cwd}/.pi/extensions/pi-mm/src/lock.ts`;

  const contenderScript = `
    import { withLock } from "${lockModule}";
    import { appendFile } from "node:fs/promises";

    const [dir, logPath, id] = Deno.args;
    await withLock(dir, async () => {
      const start = Date.now();
      await appendFile(logPath, \`\${id}:ENTER:\${start}\\n\`);
      // Hold critical section
      await new Promise((r) => setTimeout(r, 60));
      const end = Date.now();
      await appendFile(logPath, \`\${id}:EXIT:\${end}\\n\`);
    }, { timeoutMs: 3000, staleThresholdMs: 50 });
  `;

  const scriptPath = join(tmpDir, "contender.ts");
  await writeFile(scriptPath, contenderScript);

  // Launch two child OS processes simultaneously to race over the stale lock
  const p1 = new Deno.Command(Deno.execPath(), {
    args: ["run", "--allow-all", "--sloppy-imports", scriptPath, tmpDir, logFile, "P1"],
    stdout: "piped",
    stderr: "piped",
  }).output();

  const p2 = new Deno.Command(Deno.execPath(), {
    args: ["run", "--allow-all", "--sloppy-imports", scriptPath, tmpDir, logFile, "P2"],
    stdout: "piped",
    stderr: "piped",
  }).output();

  const [res1, res2] = await Promise.all([p1, p2]);
  assert.strictEqual(res1.code, 0, new TextDecoder().decode(res1.stderr));
  assert.strictEqual(res2.code, 0, new TextDecoder().decode(res2.stderr));

  // Parse timeline
  const timeline = (await readFile(logFile, "utf-8")).trim().split("\n");
  assert.strictEqual(timeline.length, 4);

  const events = timeline.map((line) => {
    const [proc, type, time] = line.split(":");
    return { proc, type, time: Number(time) };
  });

  // Verify that ENTER of the second contender happened at or after EXIT of the first
  const firstEnter = events[0];
  const firstExit = events[1];
  const secondEnter = events[2];
  const secondExit = events[3];

  assert.strictEqual(firstEnter.proc, firstExit.proc);
  assert.strictEqual(firstExit.type, "EXIT");
  assert.strictEqual(secondEnter.proc, secondExit.proc);
  assert.strictEqual(secondEnter.type, "ENTER");
  assert.notStrictEqual(firstEnter.proc, secondEnter.proc);
  assert.strictEqual(secondEnter.time >= firstExit.time, true);
});
