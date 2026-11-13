import test from "node:test";
import assert from "node:assert/strict";

test("stele-client: on-demand lazy WASM module streaming", async () => {
  // Mock minimal browser environment
  class MockElement {
    constructor(id = "") {
      this.id = id;
      this.listeners = new Map();
      this.attributes = new Map();
    }
    addEventListener(event, handler, options) {
      if (!this.listeners.has(event)) this.listeners.set(event, []);
      this.listeners.get(event).push({ handler, options });
    }
    removeEventListener(event, handler) {
      if (!this.listeners.has(event)) return;
      const filtered = this.listeners.get(event).filter((l) => l.handler !== handler);
      this.listeners.set(event, filtered);
    }
    dispatchEvent(event) {
      const list = this.listeners.get(event.type) || [];
      for (const { handler } of [...list]) {
        handler(event);
      }
    }
    getAttribute(name) {
      return this.attributes.get(name) || null;
    }
    setAttribute(name, value) {
      this.attributes.set(name, value);
    }
    querySelector() {
      return null;
    }
    querySelectorAll() {
      return [];
    }
  }

  const mockTarget = new MockElement("lazy-widget");
  const mockDoc = {
    addEventListener: () => {},
    removeEventListener: () => {},
    getElementById: (id) => (id === "lazy-widget" ? mockTarget : null),
    querySelector: (sel) => (sel === "#lazy-widget" ? mockTarget : null),
    querySelectorAll: () => [],
    body: new MockElement("body"),
  };

  globalThis.document = mockDoc;
  globalThis.window = {
    addEventListener: () => {},
    removeEventListener: () => {},
  };

  const { SteleWasmClient } = await import("../../htdocs/stele-client.js");
  const client = new SteleWasmClient();

  // Mock mountSlot to record invocation
  let mountedSlot = null;
  let mountedModule = null;
  client.mountSlot = async (slot, modName) => {
    mountedSlot = slot;
    mountedModule = modName;
    client.slotInstances.set(slot, { instance: {}, memory: client.sharedMemory, moduleName: modName });
    return true;
  };

  // 1. Interaction trigger
  client.lazyMountSlot(5, "analytics_viz", mockTarget, { trigger: "interaction" });
  assert.equal(mountedSlot, null, "Module should not mount eagerly");
  assert.equal(client.slotInstances.has(5), false);

  // Trigger interaction event
  mockTarget.dispatchEvent({ type: "click" });
  // Allow microtasks to resolve
  await new Promise((r) => setTimeout(r, 10));

  assert.equal(mountedSlot, 5, "Module should mount after click");
  assert.equal(mountedModule, "analytics_viz");
  assert.equal(client.slotInstances.has(5), true);

  // 2. Manual trigger
  let manualSlot = null;
  client.mountSlot = async (slot, modName) => {
    manualSlot = slot;
    client.slotInstances.set(slot, { instance: {}, memory: client.sharedMemory, moduleName: modName });
    return true;
  };

  const res = await client.lazyMountSlot(6, "chat_drawer", null, { trigger: "manual" });
  assert.equal(res, true);
  assert.equal(manualSlot, 6);
  assert.equal(client.slotInstances.has(6), true);
});
