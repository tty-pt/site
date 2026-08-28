# Quest: picker-view-collection-libhyle-bud

## Goal
Move schema-driven picker view collection and query parsing into libhyle-bud

## Original request
> Goal: Move schema-driven picker view collection and query parsing into libhyle-bud

## Parent Quest
[[hyle-site-abstractions-simplification]]

## Current Status
- [x] done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `sh tests/unit/run-bud-picker-collect.sh && make standalone-unit-tests && make pages-test && bash tests/pages/50-pickers.sh`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Developed unit test `tests/unit/bud_picker_collect_test.c` and runner `tests/unit/run-bud-picker-collect.sh` BEFORE modifying feature code.
- [x] **3. Feature Implementation**: Implemented `hyle_bud_picker_view_collect_schema` and `hyle_bud_picker_view_collect_scoped` in `external/hyle/c/libhyle-bud/src/picker.c`.
- [x] **4. Build & Run**: Built and ran project with zero build errors. Verified clean restart.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed standalone tests, pages smoke tests, and picker e2e test suite with zero errors.

## In-Depth Analysis & Findings
- Added `hyle_bud_picker_view_collect_schema` and `hyle_bud_picker_view_collect_scoped` directly in `libhyle-bud`.
- Connected schema descriptor inspection (`source_type == HYLE_BUD_REFERENCE` or `HYLE_BUD_MULTI_REFERENCE`), query param extraction (`pick_q_*`, `pick_page_*`, `?key=draft`), option search, and token resolution in a single generic API call.
- Delegated `pick_view_collect_desc_values` in `mods/index/pick.c` to `hyle_bud_picker_view_collect_schema`.

## Files touched
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `external/hyle/c/libhyle-bud/Makefile`
- `external/hyle/c/libhyle-bud/objects-set.mk`
- `mods/index/pick.c`
- `tests/unit/bud_picker_collect_test.c`
- `tests/unit/run-bud-picker-collect.sh`
- `Makefile`

## Acceptance Criteria & Polish Checklist
- [x] Unit test `tests/unit/bud_picker_collect_test.c` passes.
- [x] `make standalone-unit-tests` passes.
- [x] `make pages-test` passes.
- [x] `tests/pages/50-pickers.sh` passes.
- [x] All 5 picker e2e tests pass.

## Next recommended step
Archive sub-quest to return to parent quest [[hyle-site-abstractions-simplification]] for Sub-Quest 3 (Generic `axil-hyle` HTTP Module) and Sub-Quest 4 (Clean up `mods/common` Redundancies).
