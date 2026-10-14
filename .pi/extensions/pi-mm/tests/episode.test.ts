import assert from "node:assert";
import { join } from "node:path";
import { makeStoreTool } from "../src/tools/store.ts";
import { SessionEventBuffer } from "../src/hooks/events.ts";
import { fakeRunner, runTool, okResult } from "./fake-qmap.ts";
import { DEFAULT_CONFIG, type MmConfig } from "../src/config.ts";
import { decodePayload } from "../src/payload.ts";
import { give, type ToolEnv } from "../src/tools/index.ts";
import type { QmapRunner } from "../src/qmap.ts";

const CWD = await Deno.makeTempDir({ prefix: "mm-episode-cwd-" });
const ctx = { cwd: CWD, hasUI: false, mode: "agent" };

function now(): Date {
  return new Date("2026-10-14T12:00:00Z");
}

function env(runner: QmapRunner, cfg: Partial<MmConfig> = {}): ToolEnv {
  return {
    cwd: CWD,
    runner,
    nowProvider: now,
    cfg: { ...DEFAULT_CONFIG, qmapBin: "/usr/bin/qmap", ...cfg },
  };
}

Deno.test("episode: SessionEventBuffer captures tool outcomes and approaches", () => {
  const buf = new SessionEventBuffer();
  buf.record({
    id: "evt_1",
    timestamp: "2026-10-14T12:01:00Z",
    toolName: "bash",
    summary: "cargo build failed: error[E0308]: mismatched types",
    isError: true,
    approach: "Cargo build with strict types",
    outcome: "failed",
  });
  buf.record({
    id: "evt_2",
    timestamp: "2026-10-14T12:05:00Z",
    toolName: "bash",
    summary: "cargo build: ok",
    isError: false,
    approach: "Cargo build with type coercion",
    outcome: "worked",
  });

  const approaches = buf.getApproaches();
  assert.strictEqual(approaches.length, 2);
  assert.strictEqual(approaches[0].outcome, "failed");
  assert.strictEqual(approaches[0].reason?.includes("E0308"), true);
  assert.strictEqual(approaches[1].outcome, "worked");

  buf.clear();
  assert.strictEqual(buf.getEvents().length, 0);
});

Deno.test("episode: store captures comprehensive L0 episode with failed vs worked approaches", async () => {
  const runner = fakeRunner([
    okResult(""),
    okResult("1"),
  ]);

  const tool = makeStoreTool(give(env(runner)));
  const res = await runTool(tool, {
    level: "L0",
    type: "episode",
    episode: {
      problem: "Intermittent SIGSEGV in stoma_query_eval",
      approaches: [
        {
          approach: "Increase buffer size in stoma_read",
          outcome: "failed",
          reason: "Still crashed with buffer overflow on large query terms",
        },
        {
          approach: "Add null terminator check before index scan",
          outcome: "worked",
          reason: "Prevented reading beyond buffer bounds",
        },
      ],
      observations: ["Query string was not 0-terminated when length exactly equaled chunk size"],
      findings: ["stoma buffer boundaries must check null terminator explicitly"],
      unresolved: ["Benchmark performance impact under 100k queries"],
      environment: {
        compiler: "gcc 13.2.0",
        commit: "a8f3b21",
        os: "linux",
      },
      outcome: "success",
    },
    context: {
      project: "site",
      subsystem: "libcorm",
      module: "libstoma",
    },
  }, ctx);

  const details = res.details as Record<string, unknown>;
  assert.strictEqual(details.ref, 1);
  assert.strictEqual(details.level, "L0");
  assert.strictEqual(details.summary, "Intermittent SIGSEGV in stoma_query_eval");

  const arg = runner.invocations[1].args[1];
  const storedPayload = arg.slice(arg.indexOf(":") + 1);
  const { date, envelope } = decodePayload(storedPayload);
  assert.strictEqual(date, "2026-10-14");
  assert.strictEqual(envelope.level, "L0");
  assert.strictEqual(envelope.type, "episode");
  assert.strictEqual(envelope.episode?.problem, "Intermittent SIGSEGV in stoma_query_eval");
  assert.strictEqual(envelope.episode?.approaches?.length, 2);
  assert.strictEqual(envelope.episode?.approaches?.[0].outcome, "failed");
  assert.strictEqual(envelope.episode?.approaches?.[1].outcome, "worked");
  assert.strictEqual(envelope.episode?.environment?.compiler, "gcc 13.2.0");
});

