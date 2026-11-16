# Architectural Roadmap: Hyle Ecosystem & Cross-Framework Convergence

> Strategic roadmap detailing the expansion of Hyle into Next.js/React (`~/duna`), the architecture of `hyle-react`, server-side `hyle-source` integration via Node-API, and ecosystem-wide boundary standardization across all 13 `@tty-pt/*` libraries.

---

## 1. Executive Summary & The Framework Pair

The architecture is built upon a deliberate **framework pair**:

1. **`site` (Pure C Systems Web)**: Pure C on the server (Axil), isomorphic C on the client (Bud/Stele WASM), with SSR-first progressive enhancement and zero client-side JavaScript domain logic.
2. **`~/duna` (Modern React Web)**: Next.js 16 (Turbopack), React 19, TypeScript, and Tailwind CSS.

Currently, `~/duna` (`/home/quirinpa/duna/lib/cormStore.ts`) interacts directly with low-level C storage primitives via `@tty-pt/corm` (`CormDb`) and `@tty-pt/joint` (`JointTemporalAxis`) through Node-API native bindings. However, domain schemas, data validation, serialization, and filtering logic are implemented ad-hoc in TypeScript.

The objective of this roadmap is to bring the unified, declarative **Hyle data engine** into `~/duna`, establishing full parity between the pure-C and React environments.

---

## 2. The Hyle Stack Across Frameworks

| Conceptual Layer | `site` (Pure C + Bud) | `~/duna` (Next.js + React 19) |
| :--- | :--- | :--- |
| **Data Schema & Purification** | `external/libhyle` (`hyle_schema_desc_t`) | `@tty-pt/hyle` (TypeScript & Zod types via `hyle-to-ts`) |
| **Dataset Storage & FTS Querying** | `external/libhyle-source` (`libhyle-source.so`) | `@tty-pt/hyle-source` (Node-API addon `.node` in Node server) |
| **Presentation & Binder Bridge** | `external/libhyle-bud` (`hyle_bud_*` $\rightarrow$ `bud_node *`) | **`@tty-pt/hyle-react`** (React 19 Server/Client Components) |
| **Rendering Engine** | `external/libbud` (Pure-C VDOM Diff/Patch) | React 19 Concurrent / Fiber DOM Engine |

---

## 3. Architecture of `hyle-react`

Just as `libhyle-bud` provides a pure bridge between Hyle data schemas and Bud virtual DOM nodes without database dependencies, **`hyle-react`** will be an independent, pure UI bridge binding Hyle schemas directly to React 19 components and hooks.

```
┌─────────────────────────────────┐
│     Hyle C Schema Definition    │  (fields.h: hyle_schema_desc_t)
└────────────────┬────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│      TypeScript / Zod Bridge    │  (@tty-pt/hyle via hyle-to-ts)
└────────────────┬────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│           hyle-react            │  (@tty-pt/hyle-react)
│  ┌───────────────────────────┐  │
│  │ <HyleTable>               │  │  - Sortable column headers
│  │ <HyleFilterBar>           │  │  - Omni/custom filter toggle
│  │ <HyleSingleSelect>        │  │  - Searchable dropdown with "All" option
│  │ <HyleMultiSelect>         │  │  - Faceted multiselect checkboxes
│  │ <HylePagination>          │  │  - In-place paging slices
│  │ useHyleQuery()            │  │  - Client-side cache hook
│  └───────────────────────────┘  │
└────────────────┬────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│      React 19 / Next.js 16      │  (Server & Client Components)
└─────────────────────────────────┘
```

### 3.1 Declarative Component Suite

1. **`<HyleTable<T>>`**:
   - Accepts a schema descriptor and typed record rows.
   - Automatically renders accessible semantic HTML `<table>` structures.
   - Features column-type-aware rendering, sort toggle headers (asc/desc), and row action links.
   - Matches the behavior of `hyle_bud_table_actions()`.

2. **`<HyleFilterBar>`**:
   - Replicates the omni/custom mode toggle (`?custom=1`).
   - Automatically generates the appropriate input control per schema field type: text filters, boolean checkboxes, and reference dropdowns.
   - Includes standard `<form method="GET">` fallback for full SSR operation.

3. **`<HyleSingleSelect>` & `<HyleMultiSelect>`**:
   - Progressive enhancement: renders standard `<details>` dropdowns with radio/checkbox panels.
   - Includes a default `"All <label>s"` (`value=""`) option at the top for clean No-JS reset.
   - Supports live search filtering within options and click-to-deselect toggling in interactive mode.

