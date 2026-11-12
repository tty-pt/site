# Architecture — the framework-pair model

> Read this first. It states the governing design rule of this repository: the
> **framework-pair model**. Every other doc in `docs/` assumes it.

## 1. The one idea: each library is independent, frameworks come in pairs

The project is a layered set of **independent libraries**. Nothing about the
data layer depends on a component framework, and no component framework is the
"platform's" client runtime.

```
        hyle ── data layer (canonical schema hyle_schema_desc_t, query, filtering, FTS)
           │         framework-neutral. No bud/React/dioxus symbols.
           │         Server contract: query API + schema metadata strings.
           ▼
 libhyle-source ── dataset persistence, DSV, JSON state, pluggable store drivers
           │
           ▼
   SSR contract ── plain HTML + native form controls + data-* hooks.
           │         The ONLY cross-framework interface.
           │
   ┌───────┴───────────┬──────────────────┐
   ▼                   ▼                  ▼
bud stack           React stack        dioxus/other (future)
   │                   │
   ├─ SSR: hyle-bud   ├─ SSR: React components
   │   (C, native .so)│
   └─ client: bud     └─ client: React hydration
       WASM bundle        (NO bud bridge involved)
       (bud bridge)
```

Rules derived from the model:

1. **A framework is used on both sides.** The normal case is one framework for
   SSR and its own client runtime (bud↔bud-WASM, React↔React-hydration).
2. **The mixed case (bud SSR + React client, etc.) is an outlier.** It is not
   supported by a shared client bridge. If it ever happens, each side simply
   implements the same SSR contract independently.
3. **hyle stays neutral.** It carries metadata (including UI hints like the
   filter style) **opaquely** — it does not interpret presentation; the
   component layer decides.
4. **The SSR markup is the contract.** A React consumer must be able to
   implement components against the same server contract (hyle query API +
   schema + the documented `data-*` DOM hooks). See `docs/DESIGN.md` and `docs/WEB_HAL_SPECIFICATION.md`.
5. **Client-side enhancement is optional and framework-paired.** No-JS must
   always work. bud's WASM bridge is *bud's* client layer, purely additive,
   never required by the contract.

## 2. Libraries and dependency direction

```text
               axil (HTTP/WS Daemon & Host)
                 │  ▲
                 ▼  │ (via libxylem hooks)
             libxylem (Dynamic Module Extensibility)
                 │
                 ▼
  Browser WASM <─── libbud ───> Native SSR (Isomorphic DOM)
                      ▲
                      │ (via libhyle-bud bridge)
                   libhyle (Data & Schema Kernel)
                      │
                      ▼
                   libcorm (Core In-Memory Map Store & Recall Engine)
                      │
     ┌────────────────┼────────────────┬────────────────┐
     ▼                ▼                ▼                ▼
  libjoint         libislet         liblemma         libsepal
 (Time Axis)      (Space Axis)     (Text Axis)     (Meaning Axis)
```

