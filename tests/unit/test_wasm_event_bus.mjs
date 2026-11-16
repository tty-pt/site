import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

function decodeString(memory, ptr, len) {
  if (!ptr || len <= 0) return "";
  const buf = new Uint8Array(memory.buffer, ptr, len);
  return new TextDecoder("utf-8").decode(buf);
}

function encodeString(instance, memory, str) {
  const encoded = new TextEncoder().encode(str);
  const ptr = instance.exports.malloc(encoded.length + 1);
  const view = new Uint8Array(memory.buffer, ptr, encoded.length + 1);
  view.set(encoded);
  view[encoded.length] = 0;
  return [ptr, encoded.length];
}

test("In-WASM event bus executes local listener dispatch within unified site.wasm kernel", async () => {
  const wasmPath = path.resolve(__dirname, "../../htdocs/site.wasm");
  assert.ok(fs.existsSync(wasmPath), `htdocs/site.wasm must exist at ${wasmPath}`);

  const hostDispatchedEvents = [];
  let memoryRef = null;

  const imports = {
    env: new Proxy({
      __main_argc_argv: () => 0,
    }, {
      get: (target, prop) => {
        if (prop in target) return target[prop];
        return () => 0;
      }
    }),
    bud: {
      bud_host_emit_patch: () => {},
      bud_host_log: () => {},
      bud_host_fetch: () => {},
      bud_host_set_location: () => {},
      bud_host_mark_dirty: () => {},
      bud_host_flush: () => {},
    },
    xylem: new Proxy({}, { get: () => () => 0 }),
    stele: {
      stl_host_emit_patch: () => {},
      stl_host_log: () => {},
      stl_host_fetch: () => {},
      stl_host_set_location: () => {},
      stl_host_invalidate: () => {},
      stl_host_event_emit: (tPtr, tLen, pPtr, pLen) => {
        const topic = decodeString(memoryRef, tPtr, tLen);
        const payload = decodeString(memoryRef, pPtr, pLen);
        hostDispatchedEvents.push({ topic, payload });
      },
      stele_host_emit_patch: () => {},
      stele_host_log: () => {},
      stele_host_fetch: () => {},
      stele_host_set_location: () => {},
      stele_host_invalidate: () => {},
      stele_host_event_emit: (tPtr, tLen, pPtr, pLen) => {
        const topic = decodeString(memoryRef, tPtr, tLen);
        const payload = decodeString(memoryRef, pPtr, pLen);
        hostDispatchedEvents.push({ topic, payload });
      }
    },
    wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 })
  };

  const wasmBuffer = fs.readFileSync(wasmPath);
  const { instance } = await WebAssembly.instantiate(wasmBuffer, imports);
  memoryRef = instance.exports.memory;

  assert.ok(typeof instance.exports.stl_event_emit === "function", "stl_event_emit must be exported");
  assert.ok(typeof instance.exports.stl_on_event === "function", "stl_on_event must be exported");

  // Emit event from inside WASM
  const [topicPtr, topicLen] = encodeString(instance, memoryRef, "player:track_change");
  const [payloadPtr, payloadLen] = encodeString(instance, memoryRef, '{"track":"O Barquinho","artist":"Nara Leao"}');

  const rc = instance.exports.stl_event_emit(topicPtr, payloadPtr, payloadLen);
  assert.equal(rc, 0, "stl_event_emit must return 0 on success");

  // Verify host received the broadcast
  assert.equal(hostDispatchedEvents.length, 1);
  assert.equal(hostDispatchedEvents[0].topic, "player:track_change");
  assert.equal(hostDispatchedEvents[0].payload, '{"track":"O Barquinho","artist":"Nara Leao"}');

  // Verify external host can deliver an event into WASM via stl_on_event
  const [inTopicPtr, inTopicLen] = encodeString(instance, memoryRef, "chat:unread_badge");
  const [inPayloadPtr, inPayloadLen] = encodeString(instance, memoryRef, '{"count":3}');

  const inRc = instance.exports.stl_on_event(inTopicPtr, inTopicLen, inPayloadPtr, inPayloadLen);
  assert.equal(inRc, 0, "stl_on_event must return 0 on success");
});

test("Cross-slot event bus bridges modular WASMs (site_chrome.wasm <-> song.wasm) via SteleWasmClient", async () => {
  const chromeWasmPath = path.resolve(__dirname, "../../htdocs/site_chrome.wasm");
  const songWasmPath = path.resolve(__dirname, "../../htdocs/song.wasm");

  assert.ok(fs.existsSync(chromeWasmPath), "htdocs/site_chrome.wasm must exist");
  assert.ok(fs.existsSync(songWasmPath), "htdocs/song.wasm must exist");

  const { SteleWasmClient } = await import("../../htdocs/stele-client.js");
  const client = new SteleWasmClient();

  const capturedEvents = [];
  client.on("audio:play", (topic, payload) => {
    capturedEvents.push({ topic, payloadText: new TextDecoder().decode(payload) });
  });

  // Mount slot 0 (chrome) and slot 1 (song)
  // Mock DOM container
  globalThis.document = {
    getElementById: () => null,
    querySelector: () => null,
    addEventListener: () => {}
  };

  const chromeImports = client.buildSlotImports(0);
  const songImports = client.buildSlotImports(1);

  const chromeBuf = fs.readFileSync(chromeWasmPath);
  const songBuf = fs.readFileSync(songWasmPath);

  const chromeInst = (await WebAssembly.instantiate(chromeBuf, chromeImports)).instance;
  const songInst = (await WebAssembly.instantiate(songBuf, songImports)).instance;

  client.slotInstances.set(0, { instance: chromeInst, memory: chromeInst.exports.memory || client.sharedMemory, moduleName: "site_chrome" });
  client.slotInstances.set(1, { instance: songInst, memory: songInst.exports.memory || client.sharedMemory, moduleName: "song" });

  // Slot 1 (song) emits "audio:play"
  const [tPtr, tLen] = client.encodeString(songInst, songInst.exports.memory || client.sharedMemory, "audio:play");
  const [pPtr, pLen] = client.encodeString(songInst, songInst.exports.memory || client.sharedMemory, '{"songId":"s1","title":"Chega de Saudade"}');

  songInst.exports.stl_event_emit(tPtr, pPtr, pLen);

  // Host listener should have received event
  assert.equal(capturedEvents.length, 1);
  assert.equal(capturedEvents[0].topic, "audio:play");
  assert.ok(capturedEvents[0].payloadText.includes("Chega de Saudade"));
});
