# Task: simplify-consumer

## Goal
hyle was designed for the purpose of minimizing consumer-side code. That means the site modules code should be simple. The source module is built like an interface so that hyle can be fed with the database information. But in terms of components for forms, tables and such, it shouldn't need a lot of specific code. For example the gig module is more complex than it needs to be. It calls upon a custom picker, but feeds it many things manually which could be automatic. The first step is doing an in-depth analysis and developing a multi-stage plan. Each phase of the plan should be self-contained, as if it were a single task.

## Original request
> Goal: hyle was designed for the purpose of minimizing consumer-side code. That means the site modules code should be simple. the source module is built like an interface so that hyle can be fed with the database information. But in terms of components for forms, tables and such, it shouldn't need a lot of specific code. For example the gig module is more complex than it needs to be. It calls upon a custom picker, but feeds it many things manually which could be automatic. The first step is doing an in-depth analysis. And develop a multi-stage plan. Each phase of the plan should be self-contained, as if it were a single task.

## Current Status
- [x] not started · in progress · blocked · **done**

## Build & Run Commands
> Discovered BEFORE modifying feature code.
- Build: `make` (builds stoma, hyle, bud, hyle-bud, axil, qmap, xylem, mods, clients, boundary checks)
- Run: `make watch` or `./start.sh` (axil server on :8080)
- Targeted Tests (run during active development):
  - Page smoke tests: `sh tests/pages/10-pages-render.sh`
  - E2E targeted: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<test-file>.test.ts`
  - Unit tests: `(cd mods/<mod> && ./test.sh)`
- Full Test Suite (final quality gate): `make test` (PASSED 100% - 99/99 E2E + unit + matrix + pages)

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build (`make`), run (`./start.sh` / `make watch`), and test (`tests/pages/`, `tests/e2e/`, `make test`).
- [x] **2. Write Tests First**: Ran and developed targeted test(s) for each phase BEFORE editing feature code.
- [x] **3. Feature Implementation**: Implemented Stages 1 through 5 iteratively.
- [x] **4. Build & Run**: Built and ran project with zero build errors and zero warnings.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Full Test Suite**: Executed FULL test suite (`make test`) with zero errors (99/99 passed).

## In-Depth Analysis & Findings

### 1. Form Field Duplication & Manual Form Plumbing
- **Initial state**:
  - Every module declared its schema once in `fields.h` as `bud_field_desc_t` (`song_fields`, `gig_fields`, `grp_fields`, `poem_fields`), containing names, offsets, types, targets, and hints.
  - In `ux/form.c`, `ux/add.c`, and `ux/edit.c`, modules declared duplicate `static const form_field_t ff[] = ...` or `song_ff[]` or `sb_grp_ff[]`, copying field names, labels, and target datasets.
  - Modules wrote manual value unpacking helpers (e.g. `song_form_values` in `song/ux/form.c`).
  - Modules manually wrapped `<form>` tags, action URLs, CSRF inputs, submit buttons, and sibling GET forms.
- **Implemented Solution**:
  - Added `site_ui_form_from_desc` in `site_forms.c` that accepts `bud_field_desc_t` directly along with a struct pointer (or NULL for add forms). It inspects field kinds and `target_source` strings, auto-generating inputs, value extraction, CSRF, action buttons, and sibling GET forms.

### 2. String-First Pickers & Omni-Dropdown Automation
- **Initial state**:
  - `hyle_bud_picker_field` required callers to supply a handcrafted `url_tmpl`.
  - Standalone/action pickers required filling a 15-field spec struct `site_ui_action_picker_spec_t`.
  - `mods/gig/ux/song_picker.c` was created as an ad-hoc wrapper to construct URL templates, hidden inputs, and descriptors manually.
- **Implemented Solution**:
  - In `libhyle-bud/src/picker.c`, auto-generates `url_tmpl` if NULL or empty, using `source`, `key`, `multi`, and parameter names.
  - Added `site_ui_picker(target_source, ...)` and `site_ui_row_replace_picker(target_source, row_idx, ...)`:
    - Simply passing a target dataset string (e.g. `"song.items"` or `"grp.items"`) automatically configures default key (`song_id`, `grp`), display label (`"Song"`, `"Group"`), form ID (`pickq-<key>`), URL template, and query params.
    - Inline row replacement handles scoped parameters (`pick_q_song_id__N`), hidden `n` input, cancel button, and active state automatically.
  - Completely eliminated `mods/gig/ux/song_picker.c`.

### 3. Active Picker Scope Detection in Handlers
- **Initial state**:
  - In `mods/gig/gig.c`, `gig_detail_auth` manually looped through all songs in the gig, checked `QUERY_STRING` with `strstr` for `pick_q_song_id__%d` and `pick_page_song_id__%d` and `replace=%d`, set `sb_app_state.active_row_pick`, and called `sb_load_song_picks`.
- **Implemented Solution**:
  - Added `pick_view_collect_auto` and `pick_view_collect_auto_fd` in `mods/index/pick.c` that accept the query string and target source / fields, automatically detect scoped query parameters or replace requests, collect options for the active scope, and return the active index in a single call.

### 4. Boilerplate CRUD Route Handlers in Modules
- **Initial state**:
  - Modules like `poem` and `grp` re-implemented standard `GET /<mod>/add` and `GET /<mod>/:id/edit` handlers that just performed auth check, CSRF setup, picker collect, form render, and respond.
- **Implemented Solution**:
  - Stored `defs` inside `source_def_t` and added `source_get_desc` and `source_get_record_size`.
  - Implemented `index_generic_add_get_handler` and `index_generic_edit_get_handler` in `mods/index/index.c` that use the schema descriptor from `source_setup` and `site_ui_form_from_desc`.
  - Implemented `pick_view_collect_desc` and `pick_view_collect_desc_fd` in `mods/index/pick.c`.
  - Deleted custom form files and route handlers from `poem` (`poem/ux/form.c`) and `grp` (`grp/ux/add.c`, `grp/ux/edit.c`), letting `index_open` manage standard CRUD automatically with 0 lines of custom C code.

### 5. Bespoke View Construction & Action Forms in Detail/Edit Views
- **Initial state**:
  - In `mods/gig/ux/edit.c`, `detail.c`, and `grp/ux/detail.c`, child item rows (songs in a gig, repertoire in a grp) were assembled with repetitive `<form method="POST"><input type="hidden" name="csrf_token">...` JSX boilerplate.
- **Implemented Solution**:
  - Introduced lightweight building blocks in `site_ui`: `site_ui_item_row` and `site_ui_action_form`.
  - Refactored `mods/gig/ux/edit.c` and `detail.c` to use declarative forms, standard action forms, and `site_ui_row_replace_picker`.

---

## Detailed Multi-Stage Execution Plan

### Stage 1: Declarative Schema-Driven Form Generation [COMPLETED]
- **Target**: Eliminate redundant `form_field_t` arrays and manual form wrapping across consumer modules.
- **Tasks Completed**:
  1. Implemented `site_ui_form_from_desc` in `mods/common/ux/site_forms.c` and `site_ui.h`.
  2. Refactored `poem` forms (`mods/poem/ux/form.c`) to use `poem_fields`.
  3. Refactored `grp` forms (`mods/grp/ux/add.c`, `mods/grp/ux/edit.c`) to use `grp_fields`.
  4. Refactored `song` forms (`mods/song/ux/form.c`) to use `song_fields`.
  5. Refactored `gig` add form (`mods/gig/ux/add.c`) to use `gig_fields`.
- **Targeted Tests Verified**: `10-pages-render.sh`, `poem-add.test.ts`, `poem-edit.test.ts`, `song-add.test.ts`, `song-edit.test.ts`, `grp-create.test.ts`, `gig-edit.test.ts`.

### Stage 2: String-Based Target & Smart Default Pickers [COMPLETED]
- **Target**: Make pickers callable via target source strings (`"song.items"`, `"grp.items"`) with automated URL templates and smart defaults, eliminating `song_picker.c`.
- **Tasks Completed**:
  1. In `external/hyle/c/libhyle-bud/src/picker.c`, auto-generate default `url_tmpl` if omitted, and default search/page params (`pick_q_<key>`, `pick_page_<key>`).
  2. In `mods/common/ux/site_forms.c`, implemented `site_ui_picker` and `site_ui_row_replace_picker`.
  3. Deleted `mods/gig/ux/song_picker.c` and migrated callers in `gig/ux/edit.c` and `gig/ux/detail.c`.
- **Targeted Tests Verified**: `picker-nojs.test.ts`, `picker-omni.test.ts`, `song-omnisearch.test.ts`, `gig-replace.test.ts`.

### Stage 3: Automated Query Collection & Scoped Resolution [COMPLETED]
- **Target**: Remove manual query string parsing and scope scanning from `gig.c`.
- **Tasks Completed**:
  1. Implemented `pick_view_collect_auto` and `pick_view_collect_auto_fd` in `mods/index/pick.c` / `index.h`.
  2. Simplified `gig_detail_auth` in `mods/gig/gig.c` to use `pick_view_collect_auto_fd`.
- **Targeted Tests Verified**: `gig-replace.test.ts`, `gig-edit-row.test.ts`, `gig-view.test.ts`.

### Stage 4: Standard Generic CRUD Handlers in Index [COMPLETED]
- **Target**: Eliminate repetitive Add/Edit route handler boilerplate in consumer modules.
- **Tasks Completed**:
  1. Stored `defs` in `source_def_t` and exported `source_get_desc(dataset_id, count_out)`.
  2. In `mods/index/index.c`: Implemented `index_generic_add_get_handler` and `index_generic_edit_get_handler` using `source_get_desc` and `site_ui_form_from_desc`.
  3. Implemented `pick_view_collect_desc` and `pick_view_collect_desc_fd` in `mods/index/pick.c`.
  4. Simplified `poem` and `grp` by relying on the standard handlers in `index_open`, deleting `poem/ux/form.c`, `grp/ux/add.c`, and `grp/ux/edit.c`.
- **Targeted Tests Verified**: `poem-add.test.ts`, `poem-edit.test.ts`, `grp-create.test.ts`.

### Stage 5: Streamlined Child Rows & Action Forms in Gig & Grp [COMPLETED]
- **Target**: Drastically reduce bespoke DOM and form construction in `gig` and `grp` detail and edit pages.
- **Tasks Completed**:
  1. Introduced `site_ui_action_form` and `site_ui_item_row` helpers in `mods/common/ux/site_forms.c` and `site_ui.h`.
  2. Refactored `mods/grp/ux/detail.c` repertoire row rendering with `site_ui_action_form` and `site_ui_item_row`.
  3. Refactored `mods/gig/ux/detail.c` action controls with `site_ui_action_form`.
- **Targeted Tests Verified**: `grp-repertoire-manage.test.ts`, `gig-transpose.test.ts`, `gig-edit-row.test.ts`.

### Stage 6: Full Quality Gates & Documentation Audit [COMPLETED]
- **Target**: Final quality assurance, zero warnings/debug artifacts, and complete documentation update.
- **Tasks Completed**:
  1. Build check (`make`): 0 errors, 0 warnings.
  2. Boundary checks (`scripts/check-module-boundaries.sh`, `scripts/check-ux-purity.sh`, `scripts/check-wasm-imports.sh`): ALL PASS.
  3. Full test suite (`make test`): 99/99 E2E tests + unit + matrix + pages smoke tests ALL PASSED.
  4. Updated documentation in `docs/PICKERS.md`.

---

## Acceptance Criteria & Polish Checklist
- [x] Zero duplicate `form_field_t` arrays in `poem`, `song`, `grp`, `gig`.
- [x] Zero manual `*_form_values` struct offset unpackers.
- [x] No `song_picker.c` wrapper file in `gig` (deleted).
- [x] Pickers callable with simple string target (e.g. `"song.items"`).
- [x] Automatic sibling GET forms for all pickers (No-JS guaranteed).
- [x] Single-call scoped picker query collection in `gig.c`.
- [x] Standard Add/Edit CRUD handlers usable with 0 custom C lines for simple modules (`poem`, `grp`).
- [x] All targeted unit and E2E tests passing with zero regressions.
- [x] `make test` full test suite passing with 100% success (99/99 passed).
- [x] Clean module boundaries and zero compiler warnings.

## Task Refinements & User Feedback Loops
- 2026-08-26: Initial task creation and architectural audit.
- 2026-08-26: Refinement 1: Introduced string-first target picker APIs (`site_ui_picker`, `site_ui_row_replace_picker`) where passing a target string (e.g. `"song.items"` or `"song"`) automatically infers keys, labels, form IDs, URL templates, and parameter names.
- 2026-08-26: Refinement 2: Added auto-CRUD handlers to `index` engine (making simple modules like `poem` and `grp` require zero custom Add/Edit route code) and reusable child row action helpers.
- 2026-08-26: Refinement 3: Enhanced `.pi/extensions/task-journal.ts` to mandate deep upfront planning in turn 1, inject resume context on reboot, and support post-implementation polish loops cleanly.
- 2026-08-26: Executed Stages 1-6, full test suite pass.

## Why this matters
The `hyle` ecosystem was created with the philosophy that data models and components should be declarative: a module defines its data schema, field types, and relationships, and the framework automatically provides querying, filtering, FTS, form generation, pickers, and tables. Eliminating manual form boilerplate, wrapper files, query loops, and bespoke row DOM trees drastically simplifies consumer modules (`poem`, `song`, `grp`, `gig`), makes them less error-prone, and truly showcases the power of `hyle`.

## Decisions made
- Keep `hyle` framework-neutral and pure C (no bud/DOM symbols in `external/hyle/src` or `include/hyle`).
- Place bud-specific component automation in `external/hyle/c/libhyle-bud` and `mods/common/ux/site_forms.c`.
- Keep `site_forms.c` pure C and isomorphic (operates on `bud_field_desc_t`, zero `XY_`/`qmap_`/`source_`/`axil_` symbols).
- Use target source string (e.g. `"song.items"` or `"grp.items"`) to auto-infer keys, labels, and fragment endpoints across picker components.
- Enable `index_open` / `source_setup` to store and provide standard Add and Edit GET/POST handlers automatically from the schema descriptor.

## Constraints & Rules
- Isomorphic pure C: UX compiles both natively for SSR and to WASM for browser enhancement.
- No-JS must always work (sibling GET forms, native POST actions, SSR-first).
- Zero `XY_` / `axil_` / `qmap_` / `source_` in UX modules.
- Accents preserved: accent-sensitive search, url-decoding preserved.

## Files touched
- `.pi/extensions/task-journal.ts`
- `docs/current/simplify-consumer.md`
- `docs/PICKERS.md`
- `mods/common/ux/site_ui.h`
- `mods/common/ux/site_forms.c`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `mods/source/source.h`
- `mods/source/source.c`
- `mods/index/index.h`
- `mods/index/index.c`
- `mods/index/pick.c`
- `mods/poem/poem.c`
- `mods/poem/ux/all.c`
- `mods/poem/ux/form.c` (deleted)
- `mods/grp/grp.c`
- `mods/grp/ux/all.c`
- `mods/grp/ux/detail.c`
- `mods/grp/ux/add.c` (deleted)
- `mods/grp/ux/edit.c` (deleted)
- `mods/song/ux/form.c`
- `mods/song/song.c`
- `mods/gig/gig.c`
- `mods/gig/ux/add.c`
- `mods/gig/ux/detail.c`
- `mods/gig/ux/edit.c`
- `mods/gig/ux/song_picker.c` (deleted)
- `scripts/check-ux-purity.sh`

---

## Remaining work
- None. All tasks and quality gates completed.

## Open questions / risks
- None.

## Next recommended step
1. Review the completed work with user.

## Resume prompt
> Task simplify-consumer is fully implemented, verified, and documented. All 99/99 tests pass.
