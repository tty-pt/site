# Task: update-docs

## Goal
Comprehensive documentation update across the entire repository. Bring all markdown files, architecture diagrams, convention guides, and module READMEs up to date with recent architectural advancements:
1. Pure `bud` decoupling (5-field UI binder `bud_field_desc_t` with zero database/storage dependencies).
2. Canonical `hyle_schema_desc_t` (pure C data schema in `external/hyle/include/hyle/schema.h`).
3. Standalone `libhyle-source` persistence engine (`external/hyle/c/libhyle-source/`) with pluggable storage drivers (`hyle_source_store_ops_t`: `store_fs`, `store_mem`, custom stores).
4. Declarative form builder (`site_ui_form_from_desc`) and string-first pickers (`site_ui_picker`, `site_ui_row_replace_picker`).
5. Automated query collection (`pick_view_collect_auto_fd`, `pick_view_collect_desc_fd`) and generic CRUD routes in `mods/index`.
6. Declarative module lifecycle (`index_module_init`) and action primitives (`site_ui_action_form`, `site_ui_item_row`).
7. Fixing stale path references (e.g. `items/` -> `var/`, `choir` -> `grp`, `songbook` -> `gig`).

## Original request
> Ok, very good. Now, docs all over the project are bound to be outdated, given all the tasks we've been doing. Search for all markdown files across the project, and read them fully. Let's prepare a new task with extensive information about how to update them. This implies a lot of reading. Don't be lazy.

## Current Status
- [x] not started · in progress · blocked · **done**

## Build & Run Commands
- Build: `make`
- Run: `make watch` or `./start.sh` (axil on :8080)
- Boundary & Purity Checks: `sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-wasm-imports.sh`
- Targeted Tests:
  - Page smoke tests: `sh tests/pages/10-pages-render.sh`
  - E2E targeted: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<test-file>.test.ts`
  - Unit tests: `(cd mods/<mod> && ./test.sh)`
- Full Test Suite (final quality gate): `make test` (PASSED 100% - 99/99 passed)

## TDD & Quality Checklist
- [x] **1. Discovery & Deep Research**: Scanned and read all 32 markdown files across root, `docs/`, `mods/`, and `external/`.
- [x] **2. Documentation Updates (Phased Execution)**:
  - [x] **Phase 1: Root & Top-Level Index Docs** (`README.md`, `AGENTS.md`, `docs/OVERVIEW.md`)
  - [x] **Phase 2: Core Architecture & Invariants Docs** (`docs/ARCHITECTURE.md`, `docs/DESIGN.md`, `docs/GOALS.md`, `docs/VIOLATIONS.md`)
  - [x] **Phase 3: Schema, Forms, and Component Docs** (`docs/SCHEMA.md`, `docs/CONVENTIONS.md`, `docs/C-ISOMORPHIC-BUD.md`, `docs/SSR-CONTRACT.md`, `docs/PICKERS.md`, `docs/FILTERS.md`, `docs/WASM-BRIDGE.md`)
  - [x] **Phase 4: Build, Testing, and Operations Docs** (`docs/BUILD.md`, `docs/TESTING.md`, `docs/STYLING.md`, `docs/EXTENSIONS.md`)
  - [x] **Phase 5: Site Module & External Library READMEs** (`mods/common/README.md`, `mods/song/README.md`, `mods/gig/README.md`, `mods/grp/README.md`, `mods/poem/README.md`, `external/bud/README.md`, `external/hyle/README.md`)
- [x] **3. Quality Gate & Consistency Verification**:
  - [x] Zero broken markdown links or stale file references.
  - [x] Code snippets and signatures in docs match current C code exactly.
  - [x] `make` compiles with 0 errors and 0 warnings.
  - [x] Boundary checks (`check-module-boundaries.sh`, `check-ux-purity.sh`, `check-wasm-imports.sh`) pass 100%.
  - [x] Full test suite (`make test`) passes 100% (99/99 E2E tests).

---

## Detailed Summary of Changes Completed

1. **`README.md`**: Fixed directory creation (`var/` paths), updated module catalog (`/grp`, `/gig`), expanded requirements with submodules (`bud`, `hyle`, `stoma`, `libhyle-bud`, `libhyle-source`).
2. **`AGENTS.md` & `docs/OVERVIEW.md`**: Updated stack tables and guidelines with `external/hyle/c/libhyle-source`, highlighted pure `bud` (5-field UI binder) vs pure `hyle` (`hyle_schema_desc_t`) independence.
3. **`docs/ARCHITECTURE.md` & `docs/DESIGN.md`**: Documented the 3-tier data architecture (`hyle` $\rightarrow$ `libhyle-source` $\rightarrow$ `libhyle-bud` $\rightarrow$ `bud`), thin `mods/source`, pluggable `hyle_source_store_ops_t` storage, `index_module_init`, `site_ui_form_from_desc`, and string-first pickers.
4. **`docs/GOALS.md` & `docs/VIOLATIONS.md`**: Updated library graph and confirmed 0 open violations.
5. **`docs/SCHEMA.md`, `docs/CONVENTIONS.md`, `docs/C-ISOMORPHIC-BUD.md`**: Defined canonical `hyle_schema_desc_t` (in `external/hyle/include/hyle/schema.h`), `field_macros.h` macros, and stride-aware WASM state unpacking.
6. **`docs/BUILD.md` & `docs/TESTING.md`**: Documented `hyle-source` compilation targets, `-I` include flags, and standalone C test suites.
7. **`mods/*/README.md` & `external/*/README.md`**: Updated module and library READMEs to reflect declarative forms, storage structure, and decoupled architectures.

---

## Remaining Work
- None.

## Next Recommended Step
- All documentation is synchronized and verified against the codebase.
