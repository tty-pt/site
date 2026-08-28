# Quest: eliminate-index-pick-shims

## Goal
Migrate remaining pick view collectors to libhyle-bud schema collectors and delete mods/index/pick.c

## Original request
> Goal: Migrate remaining pick view collectors to libhyle-bud schema collectors and delete mods/index/pick.c

## Parent Quest
[[site-simplification-deep-cleanup]]

## Current Status
- [x] in progress · not started · done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `sh tests/unit/run-bud-picker-collect.sh && make standalone-unit-tests && make pages-test && bash tests/pages/50-pickers.sh && AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Extended unit test `tests/unit/bud_picker_collect_test.c` with tests for `hyle_bud_picker_view_collect_auto_fields_schema`.
- [x] **3. Feature Implementation**:
  - Implemented `hyle_bud_picker_view_collect_auto_fields_schema` in `external/hyle/c/libhyle-bud/src/picker.c` and declared in `<hyle-bud/hyle-bud.h>`.
  - Migrated `mods/song/song.c` to `hyle_bud_picker_view_collect_schema`.
  - Migrated `mods/index/index.c` to `hyle_bud_picker_view_collect_schema`.
  - Migrated `mods/gig/gig.c` (`sb_load_song_picks`, `sb_load_edit_song_picks`, `gig_detail_auth`, `gig_edit_auth`, `gig_add_get_handler`) to `libhyle-bud` schema collectors.
  - Deleted `mods/index/pick.c` (-660 lines), removed `#include "pick.c"` from `mods/index/index.c`, and removed all `XY_DECL(int, pick_*)` from `mods/index/index.h`.
- [x] **4. Build & Run**: Build clean with zero warnings or errors across native and WASM targets.
- [x] **5. Clean Code**: Verified code has zero debug artifacts, zero dead XY declarations, zero legacy `form_field_t` leftovers.
- [x] **6. Server Restart & Full Test Suite**: Restarted server instance and passed full test suite (105/105 E2E tests, unit tests, pages tests).

## In-Depth Analysis & Findings
- `mods/index/pick.c` (660 lines) was originally created as an XY provider inside `mods/index` to do query-string parsing and option resolution for omnisearch pickers.
- All core primitives (`hyle_source_resolve_options`, `hyle_source_resolve_tokens`, `hyle_bud_picker_view_collect_schema`, `hyle_bud_picker_view_collect_scoped`, `hyle_bud_form`, `hyle_bud_picker_view_collect_auto_fields_schema`) now live inside `libhyle-source` and `libhyle-bud`.
- Eliminating `mods/index/pick.c` removed 660 lines of legacy XY glue, simplified `mods/index/index.h` by removing 10 XY function declarations, and removed cross-module dependencies on `mods/index` for picker collection.

## Detailed Multi-Stage Execution Plan
### Stage 1: Extend `libhyle-bud` with `hyle_bud_picker_view_collect_auto_fields_schema` & Unit Tests
- Implemented `hyle_bud_picker_view_collect_auto_fields_schema` in `external/hyle/c/libhyle-bud/src/picker.c`.
- Declared in `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`.
- Added test assertions in `tests/unit/bud_picker_collect_test.c` and verified with `tests/unit/run-bud-picker-collect.sh`.

### Stage 2: Migrate Callers in `song`, `index`, and `gig`
- `mods/song/song.c`: replaced `pick_view_collect_desc_*` with `hyle_bud_picker_view_collect_schema`.
- `mods/index/index.c`: replaced `pick_view_collect_desc_fd` in generic add/edit handlers with `hyle_bud_picker_view_collect_schema`.
- `mods/gig/gig.c`: replaced `sb_pick_song_ff`, `sb_pick_fmt_ff`, `edit_song_ff`, `grp_field_def`, `row_candidate_ff`, `add_ff` with schema descriptors and `hyle_bud_picker_view_collect_*`.

### Stage 3: Delete `mods/index/pick.c` & Clean up `mods/index/index.h`
- Removed `#include "pick.c"` from `mods/index/index.c`.
- Deleted `mods/index/pick.c`.
- Removed dead `XY_DECL(int, pick_*)` from `mods/index/index.h`.
- Verified build and tests pass.

## Acceptance Criteria & Polish Checklist
- [x] `hyle_bud_picker_view_collect_auto_fields_schema` tested and working in `libhyle-bud`.
- [x] `mods/index/pick.c` deleted (0 leftover references in `mods/`).
- [x] Zero build warnings or errors across native and WASM builds.
- [x] All unit tests, smoke pages tests, and E2E tests passing.

## Why this matters
Eliminating `mods/index/pick.c` removes an unnecessary cross-module XY dependency between `song`/`gig` and `index`, making picker state resolution completely framework-owned in `libhyle-bud`.

## Decisions made
- Implemented `hyle_bud_picker_view_collect_auto_fields_schema` in `libhyle-bud` to handle multi-candidate cell/row pickers declaratively using `hyle_schema_desc_t`.

## Files touched
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `tests/unit/bud_picker_collect_test.c`
- `mods/song/song.c`
- `mods/index/index.c`
- `mods/index/index.h`
- `mods/gig/gig.c`
- `mods/common/ux/site_ui.h`
- `mods/index/pick.c` (deleted)

## Remaining work
- None for Sub-Quest 2! Ready to archive sub-quest and proceed to Sub-Quest 3 of [[site-simplification-deep-cleanup]].

## Next recommended step
Archive sub-quest `eliminate-index-pick-shims` and return to parent quest `site-simplification-deep-cleanup`.
