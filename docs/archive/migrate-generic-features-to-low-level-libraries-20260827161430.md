# Task: migrate-generic-features-to-low-level-libraries

## Goal
Migrate generic web, dataset, search/text, user preferences, and UI infrastructure from site-level modules (`mods/common`, `mods/source`, `mods/index`, `mods/song`) down into lower-level libraries (`external/axil`, `external/stoma`, `external/hyle/c/libhyle-source`, `external/hyle/c/libhyle-bud`) so that site modules become minimal, purely declarative consumers and generic features can be reused across any site.

## Current Status
- [x] Stage 1 Complete: `axil_respond_file` unified with axil's native `mime_hd` lookup map in `external/axil`.
- [x] Stage 2 Complete: String List & Token Utilities in `external/stoma` (`stoma_list_normalize`, `stoma_list_contains`, `stoma_list_append`).
- [x] Stage 3 Complete: Generic Schema Form Parser in `external/hyle/c/libhyle-source` (`hyle_source_parse_row_data_custom`).
- [x] Stage 4 Complete: Struct State Overlay Serializer in `external/hyle/c/libhyle-bud` (`hyle_bud_state_overlay_from_desc`, `hyle_bud_state_overlay_array`).
- [x] Stage 5 Complete: User Preference Storage Abstracted (`user_pref_read`, `user_pref_write`).
- [x] Full Verification: Clean builds, zero compiler warnings, boundary check passes, all targeted and unit tests pass.

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

## Refinements & User Feedback Loops
- User caught hardcoded MIME mapping in `axil_respond_file` when `axil` already had `mime_hd` dictionary.
- Refactored `axil_respond_file` to use the global `mime_hd` registry in `axil` and populated default MIME types (`jpeg`, `jpg`, `png`, `gif`, `webp`, `svg`, `pdf`, `mp3`, `ogg`, `wav`, `json`, `html`, `txt`, `css`, `js`, `wasm`).

---

## Acceptance Criteria & Polish Checklist
- [x] `axil_respond_file` leverages `mime_hd` lookup directly.
- [x] String tokenization and list normalization utilities in `stoma`.
- [x] Form row parser in `libhyle-source`.
- [x] Struct state overlay serialization in `libhyle-bud`.
- [x] User preferences abstracted in `common_storage.c`.
- [x] Zero compiler warnings across all components.
- [x] 100% boundary check and test pass.

## Decisions made
- Reused `axil`'s `mime_hd` map for `axil_respond_file` extension lookup instead of local arrays.
- Preserved strict validation against `allowed_exts` when passed to `axil_respond_file`.

## Files touched
- `docs/current/migrate-generic-features-to-low-level-libraries.md`
- `external/axil/include/ttypt/axil.h`
- `external/axil/src/libaxil.c`
- `external/stoma/include/stoma/stoma.h`
- `external/stoma/src/token.c`
- `external/hyle/c/libhyle-source/include/hyle-source/hyle_source.h`
- `external/hyle/c/libhyle-source/src/engine.c`
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/libhyle-bud.c`
- `mods/common/common_response.c`
- `mods/common/common_strlist.c`
- `mods/common/common_storage.c`
- `mods/common/common.h`
- `mods/common/bud_adapter.c`
- `mods/source/source.c`
- `mods/song/song.c`

## Next recommended step
Prompt user for wrap-up flow via `ask_questions`.
