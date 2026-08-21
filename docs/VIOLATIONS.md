# Violations — known departures from the architecture goals

This is the canonical catalog of **current violations of `docs/GOALS.md`**.
It records what is wrong today so agents do not copy a bad precedent, mistake
an unimplemented plan for reality, or repeatedly rediscover the same problem.

This does not replace `docs/AUDIT.md`. The audit also covers security,
correctness, and performance. This file is limited to module encapsulation,
external-library independence, WASM architecture/bundling, and developer
ergonomics. Related audit IDs are noted below.

Reviewed: **2026-08-22** — **0 open violations**. Last fixed batch
2026-08-22 `M01-M07` `L01-L03` `W01-W08` `D01-D05` (24/24 Fixed). Full detail
archived in `docs/VIOLATIONS-ARCHIVE-2026-08-22.md` and `git log -- docs/VIOLATIONS.md`.

## Summary

No open violations. Guidelines in `docs/GOALS.md:1.2,2.2,3,5` + `docs/ARCHITECTURE.md:§3,§5,§6` + `docs/DESIGN.md` + `docs/CONVENTIONS.md` are the checklist. Copying any previously-fixed pattern (e.g. `#include "*.c"` across modules, global `hyle-bud` include, bare `--allow-undefined`) will fail `make all:boundary-check`.

## Deliberate exceptions — do not “fix” these

- **`libhyle-bud` is the sanctioned bridge.** Its bud dependency is correct;
  global exposure (L01), not the bridge itself, is the violation.
- **Small pure C-isomorphic source inclusion is sanctioned** for identical
  native/WASM node order (`mods/song/ux/detail.c:10-14`,
  `mods/index/ux/list_fe.c:1-16`, `mods/index/ux/list.c`). Allowlisted in
  `scripts/check-module-boundaries.sh:26` as `mods/common/ux/site_ui.c|mods/index/ux/list.c|mods/song/ux/music.c` only. Do not extend it to native data collection.
- **Missing WASI may degrade to SSR-only** (`build.mk:44-49`). CI/release
  should separately verify required assets.
- **Search is accent-sensitive by design.** `pão` and `pao` differ.
- **Node IDs may overlap across independent roots.** Each bridge owns its map.
- **No-JS is mandatory.** Do not solve a WASM issue with JS-only controls.

## Confirmed boundaries that hold

- `external/hyle/src` and `external/hyle/include` contain no bud symbols.
- `external/bud/src` does not include hyle/qmap/axil/xylem/stoma.
- HTML row deletion routes through hyle (`AUDIT` E1 fixed).
- Inverse-ref cleanup updates hyle before persistence (`AUDIT` E2 fixed) via `source_store` put_field.
- Direct WASM sources are prerequisites (`AUDIT` D9 fixed) and `-MM` header deps close W07.
- No-JS/additive enhancement remains intact.

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
- `docs/VIOLATIONS-ARCHIVE-2026-08-22.md` — full fixed detail (24 IDs)
- `docs/C-ISOMORPHIC-BUD.md` — sanctioned dual-compile pattern
- `docs/WASM-BRIDGE.md` — bridge/patch mechanics
