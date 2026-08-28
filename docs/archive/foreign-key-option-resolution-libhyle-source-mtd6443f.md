# Quest: foreign-key-option-resolution-libhyle-source

## Goal
Move core foreign-key, display field, and option resolution from site mods into libhyle-source

## Original request
> Goal: Move core foreign-key, display field, and option resolution from site mods into libhyle-source

## Parent Quest
[[hyle-site-abstractions-simplification]]

## Current Status
- [x] done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `sh tests/unit/run-source-options.sh && make standalone-unit-tests && make pages-test && bash tests/pages/50-pickers.sh`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Developed `tests/unit/source_options_test.c` and `tests/unit/run-source-options.sh` BEFORE modifying feature code.
- [x] **3. Feature Implementation**: Implemented `hyle_source_get_display_field`, `hyle_source_get_item_label`, `hyle_source_resolve_options`, `hyle_source_resolve_tokens`, `hyle_source_normalize_tokens_to_slugs`, `hyle_source_get_enum_options` in `external/hyle/c/libhyle-source/src/options.c`.
- [x] **4. Build & Run**: Built and ran project with zero build errors. Verified clean restart.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed standalone tests, pages smoke tests, and picker e2e test suite with zero errors.

## In-Depth Analysis & Findings
- `mods/index/pick.c` and `mods/index/list_fill.c` previously contained duplicate schema introspection, label resolution, token splitting, and slug normalizations that belonged in `external/hyle/c/libhyle-source`.
- Created unified, reusable C APIs in `libhyle-source`:
  - `hyle_source_get_display_field`
  - `hyle_source_get_item_label`
  - `hyle_source_resolve_options`
  - `hyle_source_resolve_tokens`
  - `hyle_source_normalize_tokens_to_slugs`
  - `hyle_source_get_enum_options`
- Replaced manual implementations in `pick.c` and `list_fill.c` with direct one-line delegations to `libhyle-source`.

## Files touched
- `external/hyle/c/libhyle-source/include/hyle-source/hyle_source.h`
- `external/hyle/c/libhyle-source/src/options.c`
- `external/hyle/c/libhyle-source/Makefile`
- `external/hyle/c/libhyle-source/objects-set.mk`
- `mods/index/Makefile`
- `mods/index/list_fill.c`
- `mods/index/pick.c`
- `start.sh`
- `tests/unit/source_options_test.c`
- `tests/unit/run-source-options.sh`
- `Makefile`

## Decisions made
- Foreign-key resolution functions live directly in `libhyle-source` so any consumer (CLI, server, UI bridges) can resolve display labels, search options, and comma tokens without duplicating logic.

## Acceptance Criteria & Polish Checklist
- [x] Unit test `tests/unit/source_options_test.c` passes completely.
- [x] Standalone unit tests (`make standalone-unit-tests`) pass.
- [x] Pages tests (`make pages-test`) pass.
- [x] Picker integration tests (`tests/pages/50-pickers.sh`) pass.
- [x] Full picker e2e suite passes.

## Next recommended step
Sub-quest complete; archive sub-quest to return to parent quest [[hyle-site-abstractions-simplification]] for Sub-Quest 2 (Schema-Driven Picker View Collection in `libhyle-bud`) or Sub-Quest 3 (Generic `axil-hyle` HTTP Module).
