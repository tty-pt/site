# Architecture — the framework-pair model

> Read this first. It states the governing design rule of this repository: the
> **framework-pair model**. Every other doc in `docs/` assumes it.

## 1. The one idea: each library is independent, frameworks come in pairs

The project is a layered set of **independent libraries**. Nothing about the
data layer depends on a component framework, and no component framework is the
"platform's" client runtime.

```
        hyle ── data layer (schema, records, query, filtering, FTS)
           │         framework-neutral. No bud/React/dioxus symbols.
           │         Server contract: query API + schema metadata strings.
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
   schema + the documented `data-*` DOM hooks). See `docs/SSR-CONTRACT.md`.
5. **Client-side enhancement is optional and framework-paired.** No-JS must
   always work. bud's WASM bridge is *bud's* client layer, purely additive,
   never required by the contract.

## 2. Libraries and dependency direction

```
axil ── HTTP, sessions, auth, chroot, uploads
libxylem (XY) ── cross-.so call dispatch (RTLD_LOCAL dlopen before chroot)
qmap ── the data store (qmap_put copies key and value; caller frees originals)
stoma ── tokenization/search fold (accent-sensitive by design; no iconv)
hyle  ── data/schema/query layer, NO component symbols
        deps: stoma, qmap
hyle-bud (external/hyle/c/libhyle-bud) ── the bud binding; ONLY place that
        may depend on bud. deps: hyle, bud, qmap