4. **Hooks**:
   - `useHyleQuery(datasetId, query)`: Fetches data slices from the server, maintaining an in-memory client LRU cache to eliminate redundant network round-trips.
   - `useHyleFilter(schema)`: Manages URL query parameter synchronization with browser history.

### 3.2 React 19 Server & Client Component Harmony
- **React Server Components (RSC)**: Components render to static HTML on the Next.js server with zero client bundle footprint. Forms degrade gracefully to native HTTP GET submissions.
- **Client Components (`'use client'`)**: Enhance the rendered HTML with debounced live option searching, optimistic in-place table updates, and keyboard navigation.

---

## 4. Next.js Server Integration: `@tty-pt/hyle-source`

Next.js Server Components, Route Handlers (`app/api/...`), and Server Actions execute in the standard Node.js runtime. To allow Next.js to directly query and manage datasets, `external/libhyle-source` must be packaged for NPM with Node-API (N-API) bindings.

### 4.1 Node-API Native Addon Architecture
Following the battle-tested pattern of `@tty-pt/corm` (`native/corm_napi.c`) and `@tty-pt/hyle` (`native/hyle_napi.c`), `@tty-pt/hyle-source` provides:

- `native/hyle_source_napi.c`: C module compiling against `node-api` headers to produce `build/Release/hyle_source_node.node`.
- High-level JavaScript class wrapping C functions:
  ```typescript
  export class HyleSource {
    constructor(storageDir?: string);
    registerDataset(id: string, schema: HyleSchemaDef, options?: DatasetOptions): void;
    query(datasetId: string, queryString: string): HyleQueryResult;
    getItem(datasetId: string, id: string): Record<string, unknown> | null;
    putItem(datasetId: string, id: string, record: Record<string, unknown>): void;
    deleteItem(datasetId: string, id: string): void;
    getField(datasetId: string, id: string, field: string): string | null;
  }
  ```

### 4.2 Consumption in `~/duna`
In Next.js, code instantiates `HyleSource` on the server:

```typescript
// duna/lib/hyleStore.ts
import { HyleSource } from "@tty-pt/hyle-source";
import path from "node:path";

export const hyleStore = new HyleSource(path.join(process.cwd(), "var/data"));
hyleStore.registerDataset("members", memberSchema);
```

```typescript
// duna/app/api/members/route.ts
import { hyleStore } from "@/lib/hyleStore";

export async function GET(request: Request) {
  const { searchParams } = new URL(request.url);
  const result = hyleStore.query("members", searchParams.toString());
  return Response.json(result);
}
```

---

## 5. Ecosystem Audit: WASM & Node Boundary Consistency

All 13 libraries in the `@tty-pt/*` ecosystem adhere to a universal dual-target architecture:

1. **The Two-Header Rule**:
   - Standalone C libraries include **only `<ttypt/iso.h>`** for compiler export primitives (`ISO_EXPORT`, `ISO_IMPORT`). They never touch `<ttypt/stele_wasm.h>`.
   - Interactive UI modules include `<ttypt/stele_wasm.h>`, which transitively includes `iso.h` and defines the UI slot ABI (`STL_WASM_MODULE`, `stl_slot_invalidate`).
2. **Dual Node/WASM Target**:
   - In Node.js (Next.js server, unit tests): loads compiled native Node-API addon (`.node`).
   - In browser / Edge: loads standalone WebAssembly binary (`.wasm`).

### Ecosystem Status Matrix

