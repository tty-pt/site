import test from "node:test";
import assert from "node:assert/strict";

test("Universal WebAssembly runtime executes across all 6 libraries with strictly ONE instantiation", async () => {
  // 1. Instrument WebAssembly instantiation
  let instantiationCount = 0;
  const OriginalInstance = WebAssembly.Instance;
  WebAssembly.Instance = function(...args) {
    instantiationCount++;
    return new OriginalInstance(...args);
  };
  const originalInstantiate = WebAssembly.instantiate;
  WebAssembly.instantiate = async function(...args) {
    instantiationCount++;
    return originalInstantiate.apply(this, args);
  };

  // 2. Clear global cache to test initial boot
  delete globalThis.__ttypt_shared_wasm__;

  // 3. Boot shared WASM kernel
  const { getSubstrate, getSharedWasm } = await import("../../external/libstele/npm/dist/index.js");
  const kernel = getSharedWasm();
  const substrate = getSubstrate();
  assert.ok(kernel, "Kernel should boot and return exports");
  assert.equal(instantiationCount, 1, "First boot should trigger exactly 1 instantiation");

  // Verify pure-C stele (stl_) WASM kernel exports and slot operations
  assert.ok(typeof (kernel.stl_kernel_boot || kernel.stele_kernel_boot) === "function", "stl_kernel_boot export must exist");
  assert.ok(typeof (kernel.stl_kernel_mount || kernel.stele_kernel_mount) === "function", "stl_kernel_mount export must exist");
  assert.ok(typeof (kernel.stl_kernel_action || kernel.stele_kernel_action) === "function", "stl_kernel_action export must exist");
  assert.ok(typeof (kernel.stl_kernel_unmount || kernel.stele_kernel_unmount) === "function", "stl_kernel_unmount export must exist");
  assert.equal(substrate.kernelBoot(), 0);
  assert.equal(substrate.kernelVersion(), 2);
  assert.equal(substrate.kernelMount(0, "nonexistent", "{}"), -2);
  assert.equal(substrate.kernelUnmount(0), 0);

  // 4. Exercise Corm CandidateSet via shared WASM
  const { CandidateSet } = await import("../../external/libcorm/npm/dist/index.js");
  const setA = new CandidateSet();
  setA.push(10).push(20).push(30).seal();
  assert.deepEqual(setA.toArray(), [10, 20, 30]);

  // 5. Exercise Joint Temporal Timeline via shared WASM
  const { JointTimeline } = await import("../../external/libjoint/npm/dist/index.js");
  const timeline = new JointTimeline();
  timeline.recordInterval(101, 1000, 2000);
  timeline.recordInterval(102, 3000, 4000);
  const conflicts = timeline.queryOverlaps(1500, 2500);
  assert.deepEqual(conflicts, [101]);

  // 6. Exercise Islet Morton 2D via shared WASM
  const { encodeMorton2D, decodeMorton2D } = await import("../../external/libislet/npm/dist/index.js");
  const code = encodeMorton2D(120, -340);
  const decoded = decodeMorton2D(code);
  assert.equal(decoded.x, 120);
  assert.equal(decoded.y, -340);

  // 7. Exercise Lemma Unicode Case Folding via shared WASM
  const { foldAccents } = await import("../../external/liblemma/npm/dist/index.js");
  const folded = foldAccents("Pão de Açúcar");
  assert.equal(folded, "pão de açúcar");

  // 8. Exercise Sepal Vector Cosine via shared WASM
  const { cosineSimilarity } = await import("../../external/libsepal/npm/dist/index.js");
  const sim = cosineSimilarity([1.0, 0.0, 0.0], [1.0, 0.0, 0.0]);
  assert.ok(Math.abs(sim - 1.0) < 0.001);

  // 9. Exercise Hyle Row Validation via shared WASM
  const { validateRow } = await import("../../external/libhyle/npm/dist/index.js");
  const errs = validateRow([{ name: "username", required: true, minLength: 3 }], { username: "al" });
  assert.equal(errs.length, 1);
  assert.equal(errs[0].rule, "minLength");

  // 10. Exercise Bud VDOM diffing via native/WASM
  const { diffVdom, h } = await import("../../external/libbud/npm/dist/index.js");
  const patches = diffVdom(h("p", {}, []).setText("A"), h("p", {}, []).setText("B"));
  assert.ok(patches.length >= 1);

  // 11. Exercise Xylem dynamic hook bus
  const { areg, call } = await import("../../external/libxylem/npm/dist/index.js");
  areg("test_single_inst", (x) => x * 2);
  const busRes = call("test_single_inst", 21);
  assert.deepEqual(busRes, [42]);

  // 12. CRITICAL INVARIANT: Exactly 1 instantiation across all libraries!
  assert.equal(
    instantiationCount,
    1,
    `Expected exactly 1 WebAssembly instantiation, but got ${instantiationCount}!`
  );

  // Restore instrumentation
  WebAssembly.Instance = OriginalInstance;
  WebAssembly.instantiate = originalInstantiate;
});

