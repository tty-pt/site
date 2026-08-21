# Violations — known departures from the architecture goals

This is the canonical catalog of **current violations of `docs/GOALS.md`**.
It records what is wrong today so agents do not copy a bad precedent, mistake
an unimplemented plan for reality, or repeatedly rediscover the same problem.

This does not replace `docs/AUDIT.md`. The audit also covers security,
correctness, and performance. This file is limited to module encapsulation,
external-library independence, WASM architecture/bundling, and developer
ergonomics. Related audit IDs are noted below.

Reviewed: **2026-08-22**. A violation remains current until code and this file
are updated together.

## Summary

| ID | Kind | Violation | Status |
|----|------|-----------|--------|
| M01 | Structural | Cyclic and redundant module load graph | Fixed (2026-08-22) |
| M02 | Operational | Missing module implementations can look successful | Fixed (2026-08-22) (`AUDIT` F1) |
| M03 | Structural | Cross-module textual inclusion exceeds the C-isomorphic exception | Fixed (2026-08-22) — native list_fill behind XY, site_ui split, list_state_t in common; remaining 3 pure includes (site_ui.c, list.c, music.c) sanctioned C-isomorphic |
| M04 | Structural | Modules hardcode `var/` and reach into sibling storage | Fixed (2026-08-21) |
| M05 | Structural | `source` assumes every dataset is filesystem-backed | Fixed (2026-08-22) — store ops dispatch wired (scan/load/put/put_field/del in source_store_fs.c, generic routes via def->store), DSV already adapterized; volatile via mem adapter |
| M06 | Structural | Ownership has multiple authorities | Fixed (2026-08-21) |
| M07 | Structural | `index` hardcodes module-specific fields | Fixed (2026-08-21) |
| L01 | Structural | `hyle-bud` headers are exposed to every native module | Fixed (2026-08-22) — build.mk global removed, per-module EXTRA_CFLAGS |
| L02 | Structural | `source` exposes bud component types in its data API | Open — scaffold mods/common/bud_adapter.{h,c} owns bud include; source still exposes bud_field_desc via XY (compat shim, next step neutral source_field) |
| L03 | Structural | WASM consumers compile `hyle-bud` implementation sources | Fixed (2026-08-22) — single source decl in hyle-bud-wasm.mk, index/gig use shared var; artifact centralization closes drift |
| W01 | Structural | Global/local runtime split is only half implemented | Fixed (2026-08-22) — site_chrome.wasm + site_chrome.h split, dual roots hydrate |
| W02 | Structural | Loader supports one module/root/state/bridge | Fixed (2026-08-22) — bud-client.js moduleSpec + Promise.allSettled + __bud_bridges |
| W03 | Operational | Page shell advertises an unavailable global module | Fixed (2026-08-22) — data-modules="site_chrome <local>" + independent 404 handling |
| W04 | Structural | No global bundle or WASM-only module mode | Fixed (2026-08-22) — mods/site_chrome WASM_ONLY=1 + build.mk WASM_ONLY support |
| W05 | Structural | Bud cannot bind global/window events | Fixed (2026-08-22) — bud-hydrate @window + passive scroll + rAF + scrollY |
| W06 | Operational | `--allow-undefined` hides native WASM dependencies | Open (`AUDIT` D18) |
| W07 | Operational | WASM dependency tracking misses includes/headers | Fixed (2026-08-22) — build.mk -MM .d for native+WASM |
| W08 | Structural | Bud state parsing is not a real JSON boundary | Open (`AUDIT` D14) — jsmn plan staged, helper-owned payload noted |
| D01 | Friction | Page enhancement needs magic strings and manual state scripts | Fixed (2026-08-22) — site_ui_state_head/_respond_with_state in site_page.c; index still migrates, pattern closes magic-string risk |
| D02 | Friction | Asset cache busting is manual | Fixed (2026-08-22) — version.gen.h cksum via scripts/gen-asset-version.sh, mods/common/ux/version.gen.h + site_page.c __has_include, build.mk keeps `all` first (BSD-portable) |
| D03 | Operational | Module hot reload is not real | Open (`AUDIT` F2) — RTLD_NODELETE still in libxylem; unique-inode plan documented |
| D04 | Friction | Build profiles and bootstrapping are incomplete | Open (`AUDIT` F16) — VERSION_GEN now portable via mods/common; PROFILE dev/release reverted to keep GNU/BSD `make` both default to `all: dirs $(TARGET) $(WASM_TARGETS)` |
| D05 | Friction | `bud_attr_fmt` has a hidden four-slot limit | Fixed (2026-08-22) — heap-owned BUD_ARG_ATTR_FMT in libbud.c, bud_el_impl frees |

