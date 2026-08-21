# Violations — known departures from the architecture goals

This is the canonical catalog of **current violations of `docs/GOALS.md`**.
It records what is wrong today so agents do not copy a bad precedent, mistake
an unimplemented plan for reality, or repeatedly rediscover the same problem.

This does not replace `docs/AUDIT.md`. The audit also covers security,
correctness, and performance. This file is limited to module encapsulation,
external-library independence, WASM architecture/bundling, and developer
ergonomics. Related audit IDs are noted below.

Reviewed: **2026-08-21**. A violation remains current until code and this file
are updated together.

## Summary

| ID | Kind | Violation | Status |
|----|------|-----------|--------|
| M01 | Structural | Cyclic and redundant module load graph | Open |
| M02 | Operational | Missing module implementations can look successful | Open (`AUDIT` F1) |
| M03 | Structural | Cross-module textual inclusion exceeds the C-isomorphic exception | Open |
| M04 | Structural | Modules hardcode `var/` and reach into sibling storage | Fixed (2026-08-21) |
| M05 | Structural | `source` assumes every dataset is filesystem-backed | Open |
| M06 | Structural | Ownership has multiple authorities | Fixed (2026-08-21) |
| M07 | Structural | `index` hardcodes module-specific fields | Fixed (2026-08-21) |
| L01 | Structural | `hyle-bud` headers are exposed to every native module | Open |
| L02 | Structural | `source` exposes bud component types in its data API | Open |
| L03 | Structural | WASM consumers compile `hyle-bud` implementation sources | Open |
| W01 | Structural | Global/local runtime split is only half implemented | Open |
| W02 | Structural | Loader supports one module/root/state/bridge | Open |
| W03 | Operational | Page shell advertises an unavailable global module | Open |
| W04 | Structural | No global bundle or WASM-only module mode | Open |
| W05 | Structural | Bud cannot bind global/window events | Open |
| W06 | Operational | `--allow-undefined` hides native WASM dependencies | Open (`AUDIT` D18) |
| W07 | Operational | WASM dependency tracking misses includes/headers | Open |
| W08 | Structural | Bud state parsing is not a real JSON boundary | Open (`AUDIT` D14) |
| D01 | Friction | Page enhancement needs magic strings and manual state scripts | Open |
| D02 | Friction | Asset cache busting is manual | Open (`AUDIT` F12) |
| D03 | Operational | Module hot reload is not real | Open (`AUDIT` F2) |
| D04 | Friction | Build profiles and bootstrapping are incomplete | Open (`AUDIT` F16) |
| D05 | Friction | `bud_attr_fmt` has a hidden four-slot limit | Open (`AUDIT` D21) |

## 1. Module encapsulation

### M01 — cyclic and redundant module load graph

**Goal violated:** dependencies should be explicit, minimal, and acyclic.

`core` loads `common`, `source`, and modules from `mods.load`
(`mods/core/core.c:27,113-114`). Modules also load dependencies themselves:

- `auth → index`: `mods/auth/auth.c:414`
- `index → auth`: `mods/index/index.c:661`
- `song → index+mpfd`: `mods/song/song.c:441-442`
- `grp → index+mpfd+song`: `mods/grp/grp.c:639-641`
- `gig → index+mpfd+song+source+grp`: `mods/gig/gig.c:1243-1247`
- `poem → index`: `mods/poem/poem.c:143`

**Consequence:** initialization order is distributed; modules know their
transitive closure; `auth` and `index` form a direct cycle.

**Target:** one explicit DAG. Modules declare immediate capabilities only;
shared abstractions remove `auth ↔ index`; core owns top-level installation.

### M02 — missing module implementations can look successful

**Audit:** F1 (`docs/AUDIT.md:633`).

`mods/core/core.c:27-29` warns and continues after a module load failure.
`external/libxylem/src/libxylem-dispatch.c:315-321` zeroes the return and
reports `XY_OK` when no implementation ran.

**Consequence:** a missing `.so` can turn a required operation into an
apparent zero-valued success.

**Target:** `xy_call` returns `XY_ERR_NOTFOUND` when `ran == 0`; required load
failures prevent dependent modules from installing.

### M03 — textual inclusion exceeds the sanctioned exception

**Goal violated:** cross-`.so` behavior uses XY. Textual inclusion is reserved
for small, pure C-isomorphic renderer units where SSR/WASM node order must
match.

- `mods/index/index.c:45` includes `mods/common/list_fill.c`.
- `mods/grp/ux/all.c:13-16` includes index list code and song music code.
- `mods/gig/ux/detail.c:12-19` includes song music, common UI, index list
  rendering, and common list fill.
- Renderers include all of `mods/common/ux/site_ui.c`, e.g.
  `mods/song/ux/detail.c:12` and `mods/index/ux/list_fe.c:15`.