Deno.test("episode: end-to-end capture hook -> persistence -> fresh session reconstruction -> scan retrieval", async () => {
  const { makeScanTool } = await import("../src/tools/scan.ts");
  const { installSessionHooks, activeSessionBuffer } = await import("../src/hooks/events.ts");

  // Step 1: Simulate Pi tool hook firing with test failure
  activeSessionBuffer.clear();
  const mockPi = {
    handlers: new Map<string, Array<(...args: unknown[]) => unknown>>(),
    on(event: string, handler: (...args: unknown[]) => unknown): void {
      if (!this.handlers.has(event)) this.handlers.set(event, []);
      this.handlers.get(event)!.push(handler);
    },
    emit(event: string, data: unknown): void {
      for (const h of this.handlers.get(event) || []) h(data);
    },
  };

  installSessionHooks(mockPi as unknown as import("../src/hooks/events.ts").Pi);
  mockPi.emit("tool_result", {
    toolCallId: "call_1",
    toolName: "bash",
    isError: true,
    content: [{ type: "text", text: "FAIL: test_corm_alias failed with exit code 1" }],
  });

  const captured = activeSessionBuffer.getApproaches();
  assert.strictEqual(captured.length, 1);
  assert.strictEqual(captured[0].outcome, "failed");

  // Step 2: Persist episode via memory_store
  const runnerSession1 = fakeRunner([
    okResult(""),
    okResult("1"),
  ]);
  const storeTool = makeStoreTool(give(env(runnerSession1)));
  const storeRes = await runTool(storeTool, {
    level: "L0",
    type: "episode",
    summary: "Investigation of corm_alias failure",
    episode: {
      problem: "corm_alias test failed on double open",
      approaches: captured,
      outcome: "failed",
    },
    context: { project: "site", subsystem: "libcorm" },
  }, ctx);
  assert.strictEqual((storeRes.details as Record<string, unknown>).ref, 1);

  const storedPayload = runnerSession1.invocations[1].args[1];
  const storedJson = storedPayload.slice(storedPayload.indexOf(":") + 1);

  // Step 3: Reconstruct fresh session / client
  const runnerSession2 = fakeRunner([
    okResult(`1 1.000000 ${storedJson}\n`),
  ]);
  const scanTool = makeScanTool(give(env(runnerSession2)));

  // Step 4: Scan and retrieve from fresh session
  const scanRes = await runTool(scanTool, { topic: "corm_alias", level: "L0" }, ctx);
  const details = scanRes.details as { records: Array<{ ref: number; envelope?: { episode?: { approaches?: Array<{ outcome: string }> } } }> };

  assert.strictEqual(details.records.length, 1);
  assert.strictEqual(details.records[0].ref, 1);
  assert.strictEqual(details.records[0].envelope?.episode?.approaches?.[0].outcome, "failed");
});