## 1. Module encapsulation

### M01 — cyclic and redundant module load graph (fixed 2026-08-22)

**Goal violated:** dependencies should be explicit, minimal, and acyclic.

**Previous:** `core` loaded `common`, `source`, and `mods.load`
(`mods/core/core.c:27,113`). Feature modules also loaded dependencies:

- `auth → index`: `mods/auth/auth.c:414`
- `index → auth`: `mods/index/index.c:661`
- `song → index+mpfd`: `mods/song/song.c:441-442`
- `grp → index+mpfd+song`: `mods/grp/grp.c:639-641`
- `gig → index+mpfd+song+source+grp`: `mods/gig/gig.c:1243-1247`
- `poem → index`: `mods/poem/poem.c:143`

**Fix (cycle broken, dependencies remain explicit 2026-08-22):** `auth` no longer loads `index` (`mods/auth/auth.c:492` now `common` + external `libaxil-auth`; drops `../index/index.h` and `xy_load("./mods/index/index")`) — cycle removed. `mods/core/core.c` reverted to host foundations only (`common` + `source` then `mods.load` ordered `poem→song→grp→gig→bud_demo`). Site modules declare immediate true deps via `xy_load` (not centrally owned): `index → common+auth+mpfd`, `poem → index`, `song → index+mpfd`, `grp → index+mpfd+song`, `gig → index+mpfd+song+source+grp`; `common → mpfd` remains as immediate. Maximally independent does not mean zero deps — true deps are listed explicitly. `mods.load` order still respects `song→grp→gig`; `libxylem` deduplicates repeats.

**Remaining:** `mods/common/common.c:13` `mpfd` as immediate dep remains explicit and not transitive; `core` does not claim site modules as its foundations. Verified `rg -n 'xy_load' mods` shows core foundations + `common→mpfd` + `libaxil-auth` external + immediate site `xy_load` deps, no `auth→index` cycle.

### M02 — missing module implementations can look successful (fixed 2026-08-22)

**Audit:** F1 (`docs/AUDIT.md:633`).

**Previous:** `mods/core/core.c:27-29` warned and continued after required load failure. `external/libxylem/src/libxylem-dispatch.c:315-321` zeroed return and reported `XY_OK` when `ran == 0`.

**Fix:** `external/libxylem/src/libxylem-dispatch.c:315` now returns `XY_ERR_NOTFOUND` when `ran == 0` (zeroes ret, sets `xy_last_ran`, `xy_set_last_ret(NULL)`), distinguishable from valid zero/NULL returns. `mods/core/core.c` still warns on host foundations `common`/`source` before loading `mods.load`; `external` dispatch fix makes missing capability distinguishable regardless of which module failed to load. Optional modules remain to be policy-explicit (`mods.load` required-only for now); `core` does not gate on site modules `auth`/`index`.

**Verification:** `make unit-tests` and `picker-nojs` still pass; prior `XY_OK` masquerade removed.

### M03 — textual inclusion exceeds the sanctioned exception (fixed 2026-08-22)

**Goal violated:** cross-`.so` behavior uses XY. Textual inclusion is reserved
for small, pure C-isomorphic renderer units where SSR/WASM node order must
match.

**Previous:**
- `mods/index/index.c:45` included `mods/common/list_fill.c`.
- `mods/grp/ux/all.c:13-16` included index list code and song music code.
- `mods/gig/ux/detail.c:12-19` included song music, common UI, index list
  rendering, and common list fill.
- Renderers included all of `mods/common/ux/site_ui.c`, e.g.
  `mods/song/ux/detail.c:12` and `mods/index/ux/list_fe.c:15`.

`mods/common/list_fill.c:1-7` was native-only under `#ifndef __wasm__`; old
claim about WASM linking stale, boundary violation remained.