bud   ── C DOM scaffold + WASM bridge. deps: none of the above
site mods ── assemble axil + XY + hyle(+hyle-bud) + bud
```

Verify the boundary with a grep before committing: `external/hyle/src` and
`include/hyle` must contain no `bud`/`lx_`/`bud_` symbols.

## 3. Module load order

Enforced by `mods/core/core.c` `xy_install`:

```
core.so
 ├── common.so   response/page/CSRF/storage helpers (XY_DECL in common.h)
 │   └── mpfd.so multipart/form-data parser
 ├── source.so   dataset CRUD + /api/dataset/*
 └── mods.load → poem, songbook, bud_demo, song, choir
      ├── poem → index → auth, common, mpfd
      ├── songbook → index, mpfd, song, source, choir
      ├── bud_demo (registers GET:/bud-demo)
      ├── song → index, mpfd
      └── choir → index, mpfd, song
```

- `index` registers `GET:/` and the default handler; `index_open()` adds the
  generic `/module/*` CRUD routes. `auth` registers `/api/csrf`,
  `/auth/login`, `/auth/register` and dlopens `libaxil-auth.so`.
- `mods/redir/` exists but is never loaded; its `/sb` and `/chords` redirects
  were duplicated into `core.c`.

Modules are `xy_load()` → `dlopen(RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE)` **before
the process chroots**. Consequences:
- Cross-.so calls MUST use the XY dispatch mechanism (`XY_DECL`/`XY_IMPL`),
  never plain `extern`.
- Native deps (e.g. GNU libiconv for `axil_slugify`) resolve from the **host**
  root, not the chroot.

## 4. Runtime & request path

- **axil** on :8080 handles HTTP/auth/sessions/uploads; all request handlers are
  C compiled into `mods/*/*.so`.
- **No Rust/Dioxus SSR and no Deno/Fresh in the request path.** The only WASM is
  C compiled to wasm32-wasi (reactor model) for browser-side enhancement
  (`bud-client.js` loads `/{module}.wasm` per `body[data-modules]`). Ignore any
  stale doc claiming a Rust SSR path (`mods/ssr`, `ssr.so`, wasm-bindgen).
- The site self-serves from the repo root as a chroot; required data dirs:
  `items/poem/items`, `items/song/items`, `items/songbook/items`,
  `items/choir/items`.
- `htdocs/` is the static web root (`styles.css`, `hyle.css`, `*.wasm`,
  `bud-client.js`). `site_ui_page()` only emits `bud-client.js` +
  `data-modules` when given a non-empty module name — plain list pages stay
  pure SSR until they opt into a wasm bundle.

## 5. XY cross-.so convention (short form)

1. Shared header declares it behind `#ifndef MODULE_IMPL`: `XY_DECL(int,
   my_func, const char *, arg);` → static inline wrapper dispatching through
   `xy_call`.
2. Owning `.c` (after `#define MODULE_IMPL`): `XY_IMPL(int, my_func, const
   char *, arg);` then the definition. `XY_IMPL` produces the public function +
   a default-visibility auto-registered adapter.
3. Shared constants needed by callers AND impl go outside the `#ifndef
   MODULE_IMPL` guard.

Handler registration helper: `register_standard_item_handlers()` in common;
`with_item_access(fd, body, path, flags, notfound, forbidden, cb, user)` in
auth.h handles login/ownership/CSRF.

## 6. Data-layer invariants

- Route ALL row writes through hyle `put`/`del` (`mods/source`
  `source_update_item`/`source_delete_item`). Writing rows directly into shared
  qmaps bypasses `stoma_dirty` and freezes the FTS index.
- Search is **accent-sensitive** by design: `stoma_fold` lowercases ASCII and
  Latin-1 uppercase, preserving accents; `pão` ≠ `pao`. No iconv TRANSLIT in the
  search fold; don't make it accent-insensitive. (TRANSLIT survives ONLY in
  `axil_slugify`, which must keep producing ASCII slugs.)
- Multi-ref field values are stored **newline-separated**; the C
  `hyle_parse_query` creates one filter per repeated `key=val` param (the Rust
  crate instead joins with commas — keep the discrepancy in mind).
- Searchable/FTS pre-filtering and multi-ref pre-filtering happen in
  `hyle_source_query` before `hyle_apply_view`. See `docs/FILTERS.md`.

## 7. Client-side reality

- `body[data-modules]` names a module; `/{module}.wasm` is fetched (a 404 is
  caught silently — poem/choir intentionally have none).
- **The list pages now ship `data-modules="list"`** → `htdocs/list.wasm`
  (from `mods/index/ux/list.c`); poem/choir load it too and the enhancement
  no-ops (no widget nodes). See `docs/C-ISOMORPHIC-BUD.md`.
- `.wasm` assets are dual-compiled from the same sources as the `.so` (see
  `docs/C-ISOMORPHIC-BUD.md`); `make` probes for a WASI clang and skips if
  absent.
- Precedent for framework-agnostic enhancement: `data-detail-viewer-*`
  (`site_ui_viewer_controls`, plain JS) and `data-menu-toggle`
  (`site_ui_layout`).

## 8. Build / test / debug quick reference

| Command | What |
|---------|------|
| `make` | build everything |
| `AUTH_SKIP_CONFIRM=1 make watch` | auto-rebuild + restart on :8080 |
| `make unit-tests` | per-module test.sh (needs axil on :8080) |
| `make pages-test` | page-render smoke tests (curl DOCTYPE) |
| `make e2e-tests` | Playwright via Deno (needs server, `AUTH_SKIP_CONFIRM=1`) |
| `make hyle-tests` | cargo test on external/hyle |
| `make lint` / `make format` | clang-tidy (max 4 indent levels) / clang-format |
| `make -C external/hyle` | hyle C tests |
| `make wasm-debug` | rebuild wasm with `-DBUD_DEBUG` |

Logs: `debug/builds/`, `debug/runtime/axil.log`, `debug/tests/`.

Known pre-existing failure: `songbook` unit-test step 6 (`data.txt empty`)
fails on clean main — unrelated to any change.

## 9. Related docs

- `docs/C-ISOMORPHIC-BUD.md` — writing one renderer for SSR + wasm.
- `docs/SSR-CONTRACT.md` — the framework-neutral component contract.
- `docs/FILTERS.md` — multi-ref / filter semantics.
- `docs/WASM-BRIDGE.md` — bud-client.js / bud-hydrate.js / bud_wasm_app.c.
- `docs/SCHEMA.md` — schema metadata strings and hints.
- `docs/DESIGN.md` — encapsulation/abstraction philosophy and the
  "evoke, don't reimplement" patterns.
