# Quest: cleanup-common-redundancies

## Goal
Eliminate redundant bud_adapter wrappers in mods/common and update consumers to use hyle_bud_state_overlay direct APIs

## Original request
> Goal: Eliminate redundant bud_adapter wrappers in mods/common and update consumers to use hyle_bud_state_overlay direct APIs

## Parent Quest
[[hyle-site-abstractions-simplification]]

## Current Status
- [x] done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `make standalone-unit-tests && make pages-test && AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/gig-edit.test.ts tests/e2e/song-edit.test.ts`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Validated against `gig-edit.test.ts`, `song-edit.test.ts`, and full E2E suite.
- [x] **3. Feature Implementation**: Removed `mods/common/bud_adapter.c` and `bud_adapter.h`; migrated `song` and `gig` modules to direct `hyle_bud_state_overlay_*` calls.
- [x] **4. Build & Run**: Built with zero compiler errors; server restarted cleanly.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed full test suite.

## In-Depth Analysis & Findings
- `mods/common/bud_adapter.c` was an obsolete 1-line wrapper around `hyle_bud_state_overlay_*`.
- Migrated all consumers (`mods/song/song.c`, `mods/gig/gig.c`) to direct `hyle_bud_state_overlay_from_desc` and `hyle_bud_state_overlay_array`.
- Removed `bud_adapter.c`, `bud_adapter.h`, and `bud_adapter_overlay_*` XY declarations from `mods/common/common.h`.

## Files touched
- `mods/song/song.c`
- `mods/gig/gig.c`
- `mods/common/common.h`
- `mods/common/common.c`
- `mods/common/Makefile`
- `mods/common/bud_adapter.c` (removed)
- `mods/common/bud_adapter.h` (removed)

## Acceptance Criteria & Polish Checklist
- [x] `mods/common/bud_adapter.c` deleted.
- [x] `mods/common/bud_adapter.h` deleted.
- [x] Zero compilation errors across all modules.
- [x] `make standalone-unit-tests` passes.
- [x] `make pages-test` passes.
- [x] Targeted and full E2E suites pass with zero errors.

## Next recommended step
Archive sub-quest to return to parent quest [[hyle-site-abstractions-simplification]] for final review and wrap-up.