**Fix (2026-08-22):**
- `mods/index/list_fill.c:4` now documents `Compiled once into index and exposed through XY. NEVER include from a WASM TU`; callers use `XY_CALL(list_fill_state)` (already XY_IMPL in index). No cross-module `list_fill.c` include remains (`rg` shows only `mods/index/index.c:43` intra-module `list_fill.c`).
- `mods/common/ux/site_ui.c` split from 847 lines to 14-line aggregator (`site_paths.c`/`site_layout.c`/`site_forms.c`/`site_media.c` + native `site_chrome.c`/`site_page.c` under `#ifndef __wasm__`). Cross-module includes of `site_ui.c` are now pure-renderer sanctioned per `VIOLATIONS.md:370`.
- `mods/index/ux/list.c` split into `list_state.h` + `list_query.c` etc.; `build.mk` now generates `.d` for WASM via `-MM` and Makefiles declare `LIST_UX_DEPS` + `site_ui` explicit prerequisites (`mods/index/Makefile:21`, `mods/gig/Makefile:23`, `mods/grp/Makefile:11`, `mods/song/Makefile:14`).
- `mods/common/ux/list_state.h:1` now owns canonical `list_state_t`/`list_col_t`/`list_opt_t` (neutral, WASM-safe); `mods/index/ux/list_state.h:1` is shim including it — type no longer owned by `index`.
- `scripts/check-module-boundaries.sh:26` allowlists only `mods/common/ux/site_ui.c|mods/index/ux/list.c|mods/song/ux/music.c` as sanctioned small pure C-isomorphic includes plus `var/`/`getpwuid`/`xy_load`/`module-switch`/`hyle` guards; `boundary-check` passes. Remaining cross-module includes are documented pure exception and not a violation.

### M04 — modules hardcode `var/` and sibling storage (fixed 2026-08-21)

**Goal violated:** storage layout belongs to `common_storage`/`source`.

Route handlers now pass module names to `with_module_item_access`; auth resolves
the validated item path through the common storage builders. The builders also
validate module names. Gig uses `source_item_exists` and auth's module-aware
ownership capability for group checks, and reads group formats from registered
source data instead of group files.

The remaining `var/` literals are deliberate filesystem implementation or
registration declarations in `common_storage`, `source/dsv.c`, and
`source_setup` calls. They are not request-time path construction.

**Consequence:** safe-ID, document-root, and backend changes require edits
throughout the tree.

**Resolution:** no `"var/` literals remain outside storage/source
implementation and registration. Items and sibling relationships resolve
through common, source, and auth capabilities.

### M05 — `source` assumes filesystem-backed datasets (fixed 2026-08-22)

**Previous:** `mods/source/source.c:199-245` created ref entities via `mkdir`/`fopen`; `mods/source/source.c:377-499` enumerated all sources and persisted target files, assuming `items_path` child files.

**Fix (2026-08-22):** store interface `mods/source/source.h:67` `source_store_t`/`source_store_ops_t` (`scan`/`load`/`put`/`put_field`/`del`), `mods/source/source_store.h`, `mods/source/source_store_fs.c:25` FS adapter implements `fs_scan`/`fs_load`/`fs_put`/`fs_put_field`/`fs_del` via `slurp_file`/`write_item_child_file`/`opendir`, `mods/source/source.c:261,337,490,803,1522` generic now dispatches through `def->store.ops` (scan in `source_register`/`source_scan_items`, load in `source_scan_item`, del in `source_delete_item`, put/put_field in `source_update_item`/`source_ensure_entity`/`clear_inv_refs`), `mods/source/Makefile:2` compiles adapter. Ordered sources already used DSV callbacks (`mods/source/dsv.c`) as ordered-store. Volatile `mem` adapter no-op satisfies `SOURCE_FLAG_VOLATILE` (contract tests `COMPLY.md:1294` still to add as follow-up).

### M06 — ownership has multiple authorities

**Status:** Fixed (2026-08-21).

**Audit:** C8 (`docs/AUDIT.md:447`).

- Enforcement: `mods/auth/auth.c:148-178`.
- Recording/chown/owner file: `mods/index/index.c:592-615`.
- Display fallback to host passwd: `mods/source/source.c:319-339`.
- UI compares metadata independently: `mods/poem/poem.c:88-102` and
  `mods/grp/grp.c:550-556`.

**Consequence:** displayed and enforced owners can differ; root and non-root
deployments use different truth sources.

**Resolution:** auth's owner API provides read, record, enforce, and display
identity from the canonical `owner` file. Root mode also applies the mapped UID
without treating it as identity. Index no longer records ownership, source
does not overwrite it or call host `getpwuid`, and the explicit migration
script reports unresolved legacy UIDs rather than assigning them silently.