Deno.test("episode: automatic persistence on agent_settled without manual store call", async () => {
  const { installSessionHooks, activeSessionBuffer } = await import("../src/hooks/events.ts");

  activeSessionBuffer.clear();
  const runner = fakeRunner([
    okResult(""), // list all refs
    okResult(""), // store invocation
  ]);

  const mockPi = {
    handlers: new Map<string, Array<(...args: unknown[]) => unknown>>(),
    on(event: string, handler: (...args: unknown[]) => unknown): void {
      if (!this.handlers.has(event)) this.handlers.set(event, []);
      this.handlers.get(event)!.push(handler);
    },
    async emit(event: string, data: unknown, ctxData?: unknown): Promise<void> {
      for (const h of this.handlers.get(event) || []) await h(data, ctxData);
    },
  };

  installSessionHooks(mockPi as unknown as import("../src/hooks/events.ts").Pi, give(env(runner)));

  // 1. Tool result fires
  mockPi.emit("tool_result", {
    toolCallId: "call_auto",
    toolName: "bash",
    isError: true,
    content: [{ type: "text", text: "FAIL: stoma index corruption on null byte" }],
  });

  // 2. Turn finishes / agent settles -> automatic flush
  await mockPi.emit("agent_settled", {}, ctx);

  // Assert store invocation occurred automatically
  assert.strictEqual(runner.invocations.length, 2);
  const storedArg = runner.invocations[1].args[1];
  const { envelope } = decodePayload(storedArg.slice(storedArg.indexOf(":") + 1));
  assert.strictEqual(envelope.level, "L0");
  assert.strictEqual(envelope.type, "episode");
  assert.strictEqual(envelope.lifecycle, "observed"); // Default to observed!
  assert.strictEqual(envelope.summary.includes("stoma index corruption"), true);

  // 3. Second settle event with no new tool output produces zero duplicate writes (idempotent deduplication)
  await mockPi.emit("agent_settled", {}, ctx);
  assert.strictEqual(runner.invocations.length, 2);
});

Deno.test("episode: failed write followed by retry preserves unpersisted events and succeeds", async () => {
  const { installSessionHooks, activeSessionBuffer } = await import("../src/hooks/events.ts");

  activeSessionBuffer.clear();

  let shouldFail = true;
  const runner: QmapRunner = {
    run(inv: import("../src/qmap.ts").Invocation) {
      if (inv.args[0] === "-g" && inv.args[1] === ".") {
        return Promise.resolve({ stdout: "", stderr: "", code: 0 });
      }
      if (inv.args[0] === "-p") {
        if (shouldFail) {
          return Promise.resolve({ stdout: "", stderr: "disk I/O error", code: 1 });
        }
        return Promise.resolve({ stdout: "1", stderr: "", code: 0 });
      }
      return Promise.resolve({ stdout: "", stderr: "", code: 0 });
    },
  };

  const mockPi = {
    handlers: new Map<string, Array<(...args: unknown[]) => unknown>>(),
    on(event: string, handler: (...args: unknown[]) => unknown): void {
      if (!this.handlers.has(event)) this.handlers.set(event, []);
      this.handlers.get(event)!.push(handler);
    },
    async emit(event: string, data: unknown, ctxData?: unknown): Promise<void> {
      for (const h of this.handlers.get(event) || []) await h(data, ctxData);
    },
  };

  installSessionHooks(mockPi as unknown as import("../src/hooks/events.ts").Pi, give(env(runner)));

  mockPi.emit("tool_result", {
    toolCallId: "call_retry_1",
    toolName: "bash",
    isError: true,
    content: [{ type: "text", text: "FAIL: connection refused on port 8080" }],
  });

  // Attempt 1: Settle when store fails
  await mockPi.emit("agent_settled", {}, ctx);
  assert.strictEqual(activeSessionBuffer.hasUnflushed(), true);
  assert.strictEqual(activeSessionBuffer.lastError?.includes("disk I/O error"), true);

  // Attempt 2: Store now recovers
  shouldFail = false;
  await mockPi.emit("agent_settled", {}, ctx);
  assert.strictEqual(activeSessionBuffer.hasUnflushed(), false);
  assert.strictEqual(activeSessionBuffer.lastError, null);
});

