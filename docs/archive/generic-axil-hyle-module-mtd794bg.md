# Quest: generic-axil-hyle-module

## Goal
Create generic axil-hyle library providing REST CRUD and picker fragment endpoints for any axil-hyle website

## Original request
> Goal: Create generic axil-hyle library providing REST CRUD and picker fragment endpoints for any axil-hyle website

## Parent Quest
[[hyle-site-abstractions-simplification]]

## Current Status
- [x] done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `make standalone-unit-tests && make pages-test && AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Validated against `dataset-pagination.test.ts`, `song-validation.test.ts`, and full e2e suite.
- [x] **3. Feature Implementation**: Created standalone library `external/axil-hyle` providing `axil_hyle_install_routes()`.
- [x] **4. Build & Run**: Built with zero compiler errors; server runs cleanly.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed full test suite (105 passed, 0 failed).

## In-Depth Analysis & Findings
- Extracted all generic REST endpoints (`GET/POST/PUT/DELETE /api/dataset/...`) and picker fragment endpoints (`GET /pick/:id/options`) into `external/axil-hyle`.
- Removed 1,208 LOC from `mods/source/source-http.c` (file eliminated).
- Removed fragment handler and registration from `mods/index/pick.c` and `mods/index/index.c`.
- `mods/source/source.c` now delegates route mounting via one single line `axil_hyle_install_routes()`.

## Files touched
- `external/axil-hyle/Makefile`
- `external/axil-hyle/objects-set.mk`
- `external/axil-hyle/include/ttypt/axil-hyle.h`
- `external/axil-hyle/src/libaxil-hyle.c`
- `external/hyle/c/libhyle-source/include/hyle-source/hyle_source.h`
- `external/hyle/c/libhyle-source/src/json.c`
- `mods/source/Makefile`
- `mods/source/source.c`
- `mods/source/source-http.c` (removed)
- `mods/index/index.c`
- `mods/index/pick.c`
- `start.sh`
- `Makefile`

## Acceptance Criteria & Polish Checklist
- [x] `external/axil-hyle` builds cleanly as `lib/libaxil-hyle.so`.
- [x] Zero compilation errors across all modules.
- [x] `make standalone-unit-tests` passes.
- [x] `make pages-test` passes.
- [x] Full E2E suite (`deno test tests/e2e/`) passes 105/105 tests.

## Next recommended step
Archive sub-quest to return to parent quest [[hyle-site-abstractions-simplification]] for Sub-Quest 4 (Clean up `mods/common` Redundancies and complete final wrap-up).
