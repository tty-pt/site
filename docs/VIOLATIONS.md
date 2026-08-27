# Violations — known departures from the architecture goals

This is the canonical catalog of **current violations of `docs/GOALS.md`**.
It records what is wrong today so agents do not copy a bad precedent, mistake
an unimplemented plan for reality, or repeatedly rediscover the same problem.

This does not replace `docs/AUDIT.md`. The audit also covers security,
correctness, and performance. This file is limited to module encapsulation,
external-library independence, WASM architecture/bundling, and developer
ergonomics. Related audit IDs are noted below.

Reviewed: **2026-08-27** — **0 open violations**. All encapsulation boundaries verified: `external/bud` (pure UI, 0 DB), `external/hyle` (pure schema/query, 0 DOM), `libhyle-source` (persistence engine, pluggable drivers), `libhyle-bud` (pure component bridge). Full detail
archived in `git log -- docs/VIOLATIONS.md`.

## Summary

No open violations. Guidelines in `docs/GOALS.md` + `docs/ARCHITECTURE.md:§3,§5,§6` +
`docs/DESIGN.md` + `docs/CONVENTIONS.md` are the checklist. Copying any previously-fixed
pattern (e.g. `#include "*.c"` across modules, global `hyle-bud` include, bare
`--allow-undefined`) will fail `make all:boundary-check`.

Deferred low-priority drifts (not counted as open, to address later):
`wabt`
`wasm-objdump` missing on host weakens `W06` to `strings` fallback, duplicate
`VERSION_GEN` rule in `build.mk`/`mods/common/Makefile`, `cflags` declared after
`include` in `mods/gig`/`mods/index` Makefiles. See `docs/AUDIT.md` and
`git log`.

## Deliberate exceptions — do not “fix” these

- **`libhyle-bud` is the sanctioned bridge.** Its bud dependency is correct;
  global exposure (L01), not the bridge itself, is the violation.
- **`hyle-bud` in UX is sanctioned.** `mods/index/ux/list_fe.c`, `mods/gig/ux/detail.c`,
  `mods/grp/ux/detail.c` including `<hyle-bud/hyle-bud.h>` and linking
  `HYLE_BUD_WASM_SRC` (`filter.c`/`table.c`) for filter/table widgets is the
  intended use. Forbidden in UX is `hyle_source_`/`hyle_query_`/`qmap_`/`source_`/
  `axil_`/`XY_`, not `hyle_bud_*` (`CONVENTIONS` WASM purity, `GOALS.md:§2.2`).
- **Small pure C-isomorphic source inclusion is sanctioned** for identical
  native/WASM node order (`mods/song/ux/detail.c:10-14`,
  `mods/index/ux/list_fe.c:1-16`, `mods/index/ux/list.c`). Allowlisted in
  `scripts/check-module-boundaries.sh:26` as `mods/common/ux/site_ui.c|mods/index/ux/list.c|mods/song/ux/music.c` only. Do not extend it to native data collection.
- **Preprocessor aggregator is sanctioned.** `mods/common/ux/site_ui.c:#ifndef __wasm__`
  including `site_chrome.c`/`site_page.c` and
  `mods/common/ux/site_page.c:#if __has_include("version.gen.h")` fallback are
  the only allowed `#if` in UX; all other branching is runtime `if (state.foo)`.
  `site_page.c` itself is native-only (`axil_respond`, `common.h` with `XY_DECL`)
  behind that guard — not a purity violation.
- **WASM host imports are sanctioned.** `__attribute__((import_module("env")…))`
  declarations (e.g. `mods/gig/ux/detail.c` `bud_host_log`) are the only allowed
  `#ifdef __wasm__` wrapper alongside include guards.
- **Site-specific `site_paths.c` icon table is grandfathered.** Hardcoded
  `strcmp(module,"song"|"poem"|"gig"|"grp")` in `mods/common/ux/site_paths.c:68`
  is the only allowed module-name enumeration; new modules must use
  `source_list_view_t` registration. Enforced by `scripts/check-module-boundaries.sh`.
- **Missing WASI may degrade to SSR-only** (`build.mk`). CI/release
  should separately verify required assets.
- **UX purity check enforced.** `scripts/check-ux-purity.sh` verifies: no XY hooks in UX (`XY_DECL`, `XY_IMPL`, `xy_load`, `xy_install`, `XY_MODULE_API`, `XY_CALL`), and no preprocessor conditionals except include guards, `site_ui.c:#ifndef __wasm__`, `gig/ux/detail.c:#ifdef __wasm__` (host imports), `site_page.c:#if __has_include("version.gen.h")`. Runs in `make boundary-check`.
- **Search is accent-sensitive by design.** `pão` and `pao` differ.
- **Node IDs may overlap across independent roots.** Each bridge owns its map.
- **No-JS is mandatory.** Do not solve a WASM issue with JS-only controls.
- **JS-off fallback loses newly typed draft text in main form.** The HTML form API only serializes elements physically inside the GET form (or via form="..."). We cannot automatically copy main form state without JS. This is an ACCEPTABLE PROGRESSIVE ENHANCEMENT DEGRADATION.
- **`bud_demo` was retired.** The orphaned demo (dead `/bud_demo.js` loader,
  no `#bud-root`/`bud-state`) was removed along with its e2e test after the
  unified `bud-client.js` hydration landed.
- **`site_core` is dead code.** `mods/site_core/` exports `site_core_register_module`,
  `site_core_module_icon`, `site_core_module_display`, `site_core_build_csp_extra`
  via XY but is not in `mods.load`, never loaded, and its functions are never called.
  CSP headers are set in `mods/common/common_response.c` and `mods/common/ux/site_ui.c`
  instead. Keep or remove at will; do not add new callers.

## Confirmed boundaries that hold

- `external/hyle/src` and `external/hyle/include` contain no bud symbols.
- `external/bud/src` does not include hyle/qmap/axil/xylem/stoma.
- HTML row deletion routes through hyle (`AUDIT` E1 fixed).
- Inverse-ref cleanup updates hyle before persistence (`AUDIT` E2 fixed) via `source_store` put_field.
- Direct WASM sources are prerequisites (`AUDIT` D9 fixed) and `-MM` header deps close W07.
- No-JS/additive enhancement remains intact.
- `grep -E 'qmap_|source_|axil_|hyle_source|XY_' mods/*/ux/*.c` is empty
  (only `hyle_bud_*` remains, sanctioned); `grep -E '"(poem|song|gig|grp)"' mods/common mods/index`
  hits only `site_paths.c`; `rg '"[^"]*var/'` outside storage is 0.

## Updating this catalog

When fixing a violation:

1. Fix code and tests together.
2. Remove or mark the violation fixed here in the same change (when `Open=0`, keep this header and archive).
3. Change `docs/GOALS.md` only if the desired invariant changes.
4. Update `docs/AUDIT.md` when an audit ID is involved.
5. If a finding is intentional, document it in Deliberate exceptions with evidence.

## Related docs

- `docs/GOALS.md` — desired architecture and review checklists
- `docs/ARCHITECTURE.md` — current request path, XY contract, load order
- `docs/DESIGN.md` — encapsulation and “evoke, don't reimplement”
- `docs/AUDIT.md` — security/correctness/performance issue catalog
- `docs/C-ISOMORPHIC-BUD.md` — sanctioned dual-compile pattern
- `docs/WASM-BRIDGE.md` — bridge/patch mechanics