| Library Package | C Library | Node-API (`.node`) | Browser WASM (`.wasm`) | Two-Header Rule Compliance | NPM Status |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`@tty-pt/stele`** | `libstele.so` | N/A (Substrate) | `stele.wasm` | Declares `stele_wasm.h` | Published (1.5.2) |
| **`@tty-pt/bud`** | `libbud.so` | `bud_node.node` | `bud.wasm` | `<ttypt/iso.h>` only | Published (1.2.2) |
| **`@tty-pt/corm`** | `libcorm.so` | `corm_node.node` | `corm.wasm` | `<ttypt/iso.h>` only | Published (2.2.4) |
| **`@tty-pt/joint`** | `libjoint.so` | `joint_node.node` | `joint.wasm` | `<ttypt/iso.h>` only | Published (1.6.3) |
| **`@tty-pt/lemma`** | `liblemma.so` | `lemma_node.node` | `lemma.wasm` | `<ttypt/iso.h>` only | Published (1.2.2) |
| **`@tty-pt/islet`** | `libislet.so` | `islet_node.node` | `islet.wasm` | `<ttypt/iso.h>` only | Published (1.3.2) |
| **`@tty-pt/sepal`** | `libsepal.so` | `sepal_node.node` | `sepal.wasm` | `<ttypt/iso.h>` only | Published (1.3.2) |
| **`@tty-pt/xylem`** | `libxylem.so` | `xylem_node.node` | `xylem.wasm` | `<ttypt/iso.h>` only | Published (1.7.2) |
| **`@tty-pt/transp`** | `libtransp.so` | `transp_node.node` | `transp.wasm` | `<ttypt/iso.h>` only | Published (1.1.2) |
| **`@tty-pt/lamina`** | `liblamina.so` | `lamina_node.node` | `lamina.wasm` | `<ttypt/iso.h>` only | Published (1.1.1) |
| **`@tty-pt/hyle`** | `libhyle.so` | `hyle_node.node` | `hyle.wasm` | `<ttypt/iso.h>` only | Published (1.6.1) |
| **`@tty-pt/hyle-bud`** | `libhyle-bud.so` | JS stub only | Compiled in `list.wasm` | `<ttypt/iso.h>` + `bud.h` | Published (1.1.1) |
| **`@tty-pt/hyle-source`** | `libhyle-source.so` | `hyle_source_node.node` | `hyle-source.wasm` | `<ttypt/iso.h>` only | Standardized (1.3.1) |

---

## 6. Phased Implementation Roadmap

### Phase 1: Bootstrap `@tty-pt/hyle-source` (Completed)
- Scaffolded `external/libhyle-source/npm/` packaging with `package.json`, `hyle-source.pc`, and TypeScript definitions.
- Implemented `native/hyle_source_napi.c` Node-API addon with dataset CRUD, querying, and query cache.
- Implemented `npm/wasm/hyle_source_wasm.c` compiling to standalone `dist/hyle-source.wasm` using `<ttypt/iso.h>` and `-Wl,--import-memory -Wl,--global-base=2097152`.
- Polymorphic Node/WASM loader in `dist/index.js` verified with 4/4 passing unit tests.

### Phase 2: Framework-Agnostic Substrate Caching & Reactive Event Bus (Current Focus)
- Implement two-tier caching in `external/libhyle-source`:
  1. Query Slice Cache: `(dataset_id, normalized_query) -> { total, ids }`.
  2. Entity Record Cache: `(dataset_id, item_id) -> record_json`.
- Add canonical query string normalizer (sorting query keys lexicographically).
- Add automatic mutation invalidation: all `putItem`, `updateItem`, `deleteItem` writes automatically invalidate affected dataset query entries.
- Add reactive subscription bus:
  - C: `hyle_source_subscribe(dataset_id, cb, user)`
  - Node-API & WASM: `subscribe(datasetId, cb) -> unsubscribe`
- Implement thin UI adapters:
  - Bud: triggers `stl_slot_invalidate(slot)`.
  - React 19: `useHyleDataset()` hook using `useSyncExternalStore` (zero external dependencies).

### Phase 3: Develop `@tty-pt/hyle-react` Component Suite
- Create `external/libhyle-react/` package.
- Implement core React 19 component library (`<HyleTable>`, `<HyleFilterBar>`, `<HyleSingleSelect>`, `<HyleMultiSelect>`, `<HylePagination>`).
- Progressive enhancement: RSC static HTML + client-side interactive enhancements.

### Phase 4: Integration into `~/duna`
- Add `@tty-pt/hyle`, `@tty-pt/hyle-source`, and `@tty-pt/hyle-react` to `/home/quirinpa/duna/package.json`.
- Migrate `duna/lib/cormStore.ts` to structured Hyle dataset persistence.
- Replace manual table and filter markup in Duna admin views with Hyle React components.
- Run complete test verification in `duna` (`npm run test:unit`, `test:e2e`, and `audit_all_pages.py`).

---

## 7. Framework-Agnostic Caching System (`libhyle-source` Substrate Cache)

### 7.1 The Architectural Paradigm
UI frameworks (React, Bud, Vue, Svelte) are transient presentation engines. Caching data slices in framework-specific stores (e.g. TanStack Query, Redux, SWR) introduces architectural fragmentation:
1. Server-side C engines (Axil) and client-side Bud modules cannot share or benefit from the cache.
2. Invalidation rules must be duplicated across every language and framework.
3. Client-side memory is wasted duplicating entity representations.

