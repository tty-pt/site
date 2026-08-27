# Task: finalize-consumer-module-encapsulation

## Goal
Tackle the remaining high-leverage encapsulation opportunities across consumer modules:
1. Extract the generic HTML sanitizer from `poem/ux/detail.c` into `common/ux/site_ui.c` (`site_ui_sanitize_html`).
2. Remove dead/duplicate field descriptor `song_ff[]` in `song/ux/form.c`.
3. Encapsulate repetitive ordered child item form persistence in `gig.c` (`sb_save_edit_songs_from_form`).

## Current Status
- [x] Stage 1 Complete: `site_ui_sanitize_html` in `common/ux/site_ui.c` and `poem/ux/detail.c` simplified to 1 call.
- [x] Stage 2 Complete: Dead `song_ff[]` descriptor removed from `song/ux/form.c` and `song.c` now evokes `pick_view_collect_desc_fd` directly from schema fields.
- [x] Stage 3 Complete: Ordered sub-item form persistence in `gig.c` encapsulated into `sb_save_edit_songs_from_form`.
- [x] Stage 4 Complete: Full Verification: Zero compiler warnings, 100% boundary check passes, all tests pass.

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
- [x] Stage 1: HTML sanitizer lives in `site_ui.c`, `poem/ux/detail.c` reduced by ~200 lines.
- [x] Stage 2: Dead `song_ff[]` removed from `song/ux/form.c` and `song.c`.
- [x] Stage 3: Sub-item row persistence in `gig.c` cleanly encapsulated into `sb_save_edit_songs_from_form`.
- [x] Stage 4: Zero compiler warnings, 100% boundary check passes, all tests pass.

## Decisions made
- Consolidated HTML sanitization in `site_ui.c`.
- Used descriptor-driven picker collection `pick_view_collect_desc_fd` in `song.c`.

## Files touched
- `docs/current/finalize-consumer-module-encapsulation.md`
- `mods/common/ux/site_ui.h`
- `mods/common/ux/site_forms.c`
- `mods/poem/ux/detail.c`
- `mods/song/ux/form.c`
- `mods/song/song.c`
- `mods/gig/gig.c`

## Next recommended step
Run final wrap-up flow via `ask_questions`.
