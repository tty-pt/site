# Architectural Roadmap: Hyle Ecosystem & Cross-Framework Convergence

> Strategic roadmap detailing the expansion of Hyle into Next.js/React (`~/duna`), the architecture of `@tty-pt/hyle-react`, server-side `@tty-pt/hyle-source` integration via Node-API/WASM, and ecosystem-wide boundary standardization across the `@tty-pt/*` libraries.

---

## 1. Executive Summary & The Framework Pair

The architecture is built upon a deliberate **framework pair**:

1. **`site` (Pure C Systems Web)**: Pure C on the server (Axil), isomorphic C on the client (Bud/Stele WASM), with SSR-first progressive enhancement and zero client-side JavaScript domain logic.
2. **`~/duna` (Modern React Web)**: Next.js 16 (Turbopack), React 19, TypeScript, and Tailwind CSS.

`~/duna` already depends on `@tty-pt/corm`, `@tty-pt/joint`, `@tty-pt/islet`, `@tty-pt/lemma`, `@tty-pt/sepal`, `@tty-pt/stele`, `@tty-pt/hyle`, `@tty-pt/hyle-source`, and `@tty-pt/hyle-react`. Today, `<HyleTable>` and `<HylePagination>` from `@tty-pt/hyle-react` power Duna's manager and portal list views, while server persistence in `/home/quirinpa/duna/lib/cormStore.ts` still interacts directly with `@tty-pt/corm` (`CormDb`) and `@tty-pt/joint` (`JointTemporalAxis`) through Node-API native bindings.

The objective of this roadmap is full parity between the pure-C and React environments across both UI bindings (`@tty-pt/hyle-react`) and declarative dataset persistence/caching (`@tty-pt/hyle-source`).

---

## 2. The Hyle Stack Across Frameworks

| Conceptual Layer | `site` (Pure C + Bud) | `~/duna` (Next.js + React 19) |
| :--- | :--- | :--- |
| **Data Schema & Purification** | `external/libhyle` (`hyle_schema_desc_t`) | `@tty-pt/hyle` (Node-API / WASM schema & query bindings) |
| **Dataset Storage & FTS Querying** | `external/libhyle-source` (`libhyle-source.so`) | `@tty-pt/hyle-source` (Node-API `.node` + `hyle-source.wasm`) |
| **Presentation & Binder Bridge** | `external/libhyle-bud` (`hyle_bud_*` $\rightarrow$ `bud_node *`) | **`@tty-pt/hyle-react`** (React 19 Components & Hooks) |
| **Rendering Engine** | `external/libbud` (Pure-C VDOM Diff/Patch) | React 19 Concurrent / Fiber DOM Engine |

---

## 3. Architecture of `@tty-pt/hyle-react`

Just as `libhyle-bud` provides a pure bridge between Hyle data schemas and Bud virtual DOM nodes without database dependencies, **`@tty-pt/hyle-react`** (`external/libhyle-react`, `v1.0.0`) is an independent UI bridge binding Hyle schemas and `@tty-pt/hyle-source`'s substrate cache to React 19 components and hooks.

```
┌─────────────────────────────────┐
│     Hyle C Schema Definition    │  (fields.h: hyle_schema_desc_t)
└────────────────┬────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│      @tty-pt/hyle & source      │  (Node-API / WASM substrate + cache)
└────────────────┬────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│       @tty-pt/hyle-react        │  (external/libhyle-react)
│  ┌───────────────────────────┐  │
│  │ <HyleTable>               │  │  - Schema/column-driven sortable table
│  │ <HylePagination>          │  │  - Page stepper & per-page selector
│  │ <HyleFilterBar>           │  │  - Search input & active filter badges
│  │ <HyleForm> / <HyleField>  │  │  - Schema-driven form & field wrappers
│  │ <HyleInput>               │  │  - Typed inputs (text/select/checkbox/...)
│  │ useHyleDataset()          │  │  - useSyncExternalStore cache hook
│  │ useHyleTable()            │  │  - Query/sort/page state hook
│  └───────────────────────────┘  │
└────────────────┬────────────────┘
                 │
                 ▼
┌─────────────────────────────────┐
│      React 19 / Next.js 16      │  (Server & Client Components)
└─────────────────────────────────┘
```