By placing the caching engine inside **`libhyle-source`**, caching becomes a **universal substrate capability** operating symmetrically across pure C (`libhyle-source.so`), Node.js (`hyle_source_node.node`), and WebAssembly (`hyle-source.wasm`).

```
                        ┌────────────────────────────────────────────────────────┐
                        │              libhyle-source C Core                    │
                        │                                                        │
                        │  ┌──────────────┐  ┌──────────────┐  ┌───────────────┐ │
                        │  │  Query Cache │  │ Entity Cache │  │ Auto-Invalid. │ │
                        │  │  (LRU + TTL) │  │  (By Record) │  │  on Mutation │ │
                        │  └──────────────┘  └──────────────┘  └───────────────┘ │
                        │            ▲                      ▲                    │
                        │            │                      │                    │
                        │     Query Normalizer       Reactive Event Bus          │
                        │   (canonical key order)     (on_change notifications)  │
                        └───────────────────┬────────────────────────────────────┘
                                            │
               ┌────────────────────────────┼─────────────────────────────┐
               ▼                            ▼                             ▼
       Pure C / Axil (SSR)          WASM Reactor (Browser)        Node-API (Next.js Server)
       Linked directly              hyle-source.wasm              hyle_source_node.node
               │                            │                             │
               ▼                            ▼                             ▼
         Bud UI Binder            Browser Renderers              React Server Components
     stl_slot_invalidate()       ┌─────────┴─────────┐            Direct in-memory hit
                                 ▼                   ▼            Zero duplicate disk reads
                                Bud                React
                            (Pure C WASM)    (useSyncExternalStore)
```

### 7.2 Two-Tier In-Memory Cache Model
1. **Query Slice Tier**: Maps `(dataset_id, normalized_query) -> { total, ids: [id1, id2, ...], timestamp }`.
2. **Entity Record Tier**: Maps `(dataset_id, item_id) -> record_json`.
Query evaluation first checks the Query Slice Tier. If warm, individual entity records are retrieved from the Entity Record Tier with zero disk I/O.

### 7.3 Canonical Query Key Normalization
Different UI controls and URL parameters may specify query parameters in varying orders:
- Filter bar: `q=alice&role=engineer&sort=title`
- Deep link: `sort=title&q=alice&role=engineer`

`libhyle-source` parses and normalizes query keys into a canonical, alphabetically sorted representation prior to cache lookup. Both formulations hit the identical cache entry.

### 7.4 Automatic Mutation Invalidation
In conventional architectures, developers must manually declare cache invalidations:
```typescript
// The brittle manual approach:
await fetch('/api/members', { method: 'POST', body });
queryClient.invalidateQueries({ queryKey: ['members'] }); // Error-prone
```
In `libhyle-source`, mutations flow through `putItem()`, `updateItem()`, or `deleteItem()`. Because `libhyle-source` owns both the store and the cache:
- Every mutation automatically invalidates the query slice cache for that dataset.
- The entity cache for that item is updated or evicted atomically.
- All registered subscribers (Bud slots, React components) receive a change notification.

### 7.5 Zero-Dependency Thin UI Adapters

#### Bud Integration (Pure C WASM)
Bud registers a dataset change listener:
```c
static void on_dataset_changed(const char *dataset_id, void *user) {
    stl_slot_invalidate(slot_for_dataset(dataset_id));
}
```
Upon invalidation, Stele re-renders the slot, Bud diffs the VDOM, and minimal DOM patches are emitted without touching JavaScript.

#### React 19 Integration (`useSyncExternalStore`)
React 19 provides the native standard hook `useSyncExternalStore` for external stores:
```typescript
import { useSyncExternalStore } from "react";
import { getHyleSource, subscribeDataset } from "@tty-pt/hyle-source";

export function useHyleDataset(datasetId: string, queryStr: string) {
  const store = getHyleSource();

  const subscribe = (callback: () => void) => {
    return subscribeDataset(datasetId, callback);
  };

  const getSnapshot = () => {
    return store.cacheGet(datasetId, queryStr) ?? store.query(datasetId, queryStr);
  };

  return useSyncExternalStore(subscribe, getSnapshot);
}
```
This requires zero third-party libraries (no TanStack Query, Redux, or SWR), executes synchronously during rendering, and supports React 19 Concurrent Mode and SSR hydration.

