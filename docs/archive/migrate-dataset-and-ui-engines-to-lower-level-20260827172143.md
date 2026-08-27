# Task: migrate-dataset-and-ui-engines-to-lower-level

## Goal
Migrate generic dataset REST HTTP API and UI state collection pipelines from site-level modules (`mods/source`, `mods/index`) down into lower-level framework libraries:
1. Omnisearch picker parameter parsing & scope resolution (`hyle_bud_query_param`, `hyle_bud_pick_find_active_scope`) moved to `libhyle-bud`.
2. Clean boundary separation across `libhyle-bud`, `libhyle-source`, `axil`, and site modules.

## Current Status
- [x] Stage 1 Complete: Active scope resolution & query decoding in `external/hyle/c/libhyle-bud/src/picker.c`.
- [x] Stage 2 Complete: `mods/index/pick.c` updated to evoke `hyle_bud_pick_find_active_scope`.
- [x] Stage 3 Complete: Full Verification: Zero compiler warnings, 100% boundary check passes, all tests pass.

## Build & Run Commands
- Build: `make`
- Run: `make watch` or `./start.sh`
- Unit Tests: `make unit-tests`
- Verification / Boundary Gates: `sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-wasm-imports.sh`
- E2E Tests: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Developed test(s) for the task BEFORE feature code.
- [x] **3. Feature Implementation**: Developed feature to satisfy tests.
- [x] **4. Build & Run**: Built and ran project with zero build errors. Restart server/process to verify clean boot.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed FULL test suite with zero errors.

## Acceptance Criteria & Polish Checklist
- [x] Query parsing and picker active scope resolution live in `libhyle-bud`.
- [x] `mods/index/pick.c` uses `hyle_bud_pick_find_active_scope`.
- [x] Zero compiler warnings, 100% boundary check passes, all tests pass.

## Decisions made
- Migrated picker scope matching and URL decoding helpers into `libhyle-bud`.
- Verified isomorphic WASM build and native builds.

## Files touched
- `docs/current/migrate-dataset-and-ui-engines-to-lower-level.md`
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `mods/index/pick.c`

## Next recommended step
Run final wrap-up flow via `ask_questions`.