### 3.1 Implemented Component & Hook Suite (`@tty-pt/hyle-react@1.0.0`)

1. **`<HyleTable<T>>`**:
   - Accepts explicit `columns` or a Hyle `schema` (with `columnOverrides`) and typed `items`.
   - Renders semantic HTML `<table>` structures with sortable column headers (`onSort`), row click handlers (`onRowClick`), and custom cell renderers.
2. **`<HylePagination>`**:
   - Unopinionated page stepper, record count summary, and per-page selector (`perPageOptions`, `onPageChange`, `onPerPageChange`).
3. **`<HyleFilterBar>`**:
   - Controlled search input (`searchQuery`, `onSearchChange`) and removable active filter badges (`activeFilters`, `onRemoveFilter`, `onReset`).
4. **`<HyleForm>`, `<HyleField>`, `<HyleInput>`**:
   - Schema-driven form and typed input primitives (`text`, `number`, `email`, `password`, `search`, `textarea`, `checkbox`, `select`) with error rendering and field overrides.
5. **Hooks**:
   - `useHyleDataset(datasetIdOrOptions, queryStr?, options?)`: React 19 `useSyncExternalStore` hook subscribed to `@tty-pt/hyle-source`'s substrate cache with Next.js SSR `initialData` hydration support.
   - `useHyleTable(options?)`: Manages pagination, sort field/order, search query, and canonical `queryStr` serialization.

### 3.2 React 19 Server & Client Component Harmony
- **React Server Components (RSC)**: Static table/pagination markup renders on the Next.js server with SSR `initialData` hydration.
- **Client Components (`'use client'`)**: Enhance rendered views with interactive sorting, pagination, filter badges, and substrate-cache reactivity.

---

## 4. Next.js Server Integration: `@tty-pt/hyle-source`

Next.js Server Components, Route Handlers (`app/api/...`), and Server Actions execute in the standard Node.js runtime. `external/libhyle-source` (`@tty-pt/hyle-source@1.4.1`) ships both a compiled Node-API native addon and a browser/edge WebAssembly build.

### 4.1 Node-API Native Addon & WASM Architecture
Following the same pattern as `@tty-pt/corm` (`native/corm_napi.c`) and `@tty-pt/hyle` (`native/hyle_napi.c`), `@tty-pt/hyle-source` provides:

- `npm/native/hyle_source_napi.c`: C module compiling against `node-api` headers to `hyle_source_node.node`.
- `npm/wasm/hyle_source_wasm.c`: Browser/edge WebAssembly build (`npm/dist/hyle-source.wasm`) using `<ttypt/iso.h>`.
- High-level `HyleSource` class and standalone functions (`npm/dist/index.d.ts`):
  ```typescript
  export class HyleSource {
    constructor(options?: string | HyleSourceOptions);
    static wasm(): HyleSource;
    registerDataset(def: HyleDatasetDef | string): boolean;
    query(datasetId: string, queryStr: string): HyleQueryResult;
    putItem(datasetId: string, itemId: string, record: Record<string, any> | string): boolean;
    updateItem(datasetId: string, itemId: string, record: Record<string, any> | string): boolean;
    deleteItem(datasetId: string, itemId: string): boolean;
    itemExists(datasetId: string, itemId: string): boolean;
    getField(datasetId: string, itemId: string, fieldName: string): string | null;
    normalizeQuery(queryStr: string): string;
    cachePut(datasetId: string, queryStr: string, payload: string | Record<string, any>, opts?: HyleCachePutOpts): number;
    cacheHydrate(datasetId: string, queryStr: string, payload: string | Record<string, any> | any[], opts?: HyleCachePutOpts): number;
    cacheGet(datasetId: string, queryStr: string, opts?: HyleCacheGetOpts): string | HyleCacheStatusResult | null;
    cacheInvalidate(datasetId: string): void;
    entityPut(datasetId: string, itemId: string, payload: string | Record<string, any>): number;
    entityGet(datasetId: string, itemId: string): string | null;
    subscribe(datasetId: string, callback: HyleSubscriberCb): UnsubscribeFn;
    optimisticUpdate(datasetId: string, itemId: string, patch: Record<string, any> | string): OptimisticHandle;
    createStore(datasetId: string): HyleExternalStore;
  }
  ```

