import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

test("WASM exports stl_optimistic_update and stl_optimistic_rollback across modular and kernel WASMs", async () => {
  const siteWasmPath = path.resolve(__dirname, "../../htdocs/site.wasm");
  const songWasmPath = path.resolve(__dirname, "../../htdocs/song.wasm");

  const imports = {
    env: new Proxy({}, { get: () => () => 0 }),
    bud: new Proxy({}, { get: () => () => 0 }),
    stele: new Proxy({}, { get: () => () => 0 }),
    xylem: new Proxy({}, { get: () => () => 0 }),
    wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
  };

  const siteInst = (await WebAssembly.instantiate(fs.readFileSync(siteWasmPath), imports)).instance;
  const songInst = (await WebAssembly.instantiate(fs.readFileSync(songWasmPath), imports)).instance;

  assert.ok(typeof siteInst.exports.stl_optimistic_update === "function", "site.wasm must export stl_optimistic_update");
  assert.ok(typeof siteInst.exports.stl_optimistic_rollback === "function", "site.wasm must export stl_optimistic_rollback");
  assert.ok(typeof songInst.exports.stl_optimistic_update === "function", "song.wasm must export stl_optimistic_update");
  assert.ok(typeof songInst.exports.stl_optimistic_rollback === "function", "song.wasm must export stl_optimistic_rollback");
});

test("SteleWasmClient intercepts optimistic form submission, updates state, and rolls back on HTTP error", async () => {
  const { SteleWasmClient } = await import("../../htdocs/stele-client.js");
  const client = new SteleWasmClient();

  const events = [];
  client.on("stl:mutate", (topic, payload) => events.push({ type: "mutate", payload }));
  client.on("stl:rollback", (topic, payload) => events.push({ type: "rollback", payload }));

  let updateCalled = false;
  let rollbackCalled = false;

  const mockInstance = {
    exports: {
      malloc: (sz) => 1024,
      free: () => {},
      stl_optimistic_update: (dPtr, iPtr, pPtr) => {
        updateCalled = true;
        return 0;
      },
      stl_optimistic_rollback: (dPtr, iPtr) => {
        rollbackCalled = true;
        return 0;
      },
      stl_kernel_update: () => 0
    }
  };

  client.slotInstances.set(1, {
    instance: mockInstance,
    memory: new WebAssembly.Memory({ initial: 1 }),
    moduleName: "song"
  });

  // Setup DOM mock and event simulation
  let submitListener = null;
  globalThis.document = {
    addEventListener: (type, fn) => {
      if (type === "submit") submitListener = fn;
    }
  };

  client.attachEventDelegation();
  assert.ok(submitListener, "submit listener must be attached");

  // Mock global fetch to fail with HTTP 500
  globalThis.fetch = async () => ({
    ok: false,
    status: 500
  });

  // Mock a form event
  const mockForm = {
    tagName: "FORM",
    getAttribute: (attr) => {
      if (attr === "method") return "POST";
      if (attr === "action") return "/api/song/edit";
      if (attr === "data-dataset") return "song";
      if (attr === "data-item") return "song_42";
      return null;
    },
    hasAttribute: (attr) => attr === "data-dataset",
    closest: () => ({ getAttribute: () => "1" })
  };

  globalThis.FormData = class {
    entries() {
      return [["title", "Updated Song Title"]];
    }
  };

  let prevented = false;
  const mockEvent = {
    target: mockForm,
    preventDefault: () => { prevented = true; }
  };

  await submitListener(mockEvent);

  assert.equal(prevented, true, "Form default submission must be prevented");
  assert.equal(updateCalled, true, "stl_optimistic_update must be called immediately");
  assert.equal(rollbackCalled, true, "stl_optimistic_rollback must be called on HTTP failure");
  assert.equal(events.length, 2, "Must emit mutate followed by rollback event");
  assert.equal(events[0].type, "mutate");
  assert.equal(events[1].type, "rollback");
});
