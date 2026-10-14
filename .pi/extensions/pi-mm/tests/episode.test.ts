import assert from "node:assert";
import { makeStoreTool } from "../src/tools/store.ts";
import { SessionEventBuffer } from "../src/hooks/events.ts";
import { fakeRunner, runTool, okResult } from "./fake-qmap.ts";
import { DEFAULT_CONFIG, type MmConfig } from "../src/config.ts";
import { decodePayload } from "../src/payload.ts";
import { give, type ToolEnv } from "../src/tools/index.ts";
import type { QmapRunner } from "../src/qmap.ts";

const CWD = "/repo";
const ctx = { cwd: CWD };

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