Deno.test("episode: session_start preserves unpersisted events but drops flushed ones", async () => {
  const { installSessionHooks, activeSessionBuffer } = await import("../src/hooks/events.ts");

  activeSessionBuffer.clear();

  let allowStore = true;
  const runner: QmapRunner = {
    run(inv: import("../src/qmap.ts").Invocation) {
      if (inv.args[0] === "-g" && inv.args[1] === ".") {
        return Promise.resolve({ stdout: "", stderr: "", code: 0 });
      }
      if (inv.args[0] === "-p") {
        if (!allowStore) return Promise.resolve({ stdout: "", stderr: "locked", code: 1 });
        return Promise.resolve({ stdout: "1", stderr: "", code: 0 });
      }
      return Promise.resolve({ stdout: "", stderr: "", code: 0 });
    },
  };

  const mockPi = {
    handlers: new Map<string, Array<(...args: unknown[]) => unknown>>(),
    on(event: string, handler: (...args: unknown[]) => unknown): void {
      if (!this.handlers.has(event)) this.handlers.set(event, []);
      this.handlers.get(event)!.push(handler);
    },
    async emit(event: string, data: unknown, ctxData?: unknown): Promise<void> {
      for (const h of this.handlers.get(event) || []) await h(data, ctxData);
    },
  };

  installSessionHooks(mockPi as unknown as import("../src/hooks/events.ts").Pi, give(env(runner)));

  // Batch 1: successfully flushed
  mockPi.emit("tool_result", {
    toolCallId: "call_batch1",
    toolName: "bash",
    isError: true,
    content: [{ type: "text", text: "FAIL: batch 1 test error" }],
  });
  await mockPi.emit("agent_settled", {}, ctx);
  assert.strictEqual(activeSessionBuffer.hasUnflushed(), false);

  // Batch 2: fails to flush
  allowStore = false;
  mockPi.emit("tool_result", {
    toolCallId: "call_batch2",
    toolName: "bash",
    isError: true,
    content: [{ type: "text", text: "FAIL: batch 2 test error" }],
  });
  await mockPi.emit("agent_settled", {}, ctx);
  assert.strictEqual(activeSessionBuffer.hasUnflushed(), true);

  // New session begins: session_start fires
  mockPi.emit("session_start", {});

  // Unpersisted batch 2 MUST be preserved!
  assert.strictEqual(activeSessionBuffer.hasUnflushed(), true);
  const remaining = activeSessionBuffer.getUnflushed();
  assert.strictEqual(remaining.length, 1);
  assert.strictEqual(remaining[0].id, "call_batch2");

  // Storage recovers in new session
  allowStore = true;
  await mockPi.emit("agent_settled", {}, ctx);
  assert.strictEqual(activeSessionBuffer.hasUnflushed(), false);
});

Deno.test("episode: captures diffs and runtime environment from tool modifications", async () => {
  const { installSessionHooks, activeSessionBuffer } = await import("../src/hooks/events.ts");

  activeSessionBuffer.clear();
  let storedPayload = "";
  const runner: QmapRunner = {
    run(inv: import("../src/qmap.ts").Invocation) {
      if (inv.args[0] === "-g" && inv.args[1] === ".") {
        return Promise.resolve({ stdout: "", stderr: "", code: 0 });
      }
      if (inv.args[0] === "-p") {
        storedPayload = inv.args[1];
        return Promise.resolve({ stdout: "1", stderr: "", code: 0 });
      }
      return Promise.resolve({ stdout: "", stderr: "", code: 0 });
    },
  };

  const mockPi = {
    handlers: new Map<string, Array<(...args: unknown[]) => unknown>>(),
    on(event: string, handler: (...args: unknown[]) => unknown): void {
      if (!this.handlers.has(event)) this.handlers.set(event, []);
      this.handlers.get(event)!.push(handler);
    },
    async emit(event: string, data: unknown, ctxData?: unknown): Promise<void> {
      for (const h of this.handlers.get(event) || []) await h(data, ctxData);
    },
  };

  installSessionHooks(mockPi as unknown as import("../src/hooks/events.ts").Pi, give(env(runner)));

  // Emit an edit event with actual diff lines
  mockPi.emit("tool_result", {
    toolCallId: "call_edit_1",
    toolName: "edit",
    content: [{
      type: "text",
      text: "@@ -10,3 +10,4 @@\n- old_function_call();\n+ new_function_call();",
    }],
  });

  await mockPi.emit("agent_settled", {}, ctx);

  assert.notStrictEqual(storedPayload, "");
  const { envelope } = decodePayload(storedPayload.slice(storedPayload.indexOf(":") + 1));
  assert.strictEqual(envelope.level, "L0");
  assert.strictEqual(envelope.episode?.diffs?.[0]?.includes("new_function_call"), true);
  assert.strictEqual(envelope.episode?.environment?.runtime, "deno");
});