### 4.2 Target Consumption Pattern in `~/duna`
In Next.js, server routes can instantiate `HyleSource` directly instead of hand-rolling CRUD/query glue over raw `CormDb`:

```typescript
// duna/lib/hyleStore.ts
import { HyleSource } from "@tty-pt/hyle-source";
import path from "node:path";

export const hyleStore = new HyleSource(path.join(process.cwd(), "var/data"));
hyleStore.registerDataset({ id: "members", fields: memberFields });
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

Core libraries in the `@tty-pt/*` ecosystem adhere to a universal dual-target architecture:

1. **The Two-Header Rule**:
   - Standalone C libraries include **only `<ttypt/iso.h>`** for compiler export primitives (`ISO_EXPORT`, `ISO_IMPORT`). They never touch `<ttypt/stele_wasm.h>`.
   - Interactive UI modules include `<ttypt/stele_wasm.h>`, which transitively includes `iso.h` and defines the UI slot ABI (`STL_WASM_MODULE`, `stl_slot_invalidate`).
2. **Dual Node/WASM Target**:
   - In Node.js (Next.js server, unit tests): loads compiled native Node-API addon (`.node`).
   - In browser / Edge: loads WebAssembly binary (`.wasm`) on the shared Stele substrate.

### Ecosystem Status Matrix (Core Libraries)

| Library Package | C Library | Node-API (`npm/native/`) | Browser WASM (`npm/wasm/`) | Two-Header Rule Compliance | Version (`package.json`) |
| :--- | :--- | :--- | :--- | :--- | :--- |
| **`@tty-pt/stele`** | `libstele.so` | `stele_napi.c` | `stele.wasm` | Declares `stele_wasm.h` | `1.7.1` |
| **`@tty-pt/bud`** | `libbud.so` | `bud_napi.c` | `bud.wasm` | `<ttypt/iso.h>` only | `1.3.2` |
| **`@tty-pt/corm`** | `libcorm.so` | `corm_napi.c` | `corm.wasm` | `<ttypt/iso.h>` only | `2.4.2` |
| **`@tty-pt/joint`** | `libjoint.so` | `joint_napi.c` | `joint.wasm` | `<ttypt/iso.h>` only | `1.8.1` |
| **`@tty-pt/lemma`** | `liblemma.so` | `lemma_napi.c` | `lemma.wasm` | `<ttypt/iso.h>` only | `1.4.2` |
| **`@tty-pt/islet`** | `libislet.so` | `islet_napi.c` | `islet.wasm` | `<ttypt/iso.h>` only | `1.6.2` |
| **`@tty-pt/sepal`** | `libsepal.so` | `sepal_napi.c` | `sepal.wasm` | `<ttypt/iso.h>` only | `1.5.2` |
| **`@tty-pt/xylem`** | `libxylem.so` | `xylem_napi.c` | `xylem.wasm` | `<ttypt/iso.h>` only | `1.8.1` |
| **`@tty-pt/pitch`** | `libpitch.so` | `pitch_napi.c` | `pitch_wasm.c` | `<ttypt/iso.h>` only | `1.0.1` |
| **`@tty-pt/lamina`** | `liblamina.so` | `lamina_napi.c` | `lamina.wasm` | `<ttypt/iso.h>` only | `1.1.1` |
| **`@tty-pt/chaff`** | `libchaff.so` | `chaff_napi.c` | `chaff_wasm.c` | `<ttypt/iso.h>` only | `1.1.0` |
| **`@tty-pt/areole`** | `libareole.so` | `areole_napi.c` | `areole_wasm.c` | `<ttypt/iso.h>` only | `1.1.0` |
| **`@tty-pt/hyle`** | `libhyle.so` | `hyle_napi.c` | `hyle.wasm` | `<ttypt/iso.h>` only | `1.7.4` |
| **`@tty-pt/hyle-bud`** | `libhyle-bud.so` | JS stub only | Compiled in `site.wasm` / `list.wasm` | `<ttypt/iso.h>` + `bud.h` | `1.2.1` |
| **`@tty-pt/hyle-source`** | `libhyle-source.so` | `hyle_source_napi.c` | `hyle-source.wasm` | `<ttypt/iso.h>` only | `1.4.1` |
| **`@tty-pt/hyle-react`** | N/A (TS/React) | N/A (React UI) | Uses `@tty-pt/hyle-source` | Zero C/Bud deps | `1.0.0` |

*(Additional sibling service/runtime packages in `external/*/npm/`: `@tty-pt/axil-auth` `1.3.4`, `@tty-pt/axil-hyle` `1.1.4`, `@tty-pt/auren` `1.5.3`, `@tty-pt/jarvis` `1.0.0`, `@tty-pt/mm` `1.0.0`, `@tty-pt/scribe` `0.1.0`.)*

---

## 6. Phased Implementation Roadmap

### Phase 1: Bootstrap `@tty-pt/hyle-source` ✅ Completed
- Scaffolded `external/libhyle-source/npm/` packaging with `package.json`, `hyle-source.pc`, and TypeScript definitions.
- Implemented `native/hyle_source_napi.c` Node-API addon with dataset CRUD and querying.
- Implemented `npm/wasm/hyle_source_wasm.c` compiling to `npm/dist/hyle-source.wasm` using `<ttypt/iso.h>` on the shared Stele memory substrate.
- Published `@tty-pt/hyle-source` (`v1.4.1`).

### Phase 2: Framework-Agnostic Substrate Caching & Reactive Event Bus ✅ Completed
- Implemented two-tier caching in `external/libhyle-source/src/cache.c`:
  1. Query Slice Cache with TTL & staleness (`hyle_source_cache_put_ttl`, `hyle_source_cache_hydrate`, `hyle_source_cache_get_status`).
  2. Entity Record Cache (`hyle_source_entity_cache_put`, `hyle_source_entity_cache_get`, `hyle_source_entity_cache_invalidate`).
- Added canonical query string normalization (`normalizeQuery`) and automatic mutation invalidation on `putItem`/`updateItem`/`deleteItem`, plus `optimisticUpdate` with rollback handles.
- Added reactive subscription bus (`hyle_source_subscribe` in C; `subscribeDataset` / `createHyleStore` in Node-API & WASM) and React 19 `useSyncExternalStore` adapter (`useHyleDataset` in `@tty-pt/hyle-react`).

### Phase 3: Develop `@tty-pt/hyle-react` Component Suite ✅ Completed
- Created `external/libhyle-react/` and published `@tty-pt/hyle-react@1.0.0`.
- Implemented `<HyleTable>`, `<HylePagination>`, `<HyleFilterBar>`, `<HyleInput>`, `<HyleField>`, `<HyleForm>`, `useHyleDataset`, and `useHyleTable`.

### Phase 4: Integration into `~/duna` (Partially Completed)
- ✅ Added `@tty-pt/corm`, `@tty-pt/joint`, `@tty-pt/islet`, `@tty-pt/lemma`, `@tty-pt/sepal`, `@tty-pt/stele`, `@tty-pt/hyle`, `@tty-pt/hyle-source`, and `@tty-pt/hyle-react` to `/home/quirinpa/duna/package.json`.
- ✅ Adopted `<HyleTable>` and `<HylePagination>` in `ManagerMemberDirectory.tsx`, `PortalActivityLedger.tsx`, and `ManagerEnquiriesSection.tsx`.
- ⬜ Optional remaining work: migrate `/home/quirinpa/duna/lib/cormStore.ts` from direct `CormDb`/`JointTemporalAxis` calls to structured `@tty-pt/hyle-source` datasets (and adopt `<HyleFilterBar>` / `<HyleForm>` where helpful).

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

