import test from "node:test";
import assert from "node:assert/strict";

test("stele-client: extractElementState detects JSON vs Base64 binary state", async () => {
  const { SteleWasmClient } = await import("../../htdocs/stele-client.js");
  const client = new SteleWasmClient();

  // 1. Standard JSON script element
  const jsonEl = {
    getAttribute(name) {
      if (name === "type") return "application/json";
      return null;
    },
    hasAttribute() { return false; },
    textContent: '{"view":"list","count":42}',
  };
  const jsonState = client.extractElementState(jsonEl);
  assert.equal(typeof jsonState, "string");
  assert.equal(jsonState, '{"view":"list","count":42}');

  // 2. Binary octet-stream script element with Base64 encoding
  const sampleBytes = new Uint8Array([0x42, 0x55, 0x44, 0x31, 0x01, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00]);
  const b64 = Buffer.from(sampleBytes).toString("base64");

  const binEl = {
    getAttribute(name) {
      if (name === "type") return "application/octet-stream";
      if (name === "data-encoding") return "base64";
      return null;
    },
    hasAttribute(name) { return name === "data-encoding"; },
    textContent: b64,
  };
  const binState = client.extractElementState(binEl);
  assert.ok(binState instanceof Uint8Array);
  assert.deepEqual(Array.from(binState), Array.from(sampleBytes));

  // 3. Element with data-state-bin attribute
  const attrEl = {
    getAttribute(name) {
      if (name === "data-state-bin") return b64;
      return null;
    },
    hasAttribute(name) { return name === "data-state-bin"; },
    textContent: "",
  };
  const attrState = client.extractElementState(attrEl);
  assert.ok(attrState instanceof Uint8Array);
  assert.deepEqual(Array.from(attrState), Array.from(sampleBytes));
});

test("stele-client: mountSlot routes binary payload to stl_kernel_mount_bin", async () => {
  const { SteleWasmClient } = await import("../../htdocs/stele-client.js");
  const client = new SteleWasmClient();

  // Mock WASM kernel instance with malloc, free, stl_kernel_mount, and stl_kernel_mount_bin
  let mountedJson = null;
  let mountedBin = null;
  let mountedSlot = null;
  let mountedModule = null;

  const mockInstance = {
    exports: {
      malloc: (sz) => 1024,
      free: (ptr) => {},
      stl_kernel_mount: (slot, namePtr, jsonPtr, len) => {
        mountedSlot = slot;
        mountedJson = true;
        return 0;
      },
      stl_kernel_mount_bin: (slot, namePtr, binPtr, len) => {
        mountedSlot = slot;
        mountedBin = true;
        return 0;
      },
    },
  };

  client.instance = mockInstance;
  client.sharedMemory = { buffer: new ArrayBuffer(65536) };
  client.memory = client.sharedMemory;

  // 1. Mount JSON payload -> invokes stl_kernel_mount
  mountedJson = false;
  mountedBin = false;
  const jsonSuccess = await client.mountSlot(1, "list_fe", { type: "json", data: '{"filter":"all"}' });
  assert.equal(jsonSuccess, true);
  assert.equal(mountedJson, true);
  assert.equal(mountedBin, false);

  // 2. Mount binary payload -> invokes stl_kernel_mount_bin
  mountedJson = false;
  mountedBin = false;
  const rawBytes = new Uint8Array([0x42, 0x55, 0x44, 0x31, 0x10, 0x20]);
  const binSuccess = await client.mountSlot(1, "list_fe", { type: "bin", data: rawBytes });
  assert.equal(binSuccess, true);
  assert.equal(mountedJson, false);
  assert.equal(mountedBin, true);
});

test("stele-client: bindSlots resolves meta[name=state-src] via fetch into Uint8Array binary state", async () => {
  const { SteleWasmClient } = await import("../../htdocs/stele-client.js");
  const client = new SteleWasmClient();

  const sampleBytes = new Uint8Array([0x48, 0x59, 0x4C, 0x54, 0x01, 0x00]);

  // Mock global document, Node, and fetch
  const origDoc = globalThis.document;
  const origFetch = globalThis.fetch;
  const origNode = globalThis.Node;

  globalThis.Node = { ELEMENT_NODE: 1, TEXT_NODE: 3 };

  let fetchedUrl = null;
  globalThis.fetch = async (url) => {
    fetchedUrl = url;
    return {
      ok: true,
      arrayBuffer: async () => sampleBytes.buffer.slice(0),
    };
  };

  let mountedState = null;
  client.mountSlot = async (slot, mod, state) => {
    mountedState = state;
    return true;
  };

  globalThis.document = {
    body: {
      getAttribute(name) {
        if (name === "data-modules") return "list_fe";
        return null;
      },
      setAttribute() {},
    },
    getElementById(id) {
      if (id === "page-root") return { getAttribute: () => "list_fe" };
      return null;
    },
    querySelector(selector) {
      if (selector && selector.includes('[data-slot="1"]')) {
        return { getAttribute: (attr) => attr === "data-module" ? "list_fe" : null };
      }
      if (selector === 'meta[name="state-src"]' || selector === 'meta[name="page-state-src"]') {
        return { getAttribute: (attr) => attr === "content" ? "/song/?format=bin" : null };
      }
      return null;
    },
    querySelectorAll() { return []; },
  };

  try {
    const bound = await client.bindSlots();
    assert.equal(bound, true);
    assert.equal(fetchedUrl, "/song/?format=bin");
    assert.ok(mountedState instanceof Uint8Array);
    assert.deepEqual(Array.from(mountedState), Array.from(sampleBytes));
  } finally {
    globalThis.document = origDoc;
    globalThis.fetch = origFetch;
    globalThis.Node = origNode;
  }
});
