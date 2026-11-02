# Colocated Node.js & WebAssembly Adapters Architecture

## 1. Overview & Core Philosophy

The `tty.pt` systems ecosystem is built upon pure-C99 computational kernels and data engines (`libcorm`, `libjoint`, `libsepal`, `liblemma`, `libhyle`, `libbud`, `libislet`, `libxylem`). When higher-level runtimes (Node.js servers, Next.js applications like `duna`, browser frontends, or CLI tools) consume these engines, creating separate standalone git repositories for each adapter induces repository bloat and maintenance overhead.

This architecture codifies the **Colocated Dual-Engine Model**:
1. **Zero New Repositories**: Every language adapter lives inside an `npm/` subdirectory within the respective C library repository (`external/libcorm/npm/`, `external/libjoint/npm/`, etc.).
2. **Pure-C Toolchain Isolation**: The standard C build (`make`, `make install`, `mk/include.mk`) NEVER invokes Node.js or npm. JavaScript/WASM toolchains are strictly opt-in.
3. **Dual-Engine Universal Runtime**:
   - In **Node.js**: Automatically loads native Node-API (`.node`) shared addons with direct OS memory mapping (`mmap(2)`) for maximum throughput.
   - In **Browser / Serverless**: Seamlessly falls back to WebAssembly reactor builds (`.wasm`) compiled via `clang --target=wasm32-wasi` and `<ttypt/iso.h>`, using in-memory or OPFS/IndexedDB persistence.
4. **Strict Genericity**: Zero project-specific identifiers (e.g. `"duna"`), zero static buffer limits, and leak-free handle lifecycles.

---

## 2. Library Applicability & Capability Matrix

| Library | Core C Capabilities | Node.js Backend Role (Native N-API) | Browser Frontend Role (Isomorphic WASM) | Package Name |
| :--- | :--- | :--- | :--- | :--- |
| **`libcorm`** (`corm` + `cormr`) | Memory-mapped persistent KV, auto-indexing, candidate set algebra (`cormr_set_t`), streaming min-heap ranker (`cormr_rank_t`). | **High**: Embedded persistent store for Next.js, Express, Fastify, and CLI tools. Direct $O(1)$ lookups, binary serialization. | **High**: Client-side candidate set algebra (intersection/union of 100k records in <1ms) and min-heap ranking. RAM storage with OPFS/IndexedDB snapshots via `corm_host_persist/restore`. | `@ttypt/corm` |
| **`libjoint`** | Interval-tree temporal presence, interval intersection (`joint_intersect`), conflict resolution, schedule recording. | **High**: Booking engines, reservation conflicts, room availability, temporal scheduling backend. | **High**: Interactive calendar UI, instant client-side date/slot conflict detection, drag-and-drop booking preview with zero server roundtrips. | `@ttypt/joint` |
| **`libsepal`** | Dense vector similarity, bitwise Hamming sketches ($O(1)$ popcount pre-filtering), cosine & dot-product ranking. | **High**: Local semantic search, embedding cache retrieval, document deduplication, agent memory ranking (`axil-auren`, `pi-mm`). | **Medium-High**: Client-side vector search over pre-embedded datasets (e.g. offline documentation search, client-side autocomplete). | `@ttypt/sepal` |
| **`liblemma`** | Unicode accent folding (`lemma_fold`), text tokenization, Porter stemming, BM25 lexical inverted indexing. | **High**: Full-text search backend, multilingual search indexing, accent-insensitive query normalization. | **High**: Instant client-side search-as-you-type in browser inputs, offline document search, client-side accent folding (`pão` $\leftrightarrow$ `pao`) with zero bundle bloat. | `@ttypt/lemma` |
| **`libhyle`** | Canonical schema descriptors (`hyle_schema_desc_t`), row purification (`hyle_purify_row`), blueprint schemas, query AST. | **High**: Request payload & server action validation. Single Source of Truth for schema definitions across C and Node. | **High**: Client-side form validation before submission (React Hook Form / native forms). Instant field error markers sharing identical validation rules with backend. | `@ttypt/hyle` |
| **`libbud`** | Declarative 5-field UI binder, virtual DOM diffing (`bud_vdom_diff`), patch operations (`bud_render_patch_ops`), SSR hydration. | **Medium**: Fast headless SSR HTML rendering from JSON state. | **Active**: Core client-side DOM hydration and patching (`htdocs/*.wasm`). | `@ttypt/bud` |
| **`libislet`** | 2D/3D spatial quadtree/grid engine, bounding box intersections, spatial entity clustering. | **Medium**: Spatial entity queries, coworking floorplan coordinate indexing, geofencing. | **Medium**: Interactive SVG/Canvas spatial selection, seat map panning, viewport entity culling. | `@ttypt/islet` |
| **`libxylem`** | Dynamic hook/event dispatch bus (`xy_areg`, `xy_call`), decoupled lifecycle management. | **Low-Medium**: Plugin architecture in Node servers. | **Active**: `libxylem-wasm.c` provides hook dispatching in WASM. | `@ttypt/xylem` |

---

## 3. Directory Layout & Distribution Convention

Within each C library repository, all npm/WASM assets are colocated in `npm/`:

```text
libcorm/
├── include/ttypt/corm.h      # Canonical C API
├── include/ttypt/cormr.h     # Corm Recall Kernel API
├── src/libcorm.c             # C Implementation
├── Makefile                  # Pure C build (untouched by Node)
├── rust-bindings/            # Rust crate (existing)
└── npm/                      # Colocated JS/WASM adapter package
    ├── package.json          # "@ttypt/corm" package manifest
    ├── tsconfig.json         # TypeScript configuration
    ├── native/
    │   └── corm_napi.c       # Generic Node-API C addon
    ├── src/
    │   ├── index.ts          # Universal entry point
    │   └── types.ts          # Strong TypeScript definitions
    ├── dist/                 # Compiled ESM/CJS bundles + types
    ├── tests/
    │   └── corm.test.mjs     # Package test suite
    └── README.md             # JS/WASM package documentation
```

### Consumption Models
- **Local Monorepo / Relative Path**:
  ```json
  "dependencies": {
    "@ttypt/corm": "file:../site/external/libcorm/npm",
    "@ttypt/joint": "file:../site/external/libjoint/npm",
    "@ttypt/hyle": "file:../site/external/libhyle/npm"
  }
  ```
- **Public NPM Registry**:
  ```bash
  cd external/libcorm/npm && npm publish --access public
  ```
  Consumers install directly: `npm install @ttypt/corm`.

---

## 4. Toolchain Isolation Invariants

1. **Makefile Purity**:
   Running `make`, `make test`, or `make install` at the library root never executes Node, npm, or npx.
2. **Opt-In Targets**:
   The root Makefile provides optional targets:
   ```makefile
   NPM ?= npm

   npm-build:
   	@if command -v $(NPM) >/dev/null 2>&1; then \
   		(cd npm && $(NPM) install && $(NPM) run build); \
   	else \
   		echo "Skipping npm build — $(NPM) not found"; \
   	fi

   npm-test: npm-build
   	@if command -v $(NPM) >/dev/null 2>&1; then \
   		(cd npm && $(NPM) test); \
   	fi
   ```
3. **Clean Gitignore**:
   `npm/node_modules/`, `npm/dist/`, `npm/build/`, and `*.node` are strictly gitignored.

---

## 5. Universal Runtime Facade (Node N-API + Browser WASM)

```typescript
// Universal loader pattern
let nativeBinding: any = null;

if (typeof process !== "undefined" && process.versions?.node) {
  try {
    const { createRequire } = await import("node:module");
    const require = createRequire(import.meta.url);
    nativeBinding = require("../build/Release/corm_node.node");
  } catch {
    // Native addon not compiled; graceful fallback to WASM
  }
}

export class CormDb {
  private backend: CormBackend;
  constructor(filePath?: string, dbName: string = "default") {
    if (nativeBinding) {
      this.backend = new NativeCormBackend(nativeBinding, filePath, dbName);
    } else {
      this.backend = new WasmCormBackend(filePath, dbName);
    }
  }
}
```

---

## 6. Genericity & Eliminating Application Coupling

1. **Configurable Database Identifiers**: Database names are explicit parameters, never hardcoded.
2. **Dynamic Value Buffers**: Replace fixed static buffers (e.g. 4096 bytes) with dynamically probed string and binary lengths via `napi_get_value_string_utf8(env, val, NULL, 0, &len)`.
3. **Decoupled Packages**: `@ttypt/corm` manages storage and set algebra; `@ttypt/joint` manages interval trees; `@ttypt/hyle` manages schema validation. No cross-library monolithic coupling.

---

## 7. How New Modules Plug Into the System (Plug-and-Play Contract)

To add a new module to the `tty.pt` stack (e.g. `nd-*` game modules, `liblamina`, or third-party extensions) without modifying `@ttypt/qsys` or any sibling libraries:

### Step 1: C Source Interface
Include `<ttypt/iso.h>` for portable export/import attributes:
```c
#include <ttypt/iso.h>

ISO_EXPORT(uint32_t, my_module_calculate, uint32_t a, uint32_t b) {
    return a + b;
}
```

### Step 2: Colocated NPM Scaffolding
Create `npm/` in your repository:
```text
my-lib/
├── include/ttypt/my_lib.h
├── src/my_lib.c
├── Makefile
└── npm/
    ├── package.json          # "@ttypt/my-lib"
    ├── native/my_lib_napi.c  # Node-API addon (build/Release/)
    ├── wasm/my_lib_wasm.c    # Standalone WASM reactor (dist/)
    └── dist/
        ├── index.js          # JavaScript/TypeScript facade
        └── index.d.ts        # Strong typings
```

### Step 3: Connecting to the Substrate
In `dist/index.js`, register your module with `@ttypt/qsys` on the shared substrate:
```typescript
import { getSubstrate } from "@ttypt/qsys";

export class MyModule {
  constructor() {
    const substrate = getSubstrate();
    substrate.registerModule("my_module", this);
  }

  calculate(a: number, b: number): number {
    return a + b;
  }
}
```

### Step 4: Pluggable Corm Axis (If Providing a Query Axis)
If your module provides a query dimension (e.g. a custom game inventory or spatial axis), implement `CormAxis`:
```typescript
export class MyCustomAxis {
  readonly name = "my_axis";
  fill(params: any): number[] {
    return [101, 102];
  }
  rank(params: any, ref: number): number {
    return 1.0;
  }
}

// In client code:
db.registerAxis(new MyCustomAxis());
```

**Zero Modifications Required**:
- `libqsys` is never touched.
- No sibling libraries need updating.
- The new module is 100% independent, plug-and-play, and immediately usable.
