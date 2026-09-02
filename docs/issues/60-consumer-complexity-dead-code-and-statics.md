---
id: 60
title: "Consumer-side complexity: dead walk-* protocol, global filter registries, duplicate textarea, domain-leak skip-list, statics"
state: ready
severity: medium
requires: []
validates: "walk-* dead code removed; g_ms/g_ss replaced with per-widget lookup; site_ui_textarea_value dedup deleted; form.c domain-leak skip-list removed; site_chrome.c statics moved off global"
area: "htdocs/bud-hydrate.js:729-778,1503, external/bud/src/libbud.c:2545-2647, mods/common/ux/site_forms.c:85-128, external/hyle/c/libhyle-bud/src/filter.c:128-149, external/hyle/c/libhyle-bud/src/form.c:108-115, mods/common/ux/site_chrome.c:17-20"
parent: []
---
# Issue: Consumer-side complexity — 6 items from quest #1788359911

- **Area:** Consumer code across `htdocs/`, `external/bud/`, `external/hyle/c/libhyle-bud/`, `mods/common/ux/`
- **Runs observed:** `1788359911` (`ANALYSIS.md:1`, plan v4 high confidence)
- **Severity:** Medium — reduces cognitive complexity without file-splitting; items 1+4 pure deletion (lowest risk), items 2+5+6 need design

## Items

### Item 1 — Dead `walk-*` replay in `bud-hydrate.js` + `libbud.c` (Phase A, lowest risk)

TWO distinct dead APIs (verified 2026-09-02 build-mode):
- **`bud_render_walk_ops`/`bud_render_walk_ops_node`** — JS bridge protocol (`libbud.c:2545-2637,2639-2647`, decl `bud.h:131`) emitting `walk-*` ops.
- **`bud_walk`/`bud_walk_node`** — introspection API (`libbud.c:2659-2716,2718-2725`, `bud_walk_ops`/typedefs `bud.h:54-71,133`). 0 prod callers.

| Artifact | Location | Status |
|----------|----------|--------|
| `case 'walk-enter'` | `htdocs/bud-hydrate.js:729-738` | DEAD — only push onto walkStack |
| `case 'walk-attr'` | `bud-hydrate.js:740-745` | DEAD |
| `case 'walk-listener'` | `bud-hydrate.js:747-767` | DEAD |
| `case 'walk-text'` | `bud-hydrate.js:769-774` | DEAD |
| `case 'walk-leave'` | `bud-hydrate.js:776-778` | DEAD |
| `walkStack` | `bud-hydrate.js:660` | DEAD — only read/written by walk-* cases |
| `hydrateBudFromWalk` | `bud-hydrate.js:1503-1505` | DEAD — 0 importers |
| `bud_render_walk_ops_node` (static) | `libbud.c:2545-2637` | DEAD prod |
| `bud_render_walk_ops` (public) | `libbud.c:2639-2647` | DEAD prod |
| `bud_walk_node` (static) | `libbud.c:2659-2716` | DEAD prod |
| `bud_walk` (public) | `libbud.c:2718-2725` | DEAD prod |
| Declarations | `bud.h:131,133`, `bud_walk_ops` `bud.h:54-71` | DEAD prod |
| Docs | `external/bud/README.md` (tree-walk + walk-op lines) | remove |
| Test `bud_walk` block | `bud_test.c:858-899` | test only — remove |
| Test `walk-stream` block | `bud_test.c:901-926` | test only — remove |

**DO NOT DELETE:** `walkNodes()` at `bud-hydrate.js:58` — 3 active callers (85/468/642). And DO NOT delete shared fixtures `test_emit`/`walk_stream`/`ops` (used by hydration/patch/vdom tests at 971/991/1254/1304). Only the two walk test blocks + (if `-Werror` flags unconed) helpers `walk_enter/attr/listener/leave`/`walk_push`/`walk_ops` typedef (bud_test.c:21,135-187).

**Proof:** WASM bridge uses `bud_render_patch_ops`/`bud_render_ops`/`bud_vdom_diff`. `"walk-"` 0× in `bud_wasm_app.c`/`mods/`.

### Item 2 — Global registries `g_ms/g_ss` in `filter.c` (Phase B, medium risk)

| Symbol | Location | Type |
|--------|----------|------|
| `g_ms` | `filter.c:128` | `static hyle_bud_ms_t g_ms[HYLE_BUD_MS_MAX]` max 8 |
| `g_ms_count` | `filter.c:129` | `static int g_ms_count` |
| `g_ss` | `filter.c:148` | `static hyle_bud_ss_t g_ss[HYLE_BUD_SS_MAX]` max 8 |
| `g_ss_count` | `filter.c:149` | `static int g_ss_count` |

**Footgun:** Native `.so` lifetime — forgetting `hyle_bud_ms_reset:151-155` before next render accumulates counter → overflow → silent `<fieldset>` fallback (`filter.c:289-291`).