`mods/common/list_fill.c:1-7` calls itself native-only and textually compiled.
Its body is protected by `#ifndef __wasm__`; therefore the old claim that its
native calls currently link into WASM is stale. The module-boundary violation
remains.

**Consequence:** private implementation, flags, and types become shared by
accident; changes silently affect several modules.

**Target:** retain only minimal pure renderer inclusion. Move native list fill
behind an owned API and split common UI into small native/WASM-safe units.

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

### M05 — `source` assumes filesystem-backed datasets

`mods/source/source.c:199-245` creates referenced entities by making target
directories and writing files. `mods/source/source.c:377-499` enumerates all
sources during inverse-reference cleanup and persists target files.

**Consequence:** in-memory, remote, or differently persisted sources are not
independent; generic reference behavior assumes `items_path` child files.

**Target:** persistence is a source-owned hook. Generic ref logic updates hyle;
a filesystem adapter decides how to persist it.

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

### L01 — `hyle-bud` headers are exposed globally

`build.mk:23` adds `external/hyle/c/libhyle-bud/include` to every native
module. The top-level lint command repeats the global include. `gig`, `grp`,
and `index` link `-lhyle-bud` but rely on this global header path rather than
declaring a native `EXTRA_CFLAGS` dependency.

**Consequence:** undeclared consumers can include the bridge and compile; the
build cannot enforce library independence.

**Target:** remove the global bridge include; add it only to declared
hyle-bud consumers and scope lint identically.

### L02 — `source` exposes bud component types

`mods/source/source.c:18` includes `bud/bud.h`; `mods/source/Makefile:4`
declares the dependency. `mods/source/source.h:111-128,158-205` exposes
`struct bud_field_desc` in public XY APIs.

**Consequence:** the data module cannot serve another component framework
without carrying bud's schema type.

**Target:** source accepts a framework-neutral field descriptor. Bud
conversion belongs in common's bud adapter or `libhyle-bud`.

### L03 — WASM bundles compile hyle-bud sources directly

`mods/index/Makefile:10-15` and `mods/gig/Makefile:9-15` list hyle-bud's
`filter.c` and `table.c` as bundle sources.

**Consequence:** implementation is duplicated per bundle; ownership is
implicit; native and WASM builds can drift.

**Target:** a WASM-compatible hyle-bud artifact or one build-system-owned
source declaration.

## 3. WASM architecture and bundling

### W01 — global/local runtime split is only half implemented

The SSR tree has been split: `mods/common/ux/site_chrome.c:89-139` renders an
independent `#chrome-root`, while `mods/common/ux/site_ui.c:456-500` keeps the
page content/menu panel local. `site_ui_page` independently serializes chrome
and page HTML (`mods/common/ux/site_ui.c:744-845`). The corresponding runtime
split has not landed: there is no `site_chrome.wasm`, and the loader still
supports only `#bud-root`.

**Consequence:** the DOM advertises two independent concerns, but only the old
single local bridge exists. The global tree cannot hydrate and local bundles
are currently shadowed by the first `site_chrome` module token (W03).

**Target:** complete `NAV.md:349-408`: `site_chrome.wasm ↔
#chrome-root/#chrome-state` plus optional `<page>.wasm ↔
#bud-root/#bud-state`, each with its own bridge.

### W02 — loader supports one module/root/state/bridge

`htdocs/bud-client.js:8-13` reads the first module token and one `#bud-root`;
`:36-49` reads one `#bud-state`; `:51-56` stores one bridge.

**Consequence:** `data-modules="site_chrome list"` loads only the first bundle.

**Target:** explicit module specs, parallel isolated loads,
`Promise.allSettled`, `window.__bud_bridges`, and a compatibility alias.

### W03 — page shell advertises an unavailable global module

`mods/common/ux/site_ui.c:787-795` now correctly emits `site_chrome` on every
page and prepends it to an optional local module. It also always emits the
versioned loader (`:800-839`). However, `htdocs/bud-client.js:10-17` takes only
the first token, looks only for `#bud-root`, and fetches
`/site_chrome.wasm`; that asset does not exist.

**Consequence:** pages with a local bundle advertise
`data-modules="site_chrome <local>"`, but the loader never reaches `<local>`.
It attempts the missing global asset against the wrong root and returns on the
404. Current local WASM enhancement is therefore disabled by the half-migration.

**Target:** land the multi-root loader and `site_chrome.wasm` atomically with
the page-shell contract, with independent failure handling for every module.

### W04 — no global bundle or WASM-only build mode

There is no `mods/site_chrome`, `htdocs/site_chrome.wasm`, or common WASM
target. `build.mk:42-55` always includes a native target and has no
`WASM_ONLY=1` mode.

**Consequence:** global-only code needs a fake `.so`, copied rule, or placement
in an unrelated local bundle.

**Target:** reusable WASM-only target support and one automatically included
global bundle.

### W05 — bud cannot bind global/window events

`htdocs/bud-hydrate.js:1-13` parses `event`/`event:1`; `:125-146` builds
control payloads; `:221-244,284-332` binds nodes/roots only.