Deno.test("episode: extracts tool arguments from event.input and rejects false diffs", async () => {
  const { summarizeToolEvent } = await import("../src/hooks/events.ts");

  // 1. Tool result with event.input and ordinary success text (no diff header)
  const sig = summarizeToolEvent({
    toolCallId: "call_input_test",
    toolName: "edit",
    input: { path: "src/server.c", command: "replace" },
    content: [{ type: "text", text: "Successfully replaced 1 block(s) in src/server.c." }],
  });

  assert.notStrictEqual(sig, null);
  assert.deepStrictEqual(sig!.toolArgs, { path: "src/server.c", command: "replace" });
  // Diffs MUST NOT be inferred from an ordinary success message!
  assert.strictEqual(sig!.diffs, undefined);
});

Deno.test("episode: disk journal persists pending events across process restart and clears on flush", async () => {
  const { SessionEventBuffer } = await import("../src/hooks/events.ts");
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-journal-test-" });

  // 1. First process records an event and crashes before flush
  const proc1Buffer = new SessionEventBuffer();
  proc1Buffer.record({
    id: "evt_crash_recovery",
    timestamp: "2026-10-15T15:00:00Z",
    toolName: "bash",
    summary: "deno test: failed with syntax error",
    isError: true,
    approach: "deno test",
    outcome: "failed",
  });
  await proc1Buffer.saveJournal(tmpDir);

  // 2. Second fresh process starts (simulating process restart)
  const proc2Buffer = new SessionEventBuffer();
  assert.strictEqual(proc2Buffer.hasUnflushed(), false);

  // Restore journal from disk
  const recoveredCount = await proc2Buffer.restoreJournal(tmpDir);
  assert.strictEqual(recoveredCount, 1);
  assert.strictEqual(proc2Buffer.hasUnflushed(), true);

  const unflushed = proc2Buffer.getUnflushed();
  assert.strictEqual(unflushed[0].id, "evt_crash_recovery");

  // Mark flushed: journal should be pruned from disk
  proc2Buffer.markFlushed("fingerprint_done", ["evt_crash_recovery"]);
  await proc2Buffer.saveJournal(tmpDir);

  // 3. Third process checks journal is now empty
  const proc3Buffer = new SessionEventBuffer();
  const recoveredAfterFlush = await proc3Buffer.restoreJournal(tmpDir);
  assert.strictEqual(recoveredAfterFlush, 0);
});

Deno.test("episode: automatic journaling on normal Pi tool_result hook without manual saveJournal", async () => {
  const { installSessionHooks, activeSessionBuffer } = await import("../src/hooks/events.ts");
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-auto-journal-" });
  const journalFile = join(tmpDir, ".pending_events.json");

  activeSessionBuffer.clear();
  activeSessionBuffer.setJournalDir(tmpDir);

  const mockPi = {
    handlers: new Map<string, Array<(...args: unknown[]) => unknown>>(),
    on(event: string, handler: (...args: unknown[]) => unknown): void {
      if (!this.handlers.has(event)) this.handlers.set(event, []);
      this.handlers.get(event)!.push(handler);
    },
    emit(event: string, data: unknown): void {
      for (const h of this.handlers.get(event) || []) h(data);
    },
  };

  installSessionHooks(mockPi as unknown as import("../src/hooks/events.ts").Pi);

  // Normal tool_result hook execution
  mockPi.emit("tool_result", {
    toolCallId: "call_hook_auto_journal",
    toolName: "bash",
    isError: true,
    content: [{ type: "text", text: "FAIL: panic in buffer allocator" }],
  });

  // Small delay to allow async journal file write
  await new Promise((r) => setTimeout(r, 25));

  // Verify journal file was automatically written to disk!
  const journalExists = await (async () => {
    try {
      const s = await Deno.stat(journalFile);
      return s.isFile;
    } catch {
      return false;
    }
  })();
  assert.strictEqual(journalExists, true);

  const content = JSON.parse(await Deno.readTextFile(journalFile));
  assert.strictEqual(content.length, 1);
  assert.strictEqual(content[0].id, "call_hook_auto_journal");
});

