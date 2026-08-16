# Site

Pure-C music site (poem, song, songbook, choir) on top of the axil HTTP library,
the libxylem XY module system, and the bud HTML builder.

---

## Quick Start

```bash
make              # build everything
AUTH_SKIP_CONFIRM=1 make watch   # auto-rebuild + restart on :8080
make unit-tests   # axil must be running on :8080
make pages-test   # page-render smoke tests (curls :8080 for DOCTYPE)
make e2e-tests    # Playwright via Deno — needs server running
make lint         # clang-tidy (max 4 indent levels enforced)
make format       # clang-format on all .c/.h
```

Server self-serves from the repo root as a chroot. Required data dirs are
`items/poem/items`, `items/song/items`, `items/songbook/items`, `items/choir/items`.

## Architecture

**Runtime:**
- `axil` on port `8080` handles HTTP, auth, sessions, uploads, and business logic.
- All request handlers are C functions compiled into `mods/*/*.so`.
- **No Rust/Dioxus SSR and no Deno/Fresh in the request path.** The only WASM is
  C compiled to `wasm32-wasi` (reactor model) via the `build.mk` WASI rule, used
  for browser-side detail-page enhancement. Ignore any doc claiming a Rust SSR
  path (`mods/ssr`, `ssr.so`, wasm-bindgen) — it was removed; those mentions are
  stale.

**Module load order (enforced by `mods/core/core.c` `xy_install`):**

```
core.so
 ├── common.so     response/page/CSRF/storage helpers (XY_DECL in common.h)
 │   └── mpfd.so   multipart/form-data parser (mods/mpfd/mpfd.c)
 ├── source.so     dataset CRUD + /api/dataset/* (mods/source)
 └── mods.load → poem, songbook, bud_demo, song, choir
      ├── poem → index → auth, common, mpfd
      ├── songbook → index, mpfd, song, source, choir
      ├── bud_demo (no loads; registers GET:/bud-demo)
      ├── song → index, mpfd
      └── choir → index, mpfd, song
```

- `index` registers `GET:/` + the default handler and, via `index_open()`, the
  generic `/module/*` CRUD routes; `auth` registers `/api/csrf`,
  `/auth/login`, `/auth/register` and dlopens `libaxil-auth.so`.
- `mods/redir/` still exists but is **never loaded** — its `/sb`→`/songbook`
  (303) and `/chords`→`/song` (301) redirects were duplicated verbatim into
  `core.c`. Fix/remove in core.c if the redirects change.

Modules are loaded via `xy_load()` → `dlopen(RTLD_NOW | RTLD_LOCAL | RTLD_NODELETE)`,
and **all `.so` files are dlopen'd BEFORE the process chroots**. Consequences:
- Cross-.so calls MUST go through the XY dispatch mechanism, never plain `extern`.
- Native deps resolve from the **host** root, not the chroot — e.g. GNU libiconv
  (if ever needed for `axil_slugify`) must be installed on the host.

