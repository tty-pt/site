# Quest: Site Simplification & Deep Abstraction Cleanup

## Goal
Execute a comprehensive simplification of site-side abstractions:
1. Elevate schema-driven form generation into `libhyle-bud` (`hyle_bud_form`) and eliminate legacy `form_field_t`.
2. Migrate all handlers in `song`, `gig`, `grp`, `index` to direct `hyle_bud_picker_view_collect_schema` calls and eliminate `mods/index/pick.c`.
3. Deduplicate `mods/common/common_storage.c` by delegating file/metadata operations to `libhyle-source`.
4. Implement generic sub-entity / ordered-partition endpoints in `external/axil-hyle` to streamline `gig` and `grp` sub-item mutations.

## Current Status
- [x] in progress · not started · done

## Sub-Quests
> Sub-quests, follow-ups, or tangent quests spawned from this quest.
- [x] [[schema-driven-forms-libhyle-bud]] — Implement hyle_bud_form in libhyle-bud and eliminate legacy form_field_t across the site
- [x] [[eliminate-index-pick-shims]] — Migrate remaining pick view collectors to libhyle-bud schema collectors and delete mods/index/pick.c
- [x] [[storage-io-consolidation-libhyle-source]] — Consolidate file and metadata I/O in mods/common/common_storage.c into libhyle-source
- [x] [[generic-ordered-partition-crud-axil-hyle]] — Generic sub-entity & ordered partition CRUD endpoints in axil-hyle

## Why this matters
- Building upon the migration of foreign-key resolution, picker state parsing, and REST CRUD into `external/`, this quest cleaned up all remaining legacy layers in site modules (`form_field_t`, duplicate file utilities, ad-hoc pick view glue, sub-item mutation boilerplate).
- Transforms site modules into pure declarative domain configs with minimal custom code.

## Sub-Quests Roadmap
- [x] **Sub-Quest 1: Schema-Driven Generic Forms in `libhyle-bud` & `form_field_t` Elimination**
  - Implemented `hyle_bud_form(schema, record, action, cancel_href, submit_label, csrf_token, pv, vstr_val)` in `libhyle-bud`.
  - Added unit test `tests/unit/bud_form_test.c` and integrated `hyle-bud-wasm.mk`.
  - Migrated `site_ui_form_from_desc` to `hyle_bud_form` and removed obsolete `form_field_t` generation code from `mods/common/ux/site_forms.c` and `site_ui.h`.
  - Verified with standalone unit tests, smoke pages tests, and 105/105 E2E tests passing.
- [x] **Sub-Quest 2: Eliminate Legacy `mods/index/pick.c` XY Shims**
  - Implemented `hyle_bud_picker_view_collect_auto_fields_schema` in `external/hyle/c/libhyle-bud/src/picker.c`.
  - Migrated all remaining callers in `mods/song/`, `mods/gig/`, and `mods/index/` to direct `hyle_bud_picker_view_collect_schema` and `hyle_bud_picker_view_collect_auto_fields_schema`.
  - Deleted `mods/index/pick.c` (-660 lines) and removed 10 dead `XY_DECL` declarations from `mods/index/index.h`.
  - Verified with standalone unit tests (`run-bud-picker-collect.sh`), smoke pages tests, and 105/105 E2E tests passing.
- [x] **Sub-Quest 3: Storage & File I/O Consolidation into `libhyle-source`**
  - Upgraded `source_util_write_file` to use atomic temp-file creation and rename (`.tmp.<pid>`) with fsync.
  - Implemented and exposed `hyle_source_is_safe_id`, `hyle_source_slurp_file`, `hyle_source_write_file`, `hyle_source_remove_path_recursive`, `hyle_source_resolve_doc_root` in `libhyle-source`.
  - Added unit test `tests/unit/source_utils_test.c` and runner `tests/unit/run-source-utils.sh`.
  - Refactored `mods/common/common_storage.c` to delegate file I/O to `libhyle-source`.
  - Verified with standalone unit tests, smoke pages tests, and 105/105 E2E tests passing.
- [x] **Sub-Quest 4: Generic Ordered Partition CRUD in `axil-hyle`**
  - Added REST sub-resource endpoints in `axil-hyle` for ordered collections (`GET/POST/PUT/DELETE /api/dataset/:id/:key/ordered[/:n]`).
  - Added HTTP methods `do_PUT` and `do_DELETE` to `external/axil` engine (`libaxil.c`, `axil.h`, `axil.c`, `test-routes.c`, `test-auth.c`).
  - Unified QS budget cutoff constant `HYLE_BUD_PICK_QS_BUDGET` (2048) across `picker.h`, `hyle-bud.h`, `picker.c`, and `form.c`.
  - Added E2E test `tests/e2e/dataset-ordered-partition.test.ts`.
  - Passed standalone unit tests, smoke pages tests, `50-pickers.sh`, and 106/106 E2E tests.

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `make standalone-unit-tests && make pages-test && bash tests/pages/50-pickers.sh && AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Developed targeted unit/integration tests before editing code.
- [x] **3. Feature Implementation**: Executed modular refactoring phase by phase across all 4 sub-quests.
- [x] **4. Build & Run**: Verified zero build warnings or errors; clean server boot.
- [x] **5. Clean Code**: Zero debug artifacts, zero dead code, zero magic values.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and passed full test suite (106/106 E2E tests).

## Next Recommended Step
Archive all completed quests.