Deno.test("episode: post-crash idempotency prunes already-committed event IDs", async () => {
  const { SessionEventBuffer } = await import("../src/hooks/events.ts");
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-idempotency-" });

  const buf1 = new SessionEventBuffer();
  buf1.setJournalDir(tmpDir);
  buf1.record({
    id: "evt_committed_1",
    timestamp: "2026-10-15T15:00:00Z",
    toolName: "bash",
    summary: "build passed",
    isError: false,
    approach: "cargo build",
    outcome: "worked",
  });
  buf1.record({
    id: "evt_uncommitted_2",
    timestamp: "2026-10-15T15:01:00Z",
    toolName: "bash",
    summary: "test failed",
    isError: true,
    approach: "cargo test",
    outcome: "failed",
  });
  await buf1.saveJournal(tmpDir);

  // Process restarts. "evt_committed_1" is found in mem.db
  const existingCommitted = new Set<string>(["evt_committed_1"]);

  const buf2 = new SessionEventBuffer();
  const restored = await buf2.restoreJournal(tmpDir, existingCommitted);

  // Only the uncommitted event should be restored
  assert.strictEqual(restored, 1);
  const unflushed = buf2.getUnflushed();
  assert.strictEqual(unflushed.length, 1);
  assert.strictEqual(unflushed[0].id, "evt_uncommitted_2");
});

Deno.test("episode: crash recovery across fresh spawned OS process", async () => {
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-os-proc-recovery-" });
  const journalFile = join(tmpDir, ".pending_events.json");

  // Write a pending event journal from a crashed process
  const pendingEvents = [{
    id: "evt_from_crashed_proc",
    timestamp: "2026-10-15T16:00:00Z",
    toolName: "bash",
    summary: "make: *** [test] Error 2",
    isError: true,
    approach: "make test",
    outcome: "failed",
  }];
  await Deno.writeTextFile(journalFile, JSON.stringify(pendingEvents));

  const cwd = Deno.cwd();
  const eventsModule = cwd.includes(".pi/extensions/pi-mm")
    ? `${cwd}/src/hooks/events.ts`
    : `${cwd}/.pi/extensions/pi-mm/src/hooks/events.ts`;

  const workerScript = `
    import { SessionEventBuffer } from "${eventsModule}";

    const dir = Deno.args[0];
    const buf = new SessionEventBuffer();
    const restored = await buf.restoreJournal(dir);
    if (restored !== 1) {
      console.error("Expected 1 restored event, got " + restored);
      Deno.exit(1);
    }
    const unflushed = buf.getUnflushed();
    if (unflushed[0].id !== "evt_from_crashed_proc") {
      console.error("Unexpected id " + unflushed[0].id);
      Deno.exit(2);
    }
    buf.markFlushed("recovered_fp", ["evt_from_crashed_proc"]);
    await buf.saveJournal(dir);
    Deno.exit(0);
  `;

  const scriptPath = join(tmpDir, "recover_worker.ts");
  await Deno.writeTextFile(scriptPath, workerScript);

  const cmd = new Deno.Command(Deno.execPath(), {
    args: ["run", "--allow-all", "--sloppy-imports", scriptPath, tmpDir],
    stdout: "piped",
    stderr: "piped",
  });
  const output = await cmd.output();
  assert.strictEqual(output.code, 0, new TextDecoder().decode(output.stderr));

  // Verify journal was cleared by the recovered process
  const journalRemaining = await (async () => {
    try {
      const s = await Deno.stat(journalFile);
      return s.isFile;
    } catch {
      return false;
    }
  })();
  assert.strictEqual(journalRemaining, false);
});

Deno.test("episode: mid-flush event arrival is isolated and not marked as flushed", async () => {
  const { SessionEventBuffer } = await import("../src/hooks/events.ts");
  const buf = new SessionEventBuffer();

  buf.record({
    id: "evt_batch_1",
    timestamp: "2026-10-15T10:00:00Z",
    toolName: "bash",
    summary: "initial test",
    isError: false,
  });

  // Begin flush: batch 1 is in-flight
  const batch = buf.beginFlush()!;
  assert.strictEqual(batch.length, 1);
  assert.strictEqual(batch[0].id, "evt_batch_1");

  // An event arrives while the write is in flight
  buf.record({
    id: "evt_mid_flight_2",
    timestamp: "2026-10-15T10:00:01Z",
    toolName: "edit",
    summary: "file modified mid-flight",
    isError: false,
  });

  // Complete batch 1 flush
  buf.commitFlush(batch.map((e) => e.id));

  // The event that arrived mid-flight MUST NOT be marked flushed!
  assert.strictEqual(buf.hasUnflushed(), true);
  const remaining = buf.getUnflushed();
  assert.strictEqual(remaining.length, 1);
  assert.strictEqual(remaining[0].id, "evt_mid_flight_2");
});

