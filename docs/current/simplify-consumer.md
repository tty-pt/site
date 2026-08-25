# Task: simplify-consumer

## Goal
hyle was designed for the purpose of minimizing consumer-side code. That means the site modules code should be simple. The source module is built like an interface so that hyle can be fed with the database information. But in terms of components for forms, tables and such, it shouldn't need a lot of specific code. For example the gig module is more complex than it needs to be. It calls upon a custom picker, but feeds it many things manually which could be automatic. The first step is doing an in-depth analysis and developing a multi-stage plan. Each phase of the plan should be self-contained, as if it were a single task.

## Original request
> Goal: hyle was designed for the purpose of minimizing consumer-side code. That means the site modules code should be simple. the source module is built like an interface so that hyle can be fed with the database information. But in terms of components for forms, tables and such, it shouldn't need a lot of specific code. For example the gig module is more complex than it needs to be. It calls upon a custom picker, but feeds it many things manually which could be automatic. The first step is doing an in-depth analysis. And develop a multi-stage plan. Each phase of the plan should be self-contained, as if it were a single task.

## Current Status
- [x] In-depth analysis completed across all consumer modules and framework layers
- [x] Comprehensive 6-stage plan detailed & verified
- [x] Task journal extension enhanced for rapid upfront planning, session resumption context, and post-implementation polish loops
- [ ] Brainstorming & confirmation with user
- [ ] Stage 1 implementation (Declarative Schema-Driven Form Generation)
- [ ] Stage 2 implementation (String-Based Target & Smart Default Pickers)
- [ ] Stage 3 implementation (Automated Query Collection & Scoped Resolution)
- [ ] Stage 4 implementation (Standard Generic CRUD Handlers in Index)
- [ ] Stage 5 implementation (Streamlined Child Rows & Action Forms in Gig & Grp)
- [ ] Stage 6 verification, full quality gates & documentation audit