**Consequence:** document scroll cannot dispatch to C/WASM without bypassing
bud.

**Target:** allowlisted `event@window`, passive scroll binding, rAF throttle,
cleanup, and `scrollY` payload (`NAV.md:1131-1306`).

### W06 — `--allow-undefined` hides native dependencies

**Audit:** D18 (`docs/AUDIT.md:489`).

`build.mk:38` uses `--export-all --allow-undefined`.

**Consequence:** stray `qmap_`, `source_`, `axil_`, or XY calls can link and
fail only in the browser.

**Target:** import/export allowlists and CI symbol checks.

### W07 — dependency tracking misses includes and headers

`build.mk:44` tracks declared direct sources, not compiler header dependencies.
For example, `mods/index/ux/list_fe.c:14-16` includes `viewer_zoom.h`,
`site_ui.c`, and `list.c`.

**Consequence:** native SSR may rebuild with new node IDs while WASM remains
stale. The old claim that the rule has no prerequisites is stale, but included
files remain untracked.

**Target:** compiler dependency files or complete explicit prerequisites.

### W08 — bud state parsing is not a real JSON boundary

**Audit:** D14 (`docs/AUDIT.md:465`).

`external/bud/src/libbud.c:2371-2435` locates keys with `strstr`; array parsing
uses a 4096-byte temporary at `:2578-2587`.

**Consequence:** key-like text inside values can be misread and large elements
truncate. Every custom WASM page inherits this.

**Target:** a real length-aware parser or documented length-prefixed format.

## 4. Developer ergonomics

### D01 — magic bundle strings and manual state scripts

- List manually serializes state and passes `"list"`:
  `mods/index/index.c:184-216`.
- Song constructs a script and passes `"song_detail"`:
  `mods/song/song.c:362-375`.
- Gig assembles state and passes `"gig_detail"`:
  `mods/gig/gig.c:459-475,979-984`.

**Consequence:** typos become silent 404s at `htdocs/bud-client.js:15-18`;
modules repeat escaping, script, and root plumbing.

**Target:** one page descriptor/response helper owns chrome state, local state,
roots, and bundle identity. Callers supply data/renderer, not script HTML.

### D02 — cache busting is manual

**Audit:** F12 (`docs/AUDIT.md:727`).

`mods/common/ux/site_ui.c:761-762` manually defines `SITE_CSS_V` (`?v=21`)
and `SITE_CLIENT_V` (`?v=1`).

**Consequence:** stale CSS/bridge code can run against new SSR/WASM.

**Target:** build-derived content hashes or one generated asset version.

### D03 — hot reload is not real

**Audit:** F2 (`docs/AUDIT.md:647`).

`external/libxylem/src/libxylem.c:804-817` uses `RTLD_NODELETE`; glibc may
reuse old mappings after rebuild.

**Consequence:** reload can report success while requests run old code.

**Target:** safe removal of `RTLD_NODELETE` or unique-path/inode loading.

### D04 — build profiles and bootstrapping are incomplete

**Audit:** F16 (`docs/AUDIT.md:751`).

The top-level `Makefile:8-29` assumes axil, libxylem, and qmap are prebuilt.
`build.mk:22,37` fixes native/WASM at `-O0 -g`. Deployment copies every
`htdocs/*.wasm`, including demos.

**Target:** complete prerequisites, dev/release profiles, and an allowlisted
production asset manifest.

### D05 — `bud_attr_fmt` has a hidden four-slot limit

**Audit:** D21 (`docs/AUDIT.md:500`).

`external/bud/src/libbud.c:970-981` rotates through four 256-byte buffers.
A fifth formatted attribute aliases an earlier value.

**Target:** own each formatted value or expose caller-owned storage/lifetime.

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

1. `NAV.md` is still a plan (`NAV.md:1-11`), but its SSR chrome split is now
   partially implemented. The bundle/loader/window-event phases are not.
2. `list_fill.c` native calls are excluded from WASM by `#ifndef __wasm__`;
   its textual coupling remains a violation.
3. `build.mk:44` now tracks direct WASM sources (`AUDIT` D9 fixed); headers
   and textual includes remain untracked (W07).
4. The old CSS version literal in the audit is stale; manual versioning is not.
5. `docs/GOALS.md` describes intended local hyle-bud include declarations;
   native consumers currently rely on the global path (L01).

## 7. Confirmed boundaries that hold

- `external/hyle/src` and `external/hyle/include` contain no bud symbols.
- `external/bud/src` does not include hyle/qmap/axil/xylem/stoma.
- HTML row deletion routes through hyle (`AUDIT` E1 fixed).
- Inverse-ref cleanup updates hyle before persistence (`AUDIT` E2 fixed),
  although M05 remains.
- Direct WASM sources are prerequisites (`AUDIT` D9 fixed), although W07
  remains for includes.
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