Deno.test("episode: simultaneous flush calls are protected against duplicate commits", async () => {
  const { SessionEventBuffer } = await import("../src/hooks/events.ts");
  const buf = new SessionEventBuffer();

  buf.record({
    id: "evt_concurrent_1",
    timestamp: "2026-10-15T11:00:00Z",
    toolName: "bash",
    summary: "concurrent test",
    isError: false,
  });

  const firstBatch = buf.beginFlush();
  assert.notStrictEqual(firstBatch, null);

  // A concurrent flush call occurs before firstBatch finishes
  const secondBatch = buf.beginFlush();
  // Second flush MUST be rejected while first is in-flight
  assert.strictEqual(secondBatch, null);

  buf.commitFlush(firstBatch!.map((e) => e.id));
  assert.strictEqual(buf.hasUnflushed(), false);
});

Deno.test("episode: execution outcome distinguishes ordinary tool returns from verified test success", async () => {
  const { summarizeToolEvent } = await import("../src/hooks/events.ts");

  // 1. Ordinary read/bash tool invocation that completed without error
  const ordinaryEvent = summarizeToolEvent({
    toolName: "bash",
    content: [{ type: "text", text: "total 12\n-rw-r--r-- 1 user user 1024 Oct 15 server.c" }],
    isError: false,
  });
  assert.notStrictEqual(ordinaryEvent, null);
  // Outcome MUST be undefined (executed/observed), NOT verified "worked"!
  assert.strictEqual(ordinaryEvent!.outcome, undefined);

  // 2. Test execution that explicitly passed
  const testPassEvent = summarizeToolEvent({
    toolName: "bash",
    content: [{ type: "text", text: "ok 1 - all green 0 failed (12ms)" }],
    isError: false,
  });
  assert.notStrictEqual(testPassEvent, null);
  // Outcome MUST be verified "worked"
  assert.strictEqual(testPassEvent!.outcome, "worked");

  // 3. Test execution that failed
  const testFailEvent = summarizeToolEvent({
    toolName: "bash",
    content: [{ type: "text", text: "FAIL: 1 failed in suite" }],
    isError: true,
  });
  assert.notStrictEqual(testFailEvent, null);
  assert.strictEqual(testFailEvent!.outcome, "failed");
});

Deno.test("episode: journal write queue recovers after failed write and exposes lastError", async () => {
  const { SessionEventBuffer } = await import("../src/hooks/events.ts");
  const buf = new SessionEventBuffer();

  // Create an invalid path that cannot be written to
  const tmpFile = await Deno.makeTempFile({ prefix: "mm-journal-bad-" });
  const badDir = join(tmpFile, "cannot_create_dir");

  buf.record({
    id: "evt_fail_journal",
    timestamp: "2026-10-15T12:00:00Z",
    toolName: "bash",
    summary: "fail test",
    isError: false,
  });

  // Attempt save to bad directory: must fail safely and populate lastError
  await buf.saveJournal(badDir);
  assert.notStrictEqual(buf.lastError, null);
  assert.strictEqual(buf.lastError!.includes("journal-error:"), true);

  // Now save to a valid directory: queue MUST recover and succeed
  const goodDir = await Deno.makeTempDir({ prefix: "mm-journal-good-" });
  await buf.saveJournal(goodDir);
  assert.strictEqual(buf.lastError, null);

  const savedContent = JSON.parse(await Deno.readTextFile(join(goodDir, ".pending_events.json")));
  assert.strictEqual(savedContent.length, 1);
  assert.strictEqual(savedContent[0].id, "evt_fail_journal");
});