test("All 14 colocated WebAssembly reactors execute simultaneously on one shared WebAssembly.Memory across a 64 MB memory.grow()", async () => {
  const { getSubstrate } = await import("../../external/libstele/npm/dist/index.js");
  const substrate = getSubstrate();
  const sharedMem = substrate.memory;

  const { CandidateSet, MinHeapRanker, getCormWasm } = await import("../../external/libcorm/npm/dist/index.js");
  const { JointTimeline, getJointWasm } = await import("../../external/libjoint/npm/dist/index.js");
  const { encodeMorton2D, decodeMorton2D, getIsletWasm } = await import("../../external/libislet/npm/dist/index.js");
  const { foldAccents, tokenize, getLemmaWasm } = await import("../../external/liblemma/npm/dist/index.js");
  const { cosineSimilarity, sketchVector, hammingDistance, getSepalWasm } = await import("../../external/libsepal/npm/dist/index.js");
  const { validateRow, getHyleWasm } = await import("../../external/libhyle/npm/dist/index.js");
  const { HyleSource, getHyleSourceWasm } = await import("../../external/libhyle-source/npm/dist/index.js");
  const { diffVdom, h, getBudWasm } = await import("../../external/libbud/npm/dist/index.js");
  const { defaultBus, getXylemWasm } = await import("../../external/libxylem/npm/dist/index.js");
  const { transposeText, getPitchWasm } = await import("../../external/libpitch/npm/dist/index.js");
  const { LaminaSurface, getLaminaWasm } = await import("../../external/liblamina/npm/dist/index.js");
  const { chaffPoint2D, chaffOctaves, getChaffWasm } = await import("../../external/libchaff/npm/dist/index.js");
  const { createAreoleSystem, getAreoleWasm } = await import("../../external/libareole/npm/dist/index.js");
  const { getAurenWasm, cosineSimilarity: aurenCosine } = await import("../../external/axil-auren/npm/dist/index.js");

  // 1. Instantiate all 13 downstream WASM reactors onto the single shared stele memory
  const modules = [
    ["corm", getCormWasm()],
    ["joint", getJointWasm()],
    ["islet", getIsletWasm()],
    ["lemma", getLemmaWasm()],
    ["sepal", getSepalWasm()],
    ["hyle", getHyleWasm()],
    ["hyle-source", getHyleSourceWasm()],
    ["bud", getBudWasm()],
    ["xylem", getXylemWasm()],
    ["pitch", getPitchWasm()],
    ["lamina", getLaminaWasm()],
    ["chaff", getChaffWasm()],
    ["areole", getAreoleWasm()],
    ["auren", getAurenWasm()],
  ];

  for (const [name, inst] of modules) {
    assert.ok(inst, `${name} WASM instance should load`);
    assert.equal(inst.exports.memory, sharedMem, `${name} must share stele's single WebAssembly.Memory`);
  }

  // 2. Populate live state across multiple modules BEFORE dynamic heap growth
  const cormSet = CandidateSet.wasm();
  cormSet.push(300).push(100).push(200).seal();
  assert.deepEqual(cormSet.toArray(), [100, 200, 300]);

  const ranker = MinHeapRanker.wasm(3, 0.1);
  ranker.push(100, 0.4).push(200, 0.9).push(300, 0.7);

  const joint = JointTimeline.wasm();
  joint.clear();
  joint.recordInterval(501, 10, 50);
  joint.recordInterval(502, 60, 100);
  assert.deepEqual(joint.queryOverlaps(20, 70), [501, 502]);

  const hyleSrc = HyleSource.wasm();
  hyleSrc.registerDataset({ id: "songs" });
  hyleSrc.putItem("songs", "s1", { id: "s1", title: "Canção do Mar" });

  const surface = new LaminaSurface(16, 16);
  surface.clear(0xff112233);
  assert.equal(surface.pick(4, 4), 0xff112233);

  const areole = createAreoleSystem(160, 128, 16, 32, 16, 4);
  const sampleBefore = areole.probe(32, 32, 1);
  assert.ok(sampleBefore !== null);

  // 3. Force a 64 MB dynamic heap allocation (simulating in-browser LLM weights/KV allocation)
  const initialByteLength = sharedMem.buffer.byteLength;
  const llmArenaPtr = substrate.malloc(64 * 1024 * 1024);
  assert.ok(llmArenaPtr >= 2097152, `LLM arena pointer (${llmArenaPtr}) must be in the >2MB heap region`);
  assert.ok(sharedMem.buffer.byteLength > initialByteLength, "WebAssembly.Memory must dynamically grow beyond initial 16 MB");

  // Fill the 64 MB block to prove no static data or stack band is touched
  new Uint8Array(sharedMem.buffer, llmArenaPtr, 64 * 1024 * 1024).fill(0x5a);

  // 4. Verify all pre-existing WASM structures and all 14 WASM modules work after memory.grow()
  assert.deepEqual(cormSet.toArray(), [100, 200, 300], "Corm CandidateSet must survive 64MB memory.grow()");
  const ranked = ranker.sorted();
  assert.equal(ranked[0].ref, 200);
  assert.deepEqual(joint.queryOverlaps(20, 70), [501, 502], "Joint intervals must survive 64MB memory.grow()");
  assert.equal(hyleSrc.getField("songs", "s1", "title"), "Canção do Mar", "HyleSource dataset must survive 64MB memory.grow()");
  assert.equal(surface.pick(4, 4), 0xff112233, "LaminaSurface pixels must survive 64MB memory.grow()");

  const morton = encodeMorton2D(-45, 89, { wasm: true });
  assert.deepEqual(decodeMorton2D(morton, { wasm: true }), { x: -45, y: 89 });

  assert.equal(foldAccents("Pão de Açúcar", { wasm: true }), "pão de açúcar");
  assert.deepEqual(tokenize("Pão de Açúcar!", { wasm: true }), ["pão", "de", "açúcar"]);

  const sim = cosineSimilarity([1, 0, 0], [1, 0, 0], { wasm: true });
  assert.ok(Math.abs(sim - 1.0) < 1e-5);
  const skA = sketchVector([1, -1, 1, -1]);
  const skB = sketchVector([1, 1, 1, -1]);
  assert.equal(hammingDistance(skA, skB), 1);

  const hyleErrs = validateRow([{ name: "code", required: true, minLength: 4 }], { code: "ab" }, { wasm: true });
  assert.equal(hyleErrs.length, 1);

  const vdomPatches = diffVdom(h("div", {}, []).setText("old"), h("div", {}, []).setText("new"), { wasm: true });
  assert.ok(vdomPatches.length >= 1);

  const unreg = defaultBus.register("wasm_hook_check", (v) => v + 1, { wasm: true });
  assert.deepEqual(defaultBus.call("wasm_hook_check", 9), [10]);
  unreg();

  const transposed = transposeText("C G Am F", 2, 0, { wasm: true });
  assert.ok(transposed.includes("D") && transposed.includes("Bm"));

  const n2d = chaffPoint2D(10, 20, 42, 4, { wasm: true });
  assert.equal(typeof n2d, "number");
  const oct = chaffOctaves(2, [0, 0], [{ x: 2, w: 1 }], 42, 2, { wasm: true });
  assert.equal(oct.length, 16);

  const sampleAfter = areole.probe(32, 32, 1);
  assert.deepEqual(sampleAfter, sampleBefore, "Areole quadtree state must survive 64MB memory.grow()");

  const aSim = aurenCosine([0, 1, 0], [0, 1, 0], { wasm: true });
  assert.ok(Math.abs(aSim - 1.0) < 1e-5);

  // Clean up
  substrate.free(llmArenaPtr);
  cormSet.free();
  ranker.free();
  surface.free();
  areole.destroy();
});
