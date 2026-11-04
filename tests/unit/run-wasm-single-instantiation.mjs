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