Deno.test("episode: concurrent journal updates result in latest unflushed event state", async () => {
  const { SessionEventBuffer } = await import("../src/hooks/events.ts");
  const tmpDir = await Deno.makeTempDir({ prefix: "mm-concurrent-journal-" });
  const buf = new SessionEventBuffer();
  buf.setJournalDir(tmpDir);

  // Issue 5 rapid updates concurrently
  for (let i = 1; i <= 5; i++) {
    buf.record({
      id: `evt_rapid_${i}`,
      timestamp: `2026-10-15T12:0${i}:00Z`,
      toolName: "edit",
      summary: `edit step ${i}`,
      isError: false,
    });
  }

  // Await the journal queue to settle
  await buf.saveJournal(tmpDir);

  const saved = JSON.parse(await Deno.readTextFile(join(tmpDir, ".pending_events.json")));
  assert.strictEqual(saved.length, 5);
  assert.strictEqual(saved[4].id, "evt_rapid_5");
});

Deno.test("episode: session_start during in-flight flush preserves flush guard and avoids duplicate commit", async () => {
  const { flushSessionBuffer } = await import("../src/hooks/events.ts");

  const committedBatches: string[][] = [];
  const fakeStore = new Map<number, string>();
  let nextId = 1;

  let releaseStoreDelay: () => void = () => {};
  const storeDelayPromise = new Promise<void>((resolve) => {
    releaseStoreDelay = resolve;
  });

  const slowRunner: import("../src/qmap.ts").QmapRunner = {
    async run(inv: import("../src/qmap.ts").Invocation) {
      if (inv.args[0] === "-g" && inv.args[1] === ".") {
        const refs = [...fakeStore.keys()].join("\n");
        return { stdout: refs ? refs + "\n" : "", stderr: "", code: 0 };
      }
      if (inv.args[0] === "-p") {
        // Pause inside store operation
        await storeDelayPromise;
        const ref = nextId++;
        const payload = inv.args[1];
        fakeStore.set(ref, payload);
        const { decodePayload } = await import("../src/payload.ts");
        const { envelope } = decodePayload(payload.slice(payload.indexOf(":") + 1));
        if (envelope.episode?.eventIds) {
          committedBatches.push(envelope.episode.eventIds);
        }
        return { stdout: "", stderr: "", code: 0 };
      }
      return { stdout: "", stderr: "", code: 0 };
    },
  };

  const { DEFAULT_CONFIG } = await import("../src/config.ts");
  const envSource = () => Promise.resolve({
    cfg: { ...DEFAULT_CONFIG, qmapBin: "/fake/qmap" },
    runner: slowRunner,
    nowProvider: () => new Date("2026-10-15T12:00:00Z"),
    cwd: "/tmp",
  });

  const { activeSessionBuffer } = await import("../src/hooks/events.ts");
  activeSessionBuffer.clear();

  // 1. Record event 1
  activeSessionBuffer.record({
    id: "evt_in_flight_1",
    timestamp: "2026-10-15T12:00:00Z",
    toolName: "bash",
    summary: "first step",
    isError: false,
  });

  // 2. Start flush (now paused inside slowRunner)
  const flushPromise = flushSessionBuffer(envSource, { cwd: "/tmp", hasUI: false, mode: "test" });

  // 3. session_start fires while store is still in flight!
  activeSessionBuffer.onSessionStart();

  // 4. A new event arrives
  activeSessionBuffer.record({
    id: "evt_new_step_2",
    timestamp: "2026-10-15T12:00:01Z",
    toolName: "edit",
    summary: "second step",
    isError: false,
  });

  // 5. Release slow store operation
  releaseStoreDelay();
  await flushPromise;

  // 6. Another settle flush fires
  await flushSessionBuffer(envSource, { cwd: "/tmp", hasUI: false, mode: "test" });

  // Each event batch was persisted separately, exactly once!
  assert.strictEqual(committedBatches.length, 2);
  assert.deepStrictEqual(committedBatches[0], ["evt_in_flight_1"]);
  assert.deepStrictEqual(committedBatches[1], ["evt_new_step_2"]);
});
