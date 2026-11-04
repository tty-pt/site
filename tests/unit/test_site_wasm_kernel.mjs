import test from "node:test";
import assert from "node:assert/strict";
import fs from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __dirname = path.dirname(fileURLToPath(import.meta.url));

test("Unified site.wasm boots and executes multi-slot operations across site_chrome, song_detail, gig_detail, chat_fe, and list_fe", async () => {
  const wasmPath = path.resolve(__dirname, "../../htdocs/site.wasm");
  assert.ok(fs.existsSync(wasmPath), `htdocs/site.wasm must exist at ${wasmPath}`);

  const emittedPatches = [];
  const logMessages = [];

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
    stele: {
      stl_host_emit_patch: (slot, opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen) => {
        const op = decodeString(memoryRef, opPtr, opLen);
        const a = decodeString(memoryRef, aPtr, aLen);
        const b = decodeString(memoryRef, bPtr, bLen);
        const c = decodeString(memoryRef, cPtr, cLen);
        emittedPatches.push({ slot, op, a, b, c });
      },
      stl_host_log: (ptr, len) => {
        logMessages.push(decodeString(memoryRef, ptr, len));
      },
      stl_host_fetch: () => {},
      stl_host_set_location: () => {},
      stl_host_invalidate: () => {},
      stele_host_emit_patch: (slot, opPtr, opLen, aPtr, aLen, bPtr, bLen, cPtr, cLen) => {
        const op = decodeString(memoryRef, opPtr, opLen);
        const a = decodeString(memoryRef, aPtr, aLen);
        const b = decodeString(memoryRef, bPtr, bLen);
        const c = decodeString(memoryRef, cPtr, cLen);
        emittedPatches.push({ slot, op, a, b, c });
      },
      stele_host_log: (ptr, len) => {
        logMessages.push(decodeString(memoryRef, ptr, len));
      },
      stele_host_fetch: () => {},
      stele_host_set_location: () => {},
      stele_host_invalidate: () => {},
    },
    xylem: {
      wasm_xy_load_module: () => 0,
      wasm_xy_unload_module: () => 0,
    },
    audio: {
      stl_audio_play: () => 0,
      stl_audio_pause: () => 0,
      stl_audio_seek: () => 0,
      stl_audio_get_time: () => 0,
    },
    storage: {
      stl_storage_get: () => 0,
      stl_storage_set: () => 0,
    },
    canvas: {
      stl_canvas_blit: () => 0,
    },
    clipboard: {
      stl_clipboard_copy: () => 0,
    },
    wasi_snapshot_preview1: new Proxy({}, { get: () => () => 0 }),
  };

  const wasmBuffer = fs.readFileSync(wasmPath);
  const wasmModule = new WebAssembly.Module(wasmBuffer);
  const instance = new WebAssembly.Instance(wasmModule, imports);
  memoryRef = instance.exports.memory;

  // 1. Verify kernel ABI export surface
  const kernelBoot = instance.exports.stl_kernel_boot || instance.exports.stele_kernel_boot;
  const kernelMount = instance.exports.stl_kernel_mount || instance.exports.stele_kernel_mount;
  const kernelUnmount = instance.exports.stl_kernel_unmount || instance.exports.stele_kernel_unmount;
  const kernelAction = instance.exports.stl_kernel_action || instance.exports.stele_kernel_action;
  const kernelVersion = instance.exports.stl_kernel_version || instance.exports.stele_kernel_version;

  assert.ok(typeof kernelBoot === "function", "stl_kernel_boot must be exported");
  assert.ok(typeof kernelMount === "function", "stl_kernel_mount must be exported");
  assert.ok(typeof kernelUnmount === "function", "stl_kernel_unmount must be exported");
  assert.ok(typeof kernelAction === "function", "stl_kernel_action must be exported");
  assert.ok(typeof kernelVersion === "function", "stl_kernel_version must be exported");

  assert.equal(kernelVersion(), 2n);
  assert.equal(kernelBoot(), 0);

  // Helper mount function
  function mount(slot, moduleName, jsonStr) {
    const [namePtr] = encodeString(instance, memoryRef, moduleName);
    const [jsonPtr, jsonLen] = encodeString(instance, memoryRef, jsonStr);
    const rc = kernelMount(slot, namePtr, jsonPtr, jsonLen);
    instance.exports.free(namePtr);
    instance.exports.free(jsonPtr);
    return rc;
  }

  // 2. Mount Slot 0: site_chrome
  const chromeRc = mount(0, "site_chrome", JSON.stringify({
    title: "Site Title",
    path: "/song/1",
    icon: "music",
    user: "alice",
    lang: "en"
  }));
  assert.equal(chromeRc, 0, "site_chrome must mount cleanly in slot 0");

  // 3. Mount Slot 1: song_detail
  const songRc = mount(1, "song_detail", JSON.stringify({
    id: "song_1",
    title: "Amazing Grace",
    transpose: 0,
    zoom: 100,
    use_latin: 0
  }));
  assert.equal(songRc, 0, "song_detail must mount cleanly in slot 1");

  // 4. Soft-navigation in Slot 1: Swap to gig_detail without touching Slot 0
  const gigRc = mount(1, "gig_detail", JSON.stringify({
    sb_id: "gig_42",
    title: "Concert in Lisbon",
    zoom: 100,
    latin: 0,
    songs: []
  }));
  assert.equal(gigRc, 0, "gig_detail must mount cleanly in slot 1");

  // 5. Soft-navigation in Slot 1: Swap to chat_fe
  const chatRc = mount(1, "chat_fe", JSON.stringify({
    session: { id: "sess_99" },
    summaries: [],
    user: "bob",
    error: ""
  }));
  assert.equal(chatRc, 0, "chat_fe must mount cleanly in slot 1");

  // 6. Soft-navigation in Slot 1: Swap to list_fe
  const listRc = mount(1, "list_fe", JSON.stringify({
    module: "song",
    q: "grace",
    total: 5
  }));
  assert.equal(listRc, 0, "list_fe must mount cleanly in slot 1");

  // 7. Verify Slot 0 (site_chrome) remains active and receives actions
  const [dataPtr] = encodeString(instance, memoryRef, "test");
  const actionRc = kernelAction(0, 1, dataPtr);
  instance.exports.free(dataPtr);
  assert.equal(actionRc, 0, "site_chrome in slot 0 must receive action cleanly");

  // 8. Test clean unmounting
  assert.equal(kernelUnmount(1), 0, "Slot 1 unmount succeeds");
  assert.equal(kernelUnmount(0), 0, "Slot 0 unmount succeeds");
});