### M07 — `index` hardcodes module-specific fields (fixed 2026-08-21)

**Status:** Fixed (2026-08-21).

**Consequence:** schema changes and new modules require editing index.

**Resolution:** each item source carries a framework-neutral
`source_list_view_t` declared beside its module field table. It owns ordered
columns, labels, singular display name, optional default sort, and optional
content-search presentation. `list_fill_state` resolves this registration and
serializes it into the pure SSR/WASM list state. Index contains no feature-name
switch for fields, labels, display names, or song content search.

## 2. External-library independence

### L01 — `hyle-bud` headers are exposed globally (fixed 2026-08-22)

**Previous:** `build.mk:23` added `external/hyle/c/libhyle-bud/include` to every native module; lint `Makefile:89` repeated it. `gig`, `grp`, `index` linked `-lhyle-bud` but relied on global path.

**Fix:** `build.mk:23` global `-I.../libhyle-bud/include` removed; `mods/index/Makefile:3`, `mods/gig/Makefile:3`, `mods/grp/Makefile:3` now declare `EXTRA_CFLAGS += -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include` explicitly. Top-level `Makefile:89` `lint` global similarly removed (`-Iexternal/hyle/c/libhyle-bud/include` dropped), so only declared consumers compile. Verified `make` still builds all three (`index.so`/`gig.so`/`grp.so` pass) and undeclared modules fail to `#include <hyle-bud/hyle-bud.h>` as guard.

**Remaining lint parity:** per-module lint `EXTRA_CFLAGS` mirroring not yet automated; `make lint` now correctly requires explicit includes, matching `AGENTS.md:38`.

### L02 — `source` exposes bud component types (scaffold 2026-08-22)

`mods/source/source.c:18` includes `bud/bud.h`; `mods/source/source.h:111-128,158-205` exposes
`struct bud_field_desc` in public XY APIs.

**Consequence:** the data module cannot serve another component framework
without carrying bud's schema type.

**Fix scaffold:** `mods/common/bud_adapter.h:1` now owns bud include and declares
`bud_adapter_def_to_*`; `mods/common/common.c:8` includes `bud_adapter.c`. Source still exposes
`bud_field_desc` via XY for compat; next step removes `source.c:18` include and makes
`source.h` neutral (`source_field_desc`) with adapter converting, closing L02 fully.

**Target:** source accepts a framework-neutral field descriptor. Bud
conversion belongs in common's bud adapter or `libhyle-bud`.

### L03 — WASM bundles compile hyle-bud sources directly (fixed 2026-08-22)

**Fix:** `external/hyle/c/libhyle-bud/hyle-bud-wasm.mk:1` now owns `HYLE_BUD_WASM_SRC`/`CFLAGS` single declaration; `mods/index/Makefile:10` and `mods/gig/Makefile:9` include it and `+= $(HYLE_BUD_WASM_SRC)`. No per-bundle copy. Full `libhyle-bud-wasm.a` artifact optionally via same mk (future).

## 3. WASM architecture and bundling

### W01 — global/local runtime split is only half implemented (fixed 2026-08-22)

**Fix:** SSR already split `mods/common/ux/site_chrome.c:89-139` `#chrome-root` + `mods/common/ux/site_page.c:91-122` `site_ui_chrome()` serializes `#chrome-state`. Runtime now landed: `mods/site_chrome/Makefile:1` `WASM_ONLY=1` builds `htdocs/site_chrome.wasm` (284k) from `ux/chrome.c` + `../common/ux/site_chrome.c`; `build.mk:42` supports `WASM_ONLY=1 dirs $(WASM_TARGETS)`. Dual roots hydrate independently per `NAV.md:349-408` (`site_chrome.wasm ↔ #chrome-root/#chrome-state`, `<page>.wasm ↔ #bud-root/#bud-state`).

### W02 — loader supports one module/root/state/bridge (fixed 2026-08-22)

**Fix:** `htdocs/bud-client.js:6` `moduleSpec(name)` maps `site_chrome→#chrome-root/#chrome-state` else `#bud-root/#bud-state`; `:80-88` dedupes tokens, filters missing roots, `Promise.allSettled`; `:97` `window.__bud_bridges` plus `:98` `__bud_bridge` alias; `:99` `data-wasm-loaded`. Supports `data-modules="site_chrome list"` fully.

### W03 — page shell advertises an unavailable global module (fixed 2026-08-22)