Core repositories:
- [axil](https://github.com/tty-pt/axil) — HTTP(S), WebSocket, and network daemon library
- [libxylem](https://github.com/tty-pt/libxylem) — Dynamic module loader and hook isolation
- [libbud](https://github.com/tty-pt/libbud) — Pure C DOM scaffold for SSR + pure VDOM diff engine
- [libstele](https://github.com/tty-pt/libstele) — Universal single-instantiation WASM kernel substrate and slot orchestration ABI
- [libhyle](https://github.com/tty-pt/hyle) — Data schema and query kernel
- [libcorm](https://github.com/tty-pt/libcorm) — In-memory map store and recall kernel
- [libjoint](https://github.com/tty-pt/libit) — Temporal presence interval tree (Time axis)
- [libislet](https://github.com/tty-pt/libgeo) — Multi-dimensional Morton code spatial index (Space axis)
- [liblemma](https://github.com/tty-pt/liblemma) — Accent-preserving lexical tokenizer (Text axis)
- [libsepal](https://github.com/tty-pt/libsepal) — Semantic vector store & 2-stage ANN (Meaning axis)

```
axil ── HTTP, sessions, auth, chroot, uploads
libxylem (XY) ── cross-.so call dispatch (RTLD_LOCAL dlopen before chroot)
corm ── the data store (corm_put copies key and value; caller frees originals)
lemma ── tokenization/search fold (accent-sensitive by design; no iconv)
hyle  ── pure data/query kernel: canonical hyle_schema_desc_t, query/filter/FTS,
        generic registry/ordered/derive machinery, FFI row API, qs helpers
        (hyle_qs_param in hyle/url.h). NO component symbols. deps: lemma, corm
libhyle-source (external/libhyle-source) ── standalone persistence engine:
        registry access + CRUD, ordered/derive wrappers, option resolution,
        DSV, JSON state overlays, pluggable drivers
        (hyle_source_store_ops_t: store_fs, store_mem, custom stores), and the
        picker DTOs (`hyle-source/picker.h` — `hyle/picker.h` no longer exists).
        deps: hyle, corm, lemma, json-c
hyle-bud (external/libhyle-bud) ── the bud binding; ONLY place that
        may depend on bud. Bridges Hyle schemas to Bud UI components.
        deps: hyle, bud, corm. Used in UX for filters/tables (`index`/`gig`/`grp`
        link `HYLE_BUD_WASM_SRC` and include `<hyle-bud/hyle-bud.h>`) and by
        `mods/source` for the picker fragment route (`/pick/:id/options`).
bud   ── pure C DOM scaffold, 5-field UI state binder (bud_field_desc_t),
        and pure VDOM diff engine (bud_vdom_diff). deps: none of the above
stele ── universal single-instantiation WASM reactor substrate, slot
        orchestration (`STL_SLOT_PAGE`, `STL_SLOT_CHROME`), module lifecycle,
        and Web HAL host imports (<ttypt/stele_wasm.h>).
axil-hyle (external/axil-hyle) ── framework-neutral Axil↔Hyle REST connector:
        `GET|POST|PUT|DELETE /api/dataset/...` (JSON CRUD) + partition
        sub-resource routes. deps: axil, axil-auth, corm, hyle, libhyle-source,
        json-c. No bud/hyle-bud symbols — the bud-rendered `/pick/:id/options`
        fragment endpoint is mounted by `mods/source` instead.
site mods ── assemble axil + XY + hyle(+libhyle-source+libhyle-bud) + bud
```

Verify the boundary with a grep before committing: `external/libhyle/src` and
`include/hyle` must contain no `bud`/`lx_`/`bud_` symbols. `external/libbud/include`
and `external/libbud/src` must contain no database/storage symbols.

## 3. Module load order

Independent modules that explicitly declare immediate dependencies via `xy_load` in `xy_install`. `mods/core/core.c` loads only host foundations (`common` + `source`; `common` itself `xy_load`s `mpfd`) then `mods.load`:

```
core.so
 ├── common.so   response/page/CSRF/storage helpers, declarative forms (XY_DECL in common.h)
 │    └── mpfd.so     multipart/form-data parser (xy_load in common)
 ├── source.so   thin XY/HTTP transport wrapper over libhyle-source
 └── mods.load → poem, song, grp, gig           (order respects DAG)
      ├── poem → index
      ├── song → index, mpfd
      ├── grp  → index, mpfd, song
      ├── gig  → index, mpfd, song, source, grp

 plus:
  index → common, auth, mpfd, source
  auth  → common, libaxil-auth (external, not ./mods/*)
```

Site modules declare their own immediate **true** `xy_load` deps (not centrally owned by `core` — maximally independent does not mean zero deps). `mods.load` order `poem→song→grp→gig` respects `song→grp→gig`; `libxylem` deduplicates repeated loads. External deps like `libaxil-auth` are marked external.

- `index` registers `GET:/` and default handlers; `index_open()` or `index_module_init()`
  adds the generic `/module/*` CRUD routes (add GET/POST, edit GET/POST, detail, list)
  automatically from schema descriptors. `auth` registers `/api/csrf`,
  `/auth/login`, `/auth/register` and dlopens `libaxil-auth.so`.
- `mods/redir/` exists but is never loaded; its `/sb` and `/chords` redirects
  were duplicated into `core.c`.

- `mods/site_chrome/` is **WASM-only** (`WASM_ONLY=1` in its Makefile) — it
  produces `htdocs/site_chrome.wasm` for global chrome enhancement but has no
  native `.so` and is not in `mods.load`. It is not part of the native module
  load order.

Modules are `xy_load()` → `dlopen(RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE)` on
**both sides of the process chroot**: the `-m` chain, engine deps, handlers
and the DB open resolve from the host root, while the engine's persisted
region restore — `st_init()` (one `xy_load()` per `st` row) — runs from the
`on_axil_post_chroot()` hook (`external/axil/src/libaxil.c`, between
`init_pre_bind()`'s chroot/chdir and the first bind) and resolves inside the
jail. There is no boot-time module list: game modules load only via region
modding. The jail is seeded and gated by `scripts/seed-jail.sh` /
`scripts/check-jail.sh` (see `docs/BUILD.md`); the manifest lives in
`scripts/jail-manifest.sh`.
`xy_reload()` `external/libxylem/src/libxylem.c:804,828` copies rebuilt `.so` to a unique inode via `mkstemps` (`dir/.xylem-XXXXXX.so` → `/tmp/...`) then `dlopen(tmp)` only when `xy_reloading==1` (`libxylem-module.c:226` `papi.h:145` `tmp_load_path` + `fchmod`/`fsync`), initial loads stay direct — `RTLD_NODELETE` kept but reload no longer reuses old `link_map`. Consequences:
- Cross-.so calls MUST use the XY dispatch mechanism (`XY_DECL`/`XY_IMPL`),
  never plain `extern` (`RTLD_LOCAL` in `libxylem.c:813` means `bud_adapter` etc. must be via `XY` in `common.so`).
- Native deps (e.g. GNU libiconv for `axil_slugify`) resolve from the **host**
  root, not the chroot; there is no `LD_LIBRARY_PATH`, so a tmp-dlopened module
  resolves the *installed* `libxylem` from the system search path — the same
  copy everything linked against (`docs/BUILD.md`, one-authority rule).

## 4. Runtime & request path

- **axil** on :8080 handles HTTP/auth/sessions/uploads; all request handlers are
  C compiled into `mods/*/*.so`.
- **No Rust/Dioxus SSR and no Deno/Fresh in the request path.** The only WASM is
  C compiled to wasm32-wasi (reactor model) for browser-side enhancement
  (`stele-client.js` loads `/{module}.wasm` per `body[data-modules]`). Ignore any
  stale doc claiming a Rust SSR path (`mods/ssr`, `ssr.so`, wasm-bindgen).
- The site self-serves from the repo root as a chroot; required data dirs:
  `var/poem`, `var/song`, `var/gig`,
  `var/grp`.
- `htdocs/` is the static web root (`styles.css`, `hyle.css`, `*.wasm`,
  `stele-client.js`). `site_ui_page()` only emits `stele-client.js` +
  `data-modules` when given a non-empty module name — plain list pages stay
  pure SSR until they opt into a wasm bundle.

## 5. XY cross-.so convention (short form)

See `CONVENTIONS` for the normative recipe. Summary:

1. Shared types, structs, constants, and enums live in `M-types.h` with zero
   `XY_DECL`s.
2. Public caller header `M.h` includes `M-types.h` and declares hooks via
   unconditional `XY_DECL(...)` (no `#ifndef MODULE_IMPL` guards).
3. Owning `.c` includes `M-types.h` (never `M.h`) and defines hooks via
   `XY_IMPL(...)`. Dual-role co-implementors include `M-types.h` and declare
   called hooks with local `XY_DECL`s.

Handler registration helper: `register_standard_item_handlers()` in common;
`with_module_item_access(fd, body, module, flags, notfound, forbidden, cb,
user)` in auth.h resolves the validated item path and handles
login/ownership/CSRF.

Auth is the single ownership authority. In production (`geteuid() == 0` /
chrooted / `AUTH_ENV=prod`), ownership follows POSIX filesystem disk permissions
(`stat` `st_uid` and `chown`) strictly without creating or checking owner
files. `item_owner_read` resolves the username from the directory's UID via
`auth_get_username_by_uid`, and `item_owner_check` validates against the user's
registered UID. In dev mode (unprivileged non-root), owner files are used as a
local multi-user simulation fallback. Legacy root-created items are migrated
offline with `scripts/migrate-owner-files.sh --apply`, then refreshed by
restarting the server.

## 6. Data-layer invariants

- Route ALL row writes through hyle `put`/`del` (`mods/source`
  `source_update_item`/`source_delete_item` $\rightarrow$ `libhyle-source`
  `hyle_source_put`/`hyle_source_del`). Writing rows directly into shared
  corms bypasses `lemma_dirty` and freezes the FTS index.
- Pluggable storage drivers: `libhyle-source` operates through `hyle_source_store_ops_t`
  (`scan`, `load`, `put`, `put_field`, `del`), allowing filesystem persistence (`store_fs`),
  memory persistence (`store_mem`), or custom database engines.
- Search is **accent-sensitive** by design: `lemma_fold` lowercases ASCII and
  Latin-1 uppercase, preserving accents; `pão` ≠ `pao`. No iconv TRANSLIT in the
  search fold; don't make it accent-insensitive. (TRANSLIT survives ONLY in
  `axil_slugify`, which must keep producing ASCII slugs.)
- Multi-ref field values are stored **newline-separated**; the C
  `hyle_parse_query` creates one filter per repeated `key=val` param (the Rust
  crate instead joins with commas — keep the discrepancy in mind).
- Item sources may carry a borrowed, static `source_list_view_t`. Feature
  modules own its ordered fields, labels, display name, default sort, and
  optional content-search presentation. Native `list_fill_state` resolves the
  descriptor and serializes only resolved strings into the SSR/WASM state;
  index does not switch on feature module names.
- Searchable/FTS pre-filtering and multi-ref pre-filtering happen in
  `hyle_source_query` before `hyle_apply_view`. See `docs/FILTERS.md`.

## 7. Client-side reality

- `body[data-modules]` names a module; `/{module}.wasm` is fetched (a 404 is
  caught silently — poem/grp intentionally have none).
- **The list pages now ship `data-modules="list"`** → `htdocs/list.wasm`
  (from `mods/index/ux/list.c`); poem/grp load it too and the enhancement
  no-ops (no widget nodes). See `docs/WEB_HAL_SPECIFICATION.md`.
- `.wasm` assets are dual-compiled from the same sources as the `.so` (see
  `docs/WEB_HAL_SPECIFICATION.md`); `make` probes for a WASI clang and skips if
  absent.
- Precedent for framework-agnostic enhancement: `data-detail-viewer-*`
  (`site_ui_viewer_controls`, plain JS) and `data-menu-toggle`
  (`site_ui_layout`).

### 7.1 The Two-Header Rule for Isomorphic C & WASM
- **`<ttypt/iso.h>`**: Compiler export/import attributes (`ISO_EXPORT`, `ISO_EXPORT_AS`, `ISO_IMPORT`). Zero runtime dependencies. Standalone libraries (`libbud`, `libpitch`, etc.) include only `iso.h`.
- **`<ttypt/stele_wasm.h>`**: Single-instantiation WASM reactor substrate, UI slots (`STL_SLOT_PAGE`, `STL_SLOT_CHROME`), module registration (`STL_WASM_MODULE`), and Web HAL host imports. Transitively includes `<ttypt/iso.h>`. Interactive UX modules (`mods/*/ux/*.c`) include only `stele_wasm.h`.

### 7.2 Zero-Copy Shared Memory Chaining Across Packages
- **Unified Address Space**: Modules linking with `-Wl,--import-memory` (`@tty-pt/corm`, `@tty-pt/joint`, `@tty-pt/sepal`) share `@tty-pt/stele`'s linear memory space.
- **Zero-Copy Accessors**:
  - `CandidateSet` (`@tty-pt/corm`): `.rawPointer()`, `.count()`, `.toUint32Array()`, and `.addFromRaw(ptr, count)`.
  - `JointTimeline` (`@tty-pt/joint`): `.rawPointer(start, end)` returning `{ ptr, count, free }`, `.count(start, end)`, and `.withOverlaps(start, end, fn)`.
- **Pointer Handoff**: Intersecting intervals directly seed candidate sets or rankers without unpacking into intermediate JavaScript arrays.

### 7.3 Parallel Streaming Hydration
- **Streaming First with Fallback**: `htdocs/stele-client.js` and `@tty-pt/stele/client.js` instantiate WASM binaries via `WebAssembly.instantiateStreaming(fetch(url))`, with automated fallback to `fetch().then(r => r.arrayBuffer()).then(b => WebAssembly.instantiate(b))` when streaming is unsupported or MIME types differ.
- **Concurrent Mounting**: Slot 0 (`site_chrome`), Slot 1 (`pageModule`), and substrate kernel boot execute concurrently via `Promise.all`.

### 7.4 Centralized WASM Build Pipeline & wasm-opt
- **Standardized Rules**: `external/mk/include.mk` centralizes `make wasm` with `-ffreestanding -flto -O3 -D__wasm__ -fvisibility=hidden`.
- **Macro Extraction**: Auto-extracts exports from `ISO_EXPORT` and `ISO_EXPORT_AS` with dual GNU Make and BSD Make portability.
- **Binaryen Optimization**: Post-link `wasm-opt -O3` reduces binary footprints by 15% to 30%.
- **Single Allocator Invariant**: Only `@tty-pt/stele` exports `malloc` and `free`; downstream modules import allocator routines via `env` or use Stele substrate memory helpers. Non-colliding global bases (`-Wl,--global-base=1048576`) isolate static data segments.

## 8. Build / test / debug quick reference

See `docs/BUILD.md` and `docs/TESTING.md` for commands.
Logs: `debug/builds/`, `debug/runtime/axil.log`, `debug/tests/`.

Gig unit-test step 6 (`data.txt` seeding) passes after the 2026-08-21 source
data migration.

## 9. Related docs

- `docs/WEB_HAL_SPECIFICATION.md` — isomorphic SSR + WASM substrate and Web HAL bridge contract.
- `docs/FILTERS.md` — multi-ref / filter semantics.
- `docs/SCHEMA.md` — schema metadata strings and hints.
- `docs/DESIGN.md` — encapsulation/abstraction philosophy and the
  "evoke, don't reimplement" patterns.