## Build & Run Commands
> Discovered BEFORE modifying feature code.
- Build: `make` (builds stoma, hyle, bud, hyle-bud, axil, qmap, xylem, mods, clients, boundary checks)
- Run: `make watch` or `./start.sh` (axil server on :8080)
- Targeted Tests (run during active development):
  - Page smoke tests: `sh tests/pages/10-pages-render.sh`
  - E2E targeted: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<test-file>.test.ts`
  - Unit tests: `(cd mods/<mod> && ./test.sh)`
- Full Test Suite (final quality gate only): `make test`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build (`make`), run (`./start.sh` / `make watch`), and test (`tests/pages/`, `tests/e2e/`, `make test`).
- [ ] **2. Write Tests First**: Develop / verify targeted test(s) for each phase BEFORE editing feature code.
- [ ] **3. Feature Implementation**: Implement each phase iteratively.
- [ ] **4. Build & Run**: Verify zero build errors or compiler warnings.
- [ ] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [ ] **6. Full Test Suite**: Execute FULL test suite (`make test`) with zero errors.

## In-Depth Analysis & Findings

### 1. Form Field Duplication & Manual Form Plumbing
- **Current state**:
  - Every module declares its schema once in `fields.h` as `bud_field_desc_t` (`song_fields`, `gig_fields`, `grp_fields`, `poem_fields`), containing names, offsets, types, targets, and hints.
  - In `ux/form.c`, `ux/add.c`, and `ux/edit.c`, every module declares a duplicate `static const form_field_t ff[] = ...` or `song_ff[]` or `sb_grp_ff[]`, copying field names, labels, and target datasets.
  - Modules write manual value unpacking helpers (e.g. `song_form_values` in `song/ux/form.c` iterating offsets to build `const char *vals[]`).
  - Modules manually wrap `<form>` tags, action URLs, CSRF inputs, submit buttons, and sibling GET forms.
- **Solution**:
  - Provide `site_ui_form_from_desc` in `site_forms.c` that accepts `bud_field_desc_t` directly along with a struct pointer (or NULL for add forms). It inspects field kinds and `target_source` strings, auto-generating inputs, value extraction, CSRF, action buttons, and sibling GET forms.

### 2. String-First Pickers & Omni-Dropdown Automation
- **Current state**:
  - `hyle_bud_picker_field` requires callers to supply a handcrafted `url_tmpl` (e.g. `"/pick/song.items/options?key=song_id&multi=0&label=&sel={sel}&pick_q_song_id={q}&pick_page_song_id={page}"`).
  - Standalone/action pickers require filling a 15-field spec struct `site_ui_action_picker_spec_t`.
  - `mods/gig/ux/song_picker.c` was created as an ad-hoc wrapper to construct URL templates, hidden inputs, and descriptors manually.
- **Solution**:
  - In `libhyle-bud/src/picker.c`, auto-generate `url_tmpl` if NULL or empty, using `source`, `key`, `multi`, and parameter names.
  - Provide `site_ui_picker(target_source, ...)` and `site_ui_row_replace_picker(target_source, row_idx, ...)`:
    - Simply passing a target dataset string (e.g. `"song.items"` or `"grp.items"`) automatically configures default key (`song_id`, `grp`), display label (`"Song"`, `"Group"`), form ID (`pickq-<key>`), URL template, and query params.
    - Inline row replacement handles scoped parameters (`pick_q_song_id__N`), hidden `n` input, cancel button, and active state automatically.
  - Completely eliminate `mods/gig/ux/song_picker.c`.

### 3. Active Picker Scope Detection in Handlers
- **Current state**:
  - In `mods/gig/gig.c`, `gig_detail_auth` manually loops through all songs in the gig, checks `QUERY_STRING` with `strstr` for `pick_q_song_id__%d` and `pick_page_song_id__%d` and `replace=%d`, sets `sb_app_state.active_row_pick`, and calls `sb_load_song_picks`.
- **Solution**:
  - Introduce `pick_view_collect_auto` in `mods/index/pick.c` that accepts the query string and target source / fields, automatically detects scoped query parameters or replace requests, collects options for the active scope, and returns the active index.

### 4. Boilerplate CRUD Route Handlers in Modules
- **Current state**:
  - Modules like `poem`, `song`, `grp` re-implement standard `GET /<mod>/add` and `GET /<mod>/:id/edit` handlers that just perform auth check, CSRF setup, picker collect, form render, and respond.
- **Solution**:
  - Implement `index_generic_add_get_handler` and `index_generic_edit_get_handler` in `mods/index/index.c` that use the schema descriptor from `source_setup` and `site_ui_form_from_desc`. Simple modules require zero custom CRUD handler code.

### 5. Bespoke View Construction & Action Forms in Detail/Edit Views
- **Current state**:
  - In `mods/gig/ux/edit.c`, `detail.c`, and `grp/ux/detail.c`, child item rows (songs in a gig, repertoire in a grp) are assembled with repetitive `<form method="POST"><input type="hidden" name="csrf_token">...` JSX boilerplate.
- **Solution**:
  - Introduce lightweight building blocks in `site_ui`: `site_ui_item_row` and `site_ui_action_form`.
  - Simplify `edit.c` and `detail.c` to use declarative forms, standard action forms, and `site_ui_row_replace_picker`.

---

## Detailed Multi-Stage Execution Plan

### Stage 1: Declarative Schema-Driven Form Generation
- **Target**: Eliminate redundant `form_field_t` arrays and manual form wrapping across consumer modules.
- **Tasks**:
  1. Implement `site_ui_form_from_desc` in `mods/common/ux/site_forms.c` and `site_ui.h`. It takes `(action, cancel_href, submit_label, desc, struct_ptr, csrf_token, pv, vstr_val)`.
  2. Refactor `poem` forms (`mods/poem/ux/form.c`) to use `poem_fields`.
  3. Refactor `grp` forms (`mods/grp/ux/add.c`, `mods/grp/ux/edit.c`) to use `grp_fields`.
  4. Refactor `song` forms (`mods/song/ux/form.c`) to use `song_fields` (removing `song_ff` and `song_form_values`).
  5. Refactor `gig` add form (`mods/gig/ux/add.c`) to use `gig_fields`.
- **Targeted Tests**: `10-pages-render.sh`, `poem-add.test.ts`, `song-add.test.ts`, `song-edit.test.ts`, `grp-create.test.ts`, `gig-edit.test.ts`.

### Stage 2: String-Based Target & Smart Default Pickers
- **Target**: Make pickers callable via target source strings (`"song.items"`, `"grp.items"`) with automated URL templates and smart defaults, eliminating `song_picker.c`.
- **Tasks**:
  1. In `external/hyle/c/libhyle-bud/src/picker.c`, auto-generate default `url_tmpl` if omitted, and default search/page params (`pick_q_<key>`, `pick_page_<key>`).
  2. In `mods/common/ux/site_forms.c`, implement:
     - `site_ui_picker(target, post_action, get_action, csrf, pv, auto_submit)`
     - `site_ui_row_replace_picker(target, row_idx, cur_id, cur_title, post_action, back, csrf, pv)`
  3. Delete `mods/gig/ux/song_picker.c` and migrate callers in `gig/ux/edit.c` and `gig/ux/detail.c`.
- **Targeted Tests**: `picker-nojs.test.ts`, `picker-omni.test.ts`, `song-omnisearch.test.ts`, `gig-replace.test.ts`.

### Stage 3: Automated Query Collection & Scoped Resolution
- **Target**: Remove manual query string parsing and scope scanning from `gig.c`.
- **Tasks**:
  1. Implement `pick_view_collect_auto` in `mods/index/pick.c` / `index.h` (scans query string for `pick_q_<key>__<scope>`, `replace=<scope>`, etc., and fills `pick_view_t` for the active scope).
  2. Simplify `gig_detail_auth` and `sb_load_song_picks` in `mods/gig/gig.c` to use `pick_view_collect_auto`.
- **Targeted Tests**: `gig-replace.test.ts`, `gig-edit-row.test.ts`, `gig-view.test.ts`.

### Stage 4: Standard Generic CRUD Handlers in Index
- **Target**: Eliminate repetitive Add/Edit route handler boilerplate in consumer modules.
- **Tasks**:
  1. Store `defs` in `source_def_t` and export `source_get_desc(dataset_id, count_out)`.
  2. In `mods/index/index.c`: Implement `index_generic_add_get_handler` and `index_generic_edit_get_handler` using `source_get_desc` and `site_ui_form_from_desc`.
  3. Simplify `poem` and `grp` by having them rely on the standard handlers in `index_open`, deleting custom boilerplate.
- **Targeted Tests**: `poem-add.test.ts`, `poem-edit.test.ts`, `grp-create.test.ts`.

### Stage 5: Streamlined Child Rows & Action Forms in Gig & Grp
- **Target**: Drastically reduce bespoke DOM and form construction in `gig` and `grp` detail and edit pages.
- **Tasks**:
  1. Introduce `site_ui_action_form` and `site_ui_item_row` helpers in `mods/common/ux/site_forms.c`.
  2. Refactor `mods/gig/ux/edit.c` to use `site_ui_form_from_desc` for metadata and unified pickers for song rows.
  3. Refactor `mods/gig/ux/detail.c` to streamline inline replace picker rendering and state emission.
  4. Refactor `mods/grp/ux/detail.c` repertoire row rendering with `site_ui_action_form`.
- **Targeted Tests**: All `tests/e2e/gig-*.test.ts` and `tests/e2e/grp-*.test.ts`.

### Stage 6: Full Quality Gates & Documentation Audit
- **Target**: Final quality assurance, zero warnings/debug artifacts, and complete documentation update.
- **Tasks**:
  1. Build check (`make`) with zero errors/warnings.
  2. Boundary checks (`scripts/check-module-boundaries.sh`, `scripts/check-ux-purity.sh`, `scripts/check-wasm-imports.sh`).
  3. Run full test suite (`make test`).
  4. Update documentation in `docs/PICKERS.md`, `docs/SCHEMA.md`, `docs/DESIGN.md`, `docs/ARCHITECTURE.md`.

---

## Acceptance Criteria & Polish Checklist
- [ ] Zero duplicate `form_field_t` arrays in `poem`, `song`, `grp`, `gig`.
- [ ] Zero manual `*_form_values` struct offset unpackers.
- [ ] No `song_picker.c` wrapper file in `gig`.
- [ ] Pickers callable with simple string target (e.g. `"song.items"`).
- [ ] Automatic sibling GET forms for all pickers (No-JS guaranteed).
- [ ] Single-call scoped picker query collection in `gig.c`.
- [ ] Standard Add/Edit CRUD handlers usable with 0 custom C lines for simple modules.
- [ ] All targeted unit and E2E tests passing with zero regressions.
- [ ] `make test` full test suite passing with 100% success.
- [ ] Clean module boundaries and zero compiler warnings.

## Task Refinements & User Feedback Loops
- 2026-08-26: Initial task creation and architectural audit.
- 2026-08-26: Refinement 1: Introduced string-first target picker APIs (`site_ui_picker`, `site_ui_row_replace_picker`) where passing a target string (e.g. `"song.items"` or `"song"`) automatically infers keys, labels, form IDs, URL templates, and parameter names.
- 2026-08-26: Refinement 2: Added auto-CRUD handlers to `index` engine (making simple modules like `poem` and `grp` require zero custom Add/Edit route code) and reusable child row action helpers.
- 2026-08-26: Refinement 3: Enhanced `.pi/extensions/task-journal.ts` to mandate deep upfront planning in turn 1, inject resume context on reboot, and support post-implementation polish iterations cleanly.

## Why this matters
The `hyle` ecosystem was created with the philosophy that data models and components should be declarative: a module defines its data schema, field types, and relationships, and the framework automatically provides querying, filtering, FTS, form generation, pickers, and tables. Currently, consumer modules (especially `gig`, but also `song`, `grp`, and `poem`) maintain redundant form field arrays, manual struct-to-array value packing, handcrafted picker URL templates, manual query-string scanning for active picker scopes, bespoke wrapper structs (such as `song_picker.c`), and duplicate CRUD route handlers. Eliminating this boilerplate makes consumer modules concise, less error-prone, easier to maintain, and truly showcases the power of `hyle`.

## Decisions made
- Keep `hyle` framework-neutral and pure C (no bud/DOM symbols in `external/hyle/src` or `include/hyle`).
- Place bud-specific component automation in `external/hyle/c/libhyle-bud` and `mods/common/ux/site_forms.c`.
- Keep `site_forms.c` pure C and isomorphic (operates on `bud_field_desc_t`, zero `XY_`/`qmap_`/`source_`/`axil_` symbols).
- Use target source string (e.g. `"song.items"` or `"grp.items"`) to auto-infer keys, labels, and fragment endpoints across picker components.
- Enable `index_open` / `source_setup` to store and provide standard Add and Edit GET/POST handlers automatically from the schema descriptor.
- Break the simplification into 6 distinct, self-contained stages so each step can be developed, tested, and verified independently without breaking existing functionality.

## Constraints & Rules
- Isomorphic pure C: UX compiles both natively for SSR and to WASM for browser enhancement.
- No-JS must always work (sibling GET forms, native POST actions, SSR-first).
- Zero `XY_` / `axil_` / `qmap_` / `source_` in UX modules.
- Accents preserved: accent-sensitive search, url-decoding preserved.
- Always run targeted tests during iterations; run `make test` only at the final quality gate.

## Files touched
- `.pi/extensions/task-journal.ts`
- `docs/current/simplify-consumer.md`
- (Stage 1): `mods/common/ux/site_ui.h`, `mods/common/ux/site_forms.c`, `mods/poem/ux/form.c`, `mods/grp/ux/add.c`, `mods/grp/ux/edit.c`, `mods/song/ux/form.c`, `mods/gig/ux/add.c`
- (Stage 2): `external/hyle/c/libhyle-bud/src/picker.c`, `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`, `mods/common/ux/site_ui.h`, `mods/common/ux/site_forms.c`, `mods/gig/ux/song_picker.c` (remove)
- (Stage 3): `mods/index/pick.c`, `mods/index/index.h`, `mods/gig/gig.c`
- (Stage 4): `mods/source/source.h`, `mods/source/source.c`, `mods/index/index.c`, `mods/index/index.h`, `mods/poem/poem.c`, `mods/grp/grp.c`
- (Stage 5): `mods/common/ux/site_forms.c`, `mods/common/ux/site_ui.h`, `mods/gig/ux/edit.c`, `mods/gig/ux/detail.c`, `mods/grp/ux/detail.c`, `mods/gig/gig.c`
- (Stage 6): documentation files (`docs/PICKERS.md`, `docs/SCHEMA.md`, `docs/DESIGN.md`, `docs/ARCHITECTURE.md`)

---

## Remaining work
- [ ] Confirm plan with user
- [ ] Execute Stage 1
- [ ] Execute Stage 2
- [ ] Execute Stage 3
- [ ] Execute Stage 4
- [ ] Execute Stage 5
- [ ] Execute Stage 6

## Open questions / risks
- None. All proposed changes build upon existing `bud_field_desc_t`, `site_ui`, and `libhyle-bud` conventions while preserving No-JS compatibility and WASM isomorphic contracts.

## Next recommended step
1. Proceed with Stage 1 implementation: Declarative Schema-Driven Form Generation.

## Resume prompt
> The in-depth analysis, comprehensive 6-stage specification, and task extension enhancements are fully recorded in docs/current/simplify-consumer.md and .pi/extensions/task-journal.ts. Ready to execute Stage 1.