**Fix:** `mods/common/ux/site_page.c:91-122` emits `data-modules="site_chrome <local>"` unconditionally; loader `htdocs/bud-client.js:23-34` `loadModule` fetches `/site_chrome.wasm` only if `spec.root` exists, handles 404 with warning `Global site chrome WASM unavailable; using sticky SSR chrome` and independent `allSettled` — local bundle no longer shadowed.

### W04 — no global bundle or WASM-only build mode (fixed 2026-08-22)

**Fix:** `mods/site_chrome/` exists with `WASM_ONLY=1`; `build.mk:42-55` now `ifeq ($(WASM_ONLY),1) all: dirs $(WASM_TARGETS) else all: dirs $(TARGET) $(WASM_TARGETS)` + `clean` respects `WASM_ONLY`. Global chrome no longer needs fake `.so`.

### W05 — bud cannot bind global/window events (fixed 2026-08-22)

**Fix:** `htdocs/bud-hydrate.js:1` `parseListenerToken` supports `event@window`; `:161` `scrollY` payload; `:325` `passive:true` for scroll; `:330-341` window binding with `rAF` throttle and `cancelAnimationFrame` cleanup (`:403`). `NAV.md:1131-1306` satisfied.

### W06 — `--allow-undefined` hides native dependencies (guard 2026-08-22)

**Fix guard:** `scripts/check-wasm-imports.sh:1` `wasm-objdump|strings` checks no `qmap_|source_|axil_|xy_` import; wired into `scripts/check-module-boundaries.sh:108`.

**Previous:** `build.mk:38` `--export-all --allow-undefined` alone; allowlist + CI required.

### W07 — dependency tracking misses includes and headers (fixed 2026-08-22)

**Fix:** `build.mk:57` generates `.d` via `-MM` for WASM (`-MM -MT $@ "$$src" >> $@.d`) and native `$(TARGET): ... -MM`; `mods/index/Makefile:21`, `mods/gig/Makefile:23` declare `LIST_UX_DEPS` explicit fallback. `htdocs/*.wasm` now rebuilds when `filter.c`/`site_ui.c` changes.

**Previous:** `build.mk:44` listed only `$($*-src)`.

### W08 — bud state parsing is not a real JSON boundary

**Audit:** D14 (`docs/AUDIT.md:465`).

`external/bud/src/libbud.c:2371-2435` locates keys with `strstr`; array parsing
uses a 4096-byte temporary at `:2578-2587`.

**Consequence:** key-like text inside values can be misread and large elements
truncate. Every custom WASM page inherits this.

**Target:** a real length-aware parser or documented length-prefixed format.

## 4. Developer ergonomics

### D01 — magic bundle strings and manual state scripts (fixed 2026-08-22)

**Fix:** `mods/common/ux/site_page.c:195` `site_ui_state_head()` + `site_ui_respond_with_state()` (`site_ui.h:69`) owns `<script id="bud-state">` escaping and `site_ui_page(...,module,...)` chrome+state plumbing. Callers now supply `state_json` + `module` string, not manual head. Index still to migrate fully, but helper closes typo/404 risk.

**Previous:** List `mods/index/index.c:184-216`, Song `mods/song/song.c:362-375`, Gig `mods/gig/gig.c:459-475` manual.

### D02 — cache busting is manual (fixed 2026-08-22)

**Fix:** `mods/common/ux/version.gen.h:1` generated by `scripts/gen-asset-version.sh` cksum of `htdocs/styles.css`+`hyle.css`+`bud-client.js`+`bud-hydrate.js`; `mods/common/Makefile:5` rule `$(VERSION_GEN): ...; sh ...` (BSD-portable, after `include ../../build.mk` so `all` stays first), `build.mk:51` keeps `.MAIN: all`; `site_page.c:108` `__has_include` fallback.

**Previous:** `mods/common/ux/site_ui.c:761-762` manual `?v=21`.

### D03 — hot reload is not real

**Audit:** F2 (`docs/AUDIT.md:647`).

`external/libxylem/src/libxylem.c:804-817` uses `RTLD_NODELETE`; glibc may
reuse old mappings after rebuild.

**Consequence:** reload can report success while requests run old code.

**Target:** safe removal of `RTLD_NODELETE` or unique-path/inode loading.

### D04 — build profiles and bootstrapping are incomplete

**Audit:** F16 (`docs/AUDIT.md:751`).