**SSR impact:** SSR calls `idx_filter_bar→hyle_bud_ms_reset→hyle_bud_filter_field` on every list render. Statics populated but never queried (no events on server). HTML is plain `details/checkbox/search` + `data-hyle-ms` (`filter.c:358`). **No SSR contract break.**

**Replacement:** Keyed hash map via `data-hyle-ms="<key>"` on `details` wrapper. Resolve `event→target→closest [data-hyle-ms]→key→map lookup`. Eliminates statics, slot limit, scan.

### Item 3 — Sanitizer move to `external/libhyle-source` — **REJECTED**

Reviewer blocked per `AGENTS.md:4` hyle neutrality. `site_ui_sanitize_html:888` stays in `site_forms.c:617-901`.

### Item 4 — Dead `site_ui_textarea_value` in `site_forms.c` (Phase A, lowest risk)

`site_forms.c:85-128` — char-for-char duplicate of `form.c:39-82` (`hyle_bud_textarea_value`). `callers_total:0`, grep 0 callers — dead static. Safe to delete.

### Item 5 — Domain-leak skip-list in `form.c` (Phase D, needs user pick)

`form.c:108-115`:
```c
if (strcmp(d->key,"id")==0 || strcmp(d->key,"owner")==0 ||
    strcmp(d->key,"song_source")==0) continue;
```
Generic `writable` + `kind>=3||kind==5` already cover most. Only `id` is truly non-EXCLUDE — `owner`/`song_source` already `kind==HYLE_KIND_EXCLUDE`.

**Options (user picks before Phase D):**
- (a) `HYLE_KIND_ID` enum
- (b) `HYLE_FIELD_FLAG_FORM_SKIP` bit in `hyle_schema_desc_t`
- (c) Keep explicit `key=="id"` check in `form.c`

### Item 6 — Statics in `site_chrome.c` (Phase C, low risk)

| Line | Decl | Role |
|------|------|------|
| 17 | `static bud_node *site_chrome_nav_bar` | Node ptr mutated by `set_hidden:52-62`, read by `on_scroll:64-87` |
| 18 | `static int site_chrome_scroll_y` | Scroll accumulator |
| 19 | `static int site_chrome_scroll_ready` | First-event latch |
| 20 | `static int site_chrome_hidden` | Visibility state |

`site_chrome_on_scroll:70` reads `event->user` — but **`event->user` carries the scroll-Y STRING** (`bud-hydrate.js:158-162`), NOT an arbitrary pointer. A heap struct behind `event->user` is NOT viable for the scroll handler.

**Correct approach:** resolve the `nav_bar` node from the DOM at event time (mirrors `bud_api_action_handler:4039` parent-walk). Give `<header class='nav-bar'>` a `data-chrome-nav='1'` attr, and in `site_chrome_on_scroll:64` walk `event->target` ancestors for `data-chrome-nav`, then store the 3 ints as `data-chrome-scroll-y`/`data-chrome-scroll-ready`/`data-chrome-hidden` attrs on that node. Drop all 4 statics (`site_chrome.c:17-20`). Additive `data-*` attrs keep the SSR contract.

## Phases

| Phase | Items | Risk | Gate |
|-------|-------|------|------|
| **A** | 1 + 4 | lowest — pure deletion | `make`, `make test`, `check-wasm-imports.sh`, `check-module-boundaries.sh`, `check-no-site-specific-js.sh` |
| **B** | 2 | medium | same |
| **C** | 6 | low | same |
| **D** | 5 | deferred — user picks a/b/c | same |

## Constraints

- `AGENTS.md:1` — UX pure & isomorphic, forbidden `XY_/xy_/qmap_/source_/axil_`
- `AGENTS.md:4` — hyle neutral; `external/hyle` no DOM, `external/bud` no storage
- `AGENTS.md:5` — data invariants; all writes via `source_update_item`
- `docs/SSR-CONTRACT.md` — plain HTML + `data-*` hooks
- `docs/C-ISOMORPHIC-BUD.md` — one `bud_app_render(state)`

## Files examined

`htdocs/bud-hydrate.js`, `htdocs/bud-client.js`, `external/bud/src/libbud.c`, `external/bud/src/bud_test.c`, `external/bud/src/bud_wasm_app.c`, `external/hyle/c/libhyle-bud/src/filter.c`, `external/hyle/c/libhyle-bud/src/form.c`, `external/hyle/c/libhyle-bud/src/picker.c`, `external/hyle/c/libhyle-bud/src/table.c`, `mods/common/ux/site_ui.c`, `mods/common/ux/site_chrome.c`, `mods/common/ux/site_forms.c`, `mods/site_chrome/ux/chrome.c`, `mods/index/ux/list*.c`, `docs/OVERVIEW.md`

Related: Quest #1788359911 (`consumer-complexity-analysis`), plan v4.
