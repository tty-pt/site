# Quest: schema-driven-forms-libhyle-bud

## Goal
Implement `hyle_bud_form` in `external/hyle/c/libhyle-bud` and eliminate legacy `form_field_t` and `site_ui_form_fields*` across the site.

## Original request
> Implement hyle_bud_form in libhyle-bud and eliminate legacy form_field_t across the site

## Parent Quest
[[site-simplification-deep-cleanup]]

## Current Status
- [x] in progress · not started · done

## Build & Run Commands
- Build: `make`
- Run: `./start.sh`
- Test: `sh tests/unit/run-bud-form.sh && make standalone-unit-tests && make pages-test && bash tests/pages/50-pickers.sh && AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Created unit test `tests/unit/bud_form_test.c` and runner `tests/unit/run-bud-form.sh`.
- [x] **3. Feature Implementation**: Implemented `hyle_bud_form` in `external/hyle/c/libhyle-bud/src/form.c` and declared in `<hyle-bud/hyle-bud.h>`.
- [x] **4. Build & Run**: Build clean, compile with zero errors across native and WASM targets.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs; obsolete form helpers deleted.
- [x] **6. Server Restart & Full Test Suite**: Restart fresh server instance and pass full test suite (105/105 E2E tests, unit tests, pages tests).

## In-Depth Analysis & Findings
- `mods/common/ux/site_forms.c` had 500+ lines of ad-hoc form generation logic (`site_ui_form_fields`, `site_ui_form_fields_ex`, `site_ui_sibling_get_form`, `pick_ref_node`, `pick_inline_single`, `pick_inline_multi`, `site_ui_default_field_label`) converting `hyle_schema_desc_t` $\rightarrow$ legacy `form_field_t` arrays on the stack.
- `hyle_bud_form` now lives natively in `external/hyle/c/libhyle-bud` (`src/form.c`), providing declarative schema-to-form generation with automatic field labels, input types, textarea escaping, file inputs, omni-pickers, action buttons, and sibling GET forms.
- `external/hyle/c/libhyle-bud/hyle-bud-wasm.mk` includes `src/form.c` so all WASM builds have full form capabilities without external imports.
- `tests/unit/bud_form_test.c` validates add and edit forms rendered via `hyle_bud_form`.

## Files Touched
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h` (Declared `hyle_bud_form`)
- `external/hyle/c/libhyle-bud/src/form.c` (Implemented `hyle_bud_form` and textarea escaping)
- `external/hyle/c/libhyle-bud/hyle-bud-wasm.mk` (Added `src/form.c` to `HYLE_BUD_WASM_SRC`)
- `external/hyle/c/libhyle-bud/Makefile` (Added `src/form.o`)
- `external/hyle/c/libhyle-bud/objects-set.mk` (Added `CFLAGS-form-o`)
- `tests/unit/bud_form_test.c` (Unit test for add/edit forms)
- `tests/unit/run-bud-form.sh` (Test runner)
- `Makefile` (Added `run-bud-form.sh` to `standalone-unit-tests`)
- `mods/common/ux/site_forms.c` (Deleted obsolete form functions and delegated `site_ui_form_from_desc` to `hyle_bud_form`)
- `mods/common/ux/site_ui.h` (Cleaned obsolete form function prototypes)

## Remaining Work
- None for Sub-Quest 1! Ready to complete sub-quest and proceed to Sub-Quest 2 of [[site-simplification-deep-cleanup]].

## Next Recommended Step
Archive sub-quest `schema-driven-forms-libhyle-bud` and return to parent quest `site-simplification-deep-cleanup`.