**Library layout (external/):**
- Git submodules: `axil`, `axil-auth`, `hyle`, `libqmap`, `libxylem`, `mk`.
- Plain tracked dirs: `bud`, `stoma`. `external/qmap` is an untracked symlink to
  `external/libqmap` (don't commit it).
- Dependency chain: `axil → libxylem → libqmap → libqsys` (system);
  `hyle → stoma → libqmap`; `libhyle-bud → hyle + bud + libqmap`.

### Component abstraction limits (user requirement — do not erode)

- **hyle is the framework-neutral data layer** (`external/hyle/src` +
  `include/hyle`): schema, records, query parse, filtering. It must stay free of
  any bud/component symbols — verified today, keep it that way. It may move to
  WASM-only later, so the SSR output it enables must be ordinary HTML with no
  hidden runtime dependency.
- **The bud binding is a separate layer**: `external/hyle/c/libhyle-bud` (and
  only this dir) may depend on bud. Put all bud-specific filter/table HTML here.
- **bud is one concrete SSR component implementation, not the only one.** A
  React/Dioxus consumer must be able to implement components against the same
  server contract.
- **Client-side is SSR-first + progressive enhancement.** SSR emits plain,
  no-JS-usable HTML (native form controls + CSS classes + `data-*` hooks); the
  interactive layer (vanilla JS, React, or bud's WASM bridge) is optional and
  must not be required. Never make a widget depend on the bud patch/op stream
  (`BudWasmBridge`) to function — it is bud-specific.

### Framework-pair model (user requirement — keep VERY clear)

Each library is independent; component frameworks are used as **pairs** (the
same framework on both sides):

```
hyle (data layer, neutral) → SSR contract (plain HTML + data-*) → pairs:
  bud stack:   hyle-bud SSR (C)  +  bud WASM bundle (client)
  React stack: React SSR         +  React hydration (no bud bridge)
```

- **The SSR markup + hyle's query API are the ONLY cross-framework interface**
  (see `docs/SSR-CONTRACT.md`). `data-bud-id`/`data-bud-on` and the patch op
  stream are **bud-stack-internal**, additive, never part of the contract.
- **Mixed stacks (bud SSR + React client) are an outlier** — no shared client
  bridge is designed for it; each side implements the same contract.
- **hyle carries metadata opaquely.** UI hints (e.g. filter style) ride the
  site schema strings (`docs/SCHEMA.md`); hyle never interprets presentation,
  the component layer does.
- The bud client is the WASM bridge (`docs/WASM-BRIDGE.md`); a React client is
  its own hydration layer. See `docs/C-ISOMORPHIC-BUD.md` for how bud runs one
  renderer on both sides.

**Issue-specific docs live in `docs/`** (C-ISOMORPHIC-BUD, ARCHITECTURE,
SSR-CONTRACT, FILTERS, WASM-BRIDGE, SCHEMA, DESIGN). Read the relevant one
before tackling an issue; update them when behavior changes.

**Client/WASM reality:**
- `htdocs/bud-client.js` + `htdocs/bud-hydrate.js` drive the WASM bridge:
  `body[data-modules]` names a module, `/{module}.wasm` is fetched (a 404 is
  caught silently — poem/choir intentionally have none).
- `*.wasm` assets are built by the `build.mk` WASI rule from the same sources as
  the `.so` (dual-compiled), e.g. `mods/song/ux/detail.c` →
  `htdocs/song_detail.wasm`. Rebuild needs a WASI clang toolchain; `make`
  probes and skips if absent.
- Precedent for framework-agnostic enhancement: `data-detail-viewer-*` in
  `site_ui_viewer_controls` driven by plain JS; `data-menu-toggle` in
  `site_ui_layout`.

## XY Cross-.so Call Convention

Every public function exposed to other modules needs:

1. **Declaration in a shared header** (`#ifndef MODULE_IMPL` guard):
   ```c
   XY_DECL(int, my_func, const char *, arg);
   ```
   This expands to a static inline wrapper that dispatches through `xy_call`.

2. **Implementation in the owning `.c` file** (after `#define MODULE_IMPL`):
   ```c
   XY_IMPL(int, my_func, const char *, arg);
   int my_func(const char *arg) { ... }
   ```
   `XY_IMPL` expands to a non-static function definition PLUS an
   `__attribute__((visibility("default")))` adapter that is auto-registered via
   `AUTO_INIT`. The function itself ends up in the dynamic symbol table.

**Gotcha:** Shared headers with `XY_DECL` use a `#ifndef MODULE_IMPL` guard so
the implementing `.c` file doesn't see the `XY_DECL` inline (which would clash with
the `XY_IMPL` definition). The TPARAM_* flag constants in `common.h` must live
**outside** this guard since both callers AND the common implementation need them.

**Module headers used in this repo:**
- `mods/common/common.h` → guard `#ifndef COMMON_IMPL`
- `mods/auth/auth.h` → guard `#ifndef ITEM_IMPL`

Standard handler registration: `register_standard_item_handlers()` in common.

## Handler Patterns

### `with_item_access` (auth.h)

Simplest way to write a CRUD handler. Handles auth, item lookup, and ownership:

```c
static int handle_sb_edit_get_authorized(int fd, char *body,
                                         const item_ctx_t *ctx, void *user)
{
    // ctx->fd, ctx->username, ctx->id, ctx->item_path, ctx->doc_root
    ...
}

return with_item_access(fd, body, SONGBOOK_ITEMS_PATH,
    ICTX_NEED_LOGIN | ICTX_NEED_OWNERSHIP,
    "Songbook not found", "Forbidden",
    handle_sb_edit_get_authorized, NULL);
```

Flags: `ICTX_NEED_LOGIN`, `ICTX_NEED_OWNERSHIP`, `ICTX_SONG_ID`,
`ICTX_CSRF_MPFD`, `ICTX_CSRF_QUERY`.

### Redirect

```c
axil_header_set(fd, "Location", location);
axil_respond(fd, 303, "");
return 0;
```

Every 303 needs `Connection: close` + `DF_TO_CLOSE` before `axil_head` if headers
haven't been sent yet.

## C Conventions (enforced by `.clang-format` + `clang-tidy`)

- **Tabs** for indentation. One tab per level. No spaces.
- **Maximum 4 levels** of indentation. Run `make lint`.
- `snake_case` for functions/vars, `UPPER_CASE` for macros.
- **C89 style:** declare all local variables at the top of their block, before
  any statements. Never mid-block after a statement.
- System includes first, then local includes.
- `BinPackParameters: true` / `BinPackArguments: true` (args pack across fewer lines).
- `ColumnLimit: 80` still enforced.
- Run `make format` after edits.

### Form parsing

- `axil_query_parse` + `axil_query_param` for `application/x-www-form-urlencoded`
- `call_mpfd_parse` + `call_mpfd_get` for `multipart/form-data` only
- GET handlers must call `axil_query_parse()` with `QUERY_STRING` env var before
  `axil_query_param()`. The `query_db` is emptied every request.
- `mpfd_get` contract: returns the **copied** byte count (not the field length)
  and NUL-terminates whenever there's room. Callers must size buffers to the
  field and trust the return value as the count.

### Memory

- Never `free()` qmap-managed values.
- `qmap_put(map, key, ptr)` takes ownership of `ptr`.
- `QM_REFERENCE` field values are string IDs (slugs), not positions.

## Data-layer invariants

- **Route ALL site row writes through hyle `put`/`del`** (`mods/source`
  `source_update_item`/`source_delete_item`). Writing rows directly into the
  shared qmaps bypasses `stoma_dirty` and freezes the FTS index (new songs
  unsearchable, edited songs keep old tokens). See `mods/source/source.c`.
- Search is **accent-sensitive** by design (user requirement): `stoma_fold`
  (`external/stoma/src/token.c`) lowercases ASCII A-Z and Latin-1 Supplement
  uppercase (U+00C0–U+00DE minus `×`), preserving accents, and is
  iconv/locale-free — `pão` and `pao` are different tokens. Do NOT reintroduce
  iconv TRANSLIT or hand-rolled accent-stripping tables into the search fold,
  and do NOT make it accent-insensitive again. FTS tests must not pin `LC_ALL`
  (that masked a root-server C-locale bug); the fold suites are verified under
  `LC_ALL=C` too. The TRANSLIT + forced-UTF-8-locale pattern survives ONLY in
  `axil_slugify` (`external/axil/src/axil-encode.c`), which must keep producing
  ASCII slugs.
- Multi-ref field values are stored newline-separated; the C `hyle_parse_query`
  creates one filter per repeated `key=val` param (the Rust crate's
  `parse_query_params` instead joins repeated keys with commas — keep the
  discrepancy in mind when touching filter semantics).

## Testing

| Command | What |
|---------|------|
| `make unit-tests` | Runs `test.sh` in each module directory. Needs axil on :8080. |
| `make pages-test` | Page-render HTTP smoke test (curls against :8080 for DOCTYPE). |
| `make e2e-tests` | Playwright via Deno. Needs server + `AUTH_SKIP_CONFIRM=1`. |
| `deno test --allow-all tests/e2e/song-add.test.ts` | Single e2e file. |
| `make hyle-tests` | cargo test on the `external/hyle` Rust workspace. |

**Pre-existing failure:** `songbook` unit-test step 6 (`data.txt non-empty... FAIL: empty`)
fails even on clean `main`. Not caused by any local change.

**e2e prerequisites:** Server running with `AUTH_SKIP_CONFIRM=1` set at startup
(not just at test time). The auth module reads this on boot.

## Debug Logging

All logs captured to `debug/`:
```
debug/builds/    make output with timestamps
debug/runtime/   axil.log (via make watch)
debug/tests/     e2e output with timestamps
```

```bash
make debug-logs            # recent logs tail
make build-capture         # rebuild + save log
make test-capture          # run e2e + save
make test-single-capture TEST=foo.ts
fprintf(stderr, "DEBUG: %s\n", val);   # C debug — check debug/runtime/axil.log
```

## Chroot Setup

The site runs inside a chroot at repo root. Auth module creates `./etc/` files
automatically on first start, but `sh` + its shared libs must exist:

```bash
mkdir -p ./bin ./lib ./lib/x86_64-linux-gnu
cp /bin/sh ./bin/sh
# ldd /bin/sh → copy the libs it needs, typically libnss_files.so.2
```

Remember: module `.so`s are dlopen'd before the chroot, so `DT_NEEDED` deps
resolve from the host — do not chase missing libs inside the chroot.

## Common Pitfalls

1. Every 303 needs `Connection: close` + `DF_TO_CLOSE` before `axil_head`.
2. Do not use `call_mpfd_parse` for url-encoded forms.
3. Do not `free()` qmap-managed memory.
4. `axil_register_handler` is last-registration-wins.
5. Do not commit `*.so`, `*.o`, `*.wasm`, swap files, or Rust `target/`.
6. C frontend changes need module rebuild + server restart.
7. `-m mods/core/core` flag is required to start axil — without it, no handlers register.
8. Shared defines (`TPARAM_*`, etc.) needed by both the implementation and callers
   must go outside the `#ifndef MODULE_IMPL` guard in the header.
9. With `RTLD_LOCAL`, cross-.so calls require XY dispatch. A plain function in
   `common.so` whose address is taken by XY's adapter will be exported as `T` in
   `nm -D` (because `XY_IMPL` gives default visibility), but it is still
   **not** directly callable by name from another `.so` at link time.
10. Stale Rust-SSR claims (`mods/ssr`, `ssr.so`, wasm-bindgen, `song-client.js`)
    appear in old READMEs/docs — the code is pure C. Trust `make` + `mods/` +
    `htdocs/`, not those docs.