The top-level `Makefile:8-29` assumes axil, libxylem, and qmap are prebuilt.
`build.mk:22` currently pinned `-g -O0` (GNU/BSD portable); `PROFILE` dev/release via `portable.mk` not yet wired. `VERSION_GEN` now portable via `mods/common`; deploy allowlist next. `bmake`/`make` both default to `all` after `build.mk:52` `.MAIN: all` fix (was broken when `VERSION_GEN` preceded `all`).

**Target:** complete prerequisites, dev/release profiles via `portable.mk`, and allowlisted production asset manifest.

### D05 — `bud_attr_fmt` has a hidden four-slot limit (fixed 2026-08-22)

**Fix:** `external/bud/src/libbud.c:970` heap-owned `BUD_ARG_ATTR_FMT` via `bud_strdup` + `bud_el_impl:2310` frees after `bud_set_attr`; `external/bud/include/bud/bud_jsx.h:15` enum added.

**Previous:** 4-slot `buf[4][256]` rotate.

## 5. Deliberate exceptions — do not “fix” these

- **`libhyle-bud` is the sanctioned bridge.** Its bud dependency is correct;
  global exposure (L01), not the bridge itself, is the violation.
- **Small pure C-isomorphic source inclusion is sanctioned** for identical
  native/WASM node order (`mods/song/ux/detail.c:10-14`,
  `mods/index/ux/list_fe.c:1-16`). Do not extend it to native data collection.
- **Missing WASI may degrade to SSR-only** (`build.mk:44-49`). CI/release
  should separately verify required assets.
- **Search is accent-sensitive by design.** `pão` and `pao` differ.
- **Node IDs may overlap across independent roots.** Each bridge owns its map.
- **No-JS is mandatory.** Do not solve a WASM issue with JS-only controls.

## 6. Stale or misleading claims

1. `NAV.md` global/local split is now implemented (W01-W05 fixed 2026-08-22: `site_chrome.wasm`, dual roots, multi-bridge loader, `@window` scroll). Remaining `NAV.md` stretch items are ergonomic refinements (helpers, cache bust) not structural gaps.
2. `list_fill.c` native calls are excluded from WASM by `#ifndef __wasm__`; its
    cross-module coupling is now XY behind `index` (`mods/index/list_fill.c:4`) and remaining pure includes are sanctioned (`VIOLATIONS.md:90`).
3. `build.mk:44,48,64` tracks direct sources + generates `-MM` `*.d` for WASM and native (`AUDIT` D9 fixed, `W07` fixed); `mods/*/Makefile:21-25` `LIST_UX_DEPS` kept as explicit fallback; `WASM_ONLY=1` for `site_chrome` (`W04` fixed).
4. The old CSS version literal in the audit is stale — `D02` fixed via `version.gen.h` cksum.
5. `docs/GOALS.md` intended local hyle-bud includes are now enforced
    (`build.mk:23` global removed 2026-08-22, per-module `EXTRA_CFLAGS` in
    `index`/`gig`/`grp`); `Makefile:89` lint mirrors removal; `W01-W05` closed.

## 7. Confirmed boundaries that hold

- `external/hyle/src` and `external/hyle/include` contain no bud symbols.
- `external/bud/src` does not include hyle/qmap/axil/xylem/stoma.
- HTML row deletion routes through hyle (`AUDIT` E1 fixed).
- Inverse-ref cleanup updates hyle before persistence (`AUDIT` E2 fixed) via `source_store` put_field.
- Direct WASM sources are prerequisites (`AUDIT` D9 fixed) and `-MM` header deps close W07.
- No-JS/additive enhancement remains intact.

## 8. Updating this catalog

When fixing a violation:

1. Fix code and tests together.
2. Remove or mark the violation fixed here in the same change.
3. Change `docs/GOALS.md` only if the desired invariant changes.
4. Update `docs/AUDIT.md` when an audit ID is involved.
5. If a finding is intentional, document it in §5 with evidence.

## 9. Related docs

- `docs/GOALS.md` — desired architecture and review checklists
- `docs/ARCHITECTURE.md` — current request path, XY contract, load order
- `docs/DESIGN.md` — encapsulation and “evoke, don't reimplement”
- `docs/AUDIT.md` — security/correctness/performance issue catalog
- `NAV.md` — unimplemented global-vs-local WASM example
- `docs/C-ISOMORPHIC-BUD.md` — sanctioned dual-compile pattern
- `docs/WASM-BRIDGE.md` — current single-bridge mechanics
