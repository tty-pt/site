# Task: simplify-consumer

## Goal
hyle was designed for the purpose of minimizing consumer-side code. That means the site modules code should be simple. The source module is built like an interface so that hyle can be fed with the database information. But in terms of components for forms, tables and such, it shouldn't need a lot of specific code. For example the gig module is more complex than it needs to be. It calls upon a custom picker, but feeds it many things manually which could be automatic. The first step is doing an in-depth analysis and developing a multi-stage plan. Each phase of the plan should be self-contained, as if it were a single task.

## Original request
> Goal: hyle was designed for the purpose of minimizing consumer-side code. That means the site modules code should be simple. the source module is built like an interface so that hyle can be fed with the database information. But in terms of components for forms, tables and such, it shouldn't need a lot of specific code. For example the gig module is more complex than it needs to be. It calls upon a custom picker, but feeds it many things manually which could be automatic. The first step is doing an in-depth analysis. And develop a multi-stage plan. Each phase of the plan should be self-contained, as if it were a single task.

## Current Status
- [x] not started · in progress · blocked · **done**

## Build & Run Commands
> Discovered BEFORE modifying feature code.
- Build: `make` (builds stoma, hyle, bud, hyle-bud, hyle-source, axil, qmap, xylem, mods, clients, boundary checks)
- Run: `make watch` or `./start.sh` (axil server on :8080)
- Targeted Tests (run during active development):
  - Page smoke tests: `sh tests/pages/10-pages-render.sh`
  - E2E targeted: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<test-file>.test.ts`
  - Unit tests: `(cd mods/<mod> && ./test.sh)`
- Full Test Suite (final quality gate): `make test` (PASSED 100% - 99/99 E2E + unit + matrix + pages)

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build (`make`), run (`./start.sh` / `make watch`), and test (`tests/pages/`, `tests/e2e/`, `make test`).
- [x] **2. Write Tests First**: Ran and developed targeted test(s) for each phase BEFORE editing feature code.
- [x] **3. Feature Implementation**: Implemented Stages 1 through 8 (including declarative forms, smart pickers, generic CRUD, `libhyle-source`, and unified schema descriptors).
- [x] **4. Build & Run**: Built and ran project with zero build errors and zero warnings.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Full Test Suite**: Executed FULL test suite (`make test`) with zero errors (99/99 passed).

---

## Detailed In-Depth Analysis & Implemented Solutions

### 1. Form Field Duplication & Manual Form Plumbing
- **Initial state**:
  - Every module declared its schema once in `fields.h` as `bud_field_desc_t` (`song_fields`, `gig_fields`, `grp_fields`, `poem_fields`), containing names, offsets, types, targets, and hints.
  - In `ux/form.c`, `ux/add.c`, and `ux/edit.c`, modules declared duplicate `static const form_field_t ff[] = ...` or `song_ff[]` or `sb_grp_ff[]`, copying field names, labels, and target datasets.
  - Modules wrote manual value unpacking helpers (e.g. `song_form_values` in `song/ux/form.c` iterating struct offsets into `const char *vals[]`).
  - Modules manually wrapped `<form>` tags, action URLs, CSRF inputs, submit buttons, and sibling GET forms.
- **Implemented Solution**:
  - Added `site_ui_form_from_desc` in `mods/common/ux/site_forms.c` (and `site_ui.h`) that accepts `bud_field_desc_t` directly along with a struct pointer (or NULL for add forms). It inspects field kinds, types, and `target_source` strings, auto-generating inputs, value extraction from struct offsets, CSRF tokens, action buttons, and sibling GET forms for No-JS picker support.

### 2. String-First Pickers & Omni-Dropdown Automation
- **Initial state**:
  - `hyle_bud_picker_field` required callers to supply a handcrafted `url_tmpl`.
  - Standalone/action pickers required filling a 15-field spec struct `site_ui_action_picker_spec_t`.
  - `mods/gig/ux/song_picker.c` was created as an ad-hoc wrapper to construct URL templates, hidden inputs, and descriptors manually.
- **Implemented Solution**:
  - In `libhyle-bud/src/picker.c`, auto-generates `url_tmpl` if NULL or empty, using `source`, `key`, `multi`, and parameter names.
  - Added `site_ui_picker(target_source, ...)` and `site_ui_row_replace_picker(target_source, row_idx, ...)` in `site_forms.c`:
    - Simply passing a target dataset string (e.g. `"song.items"` or `"grp.items"`) automatically configures default key (`song_id`, `grp`), display label (`"Song"`, `"Group"`), form ID (`pickq-<key>`), URL template, and query params.
    - Inline row replacement handles scoped parameters (`pick_q_song_id__N`), hidden `n` input, cancel button, and active state automatically.
  - Completely eliminated `mods/gig/ux/song_picker.c`.

### 3. Active Picker Scope Detection in Handlers
- **Initial state**:
  - In `mods/gig/gig.c`, `gig_detail_auth` manually looped through all songs in the gig, checked `QUERY_STRING` with `strstr` for `pick_q_song_id__%d` and `pick_page_song_id__%d` and `replace=%d`, set `sb_app_state.active_row_pick`, and called `sb_load_song_picks`.
- **Implemented Solution**:
  - Added `pick_view_collect_auto` and `pick_view_collect_auto_fd` in `mods/index/pick.c` (and `index.h`) that accept the query string and target source / fields, automatically detect scoped query parameters or replace requests, collect options for the active scope, and return the active index in a single call.

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

### 6. Core Hyle Encapsulation & Data Separation
- **Implemented Solution**:
  - Created `external/hyle/include/hyle/picker.h` defining pure framework-neutral types: `hyle_option_t`, `hyle_picker_desc_t`, `hyle_picker_entry_t`, `hyle_picker_view_t`, `hyle_picker_buffer_t`.
  - `libhyle-bud` includes `<hyle/picker.h>` and acts strictly as a pure rendering binding.

### 7. Standalone `libhyle-source` Decoupling
- **Initial state**:
  - `mods/source/source.c` bundled pure data logic (metadata serialization, inverse ref synchronization, pluggable storage drivers, FTS invalidation) with XY runtime macros and HTTP request endpoints.
- **Implemented Solution**:
  - Created `external/hyle/c/libhyle-source/` containing:
    - `include/hyle-source/hyle_source.h`: Pure C API for dataset registration, queries, CRUD, validation, and JSON state overlays.
    - `include/hyle-source/store.h`: Pluggable storage driver interface (`hyle_source_store_ops_t` with `scan`, `load`, `put`, `put_field`, `del`).
    - `src/store_fs.c` & `src/store_mem.c`: Filesystem and in-memory store implementations.
    - `src/meta.c`: Metadata file serialization and parsing.
    - `src/dsv.c`: Ordered tabular DSV load and save.
    - `src/json.c`: State JSON builders and overlays.
    - `src/engine.c`: Schema definitions, CRUD, reference integrity, and FTS cache updates.
  - Refactored `mods/source/source.c` into a thin XY adapter forwarding calls to `libhyle-source`.
  - Updated root `Makefile` to build `hyle-source` library target.

### 8. Unified Schema Descriptors (`hyle_schema_desc_t`)
- **Initial state**:
  - `source_desc_t` and `bud_field_desc_t` were defined separately in `source.h` and `bud.h`.
  - Consumer modules and adapters had to cast `(const source_desc_t *)` or `(const bud_field_desc_t *)` constantly.
- **Implemented Solution**:
  - Created `external/hyle/include/hyle/schema.h` defining canonical `hyle_schema_desc_t`.
  - Aliased `bud_field_desc_t`, `hyle_source_desc_t`, and `source_desc_t` directly to `hyle_schema_desc_t`.
  - Cleaned up all redundant typecasts across `song.c`, `gig.c`, `poem.c`, `grp.c`, and `index.c`.

---

## Complete API Reference & Signatures

### 1. Canonical Schema Descriptor (`external/hyle/include/hyle/schema.h`)
```c
typedef struct hyle_schema_desc {
	const char *key;
	size_t offset;
	size_t size;
	int is_int;
	int kind; /* 0=include, 1=exclude, 2=virtual/ref-display */
	int qm_type;
	int source_type;
	int writable;
	int required;
	size_t min_length;
	const char *ref_source;
	const char *ref_inverse;
	int in_meta;
	const char *file;
	const char *filter_style;
	const char *filter_mode;
} hyle_schema_desc_t;

/* Type aliases across framework layers */
typedef hyle_schema_desc_t bud_field_desc_t;
typedef hyle_schema_desc_t source_desc_t;
typedef hyle_schema_desc_t hyle_source_desc_t;
```

### 2. Declarative Form Builder (`mods/common/ux/site_ui.h`, `site_forms.c`)
```c
bud_node *site_ui_form_from_desc(
        const char *action,
        const char *cancel_href,
        const char *submit_label,
        const bud_field_desc_t *desc,
        const void *struct_ptr,
        const char *csrf_token,
        const pick_view_t *pv,
        const char *vstr_val);
```

### 3. String-First Pickers (`mods/common/ux/site_ui.h`, `site_forms.c`)
```c
bud_node *site_ui_picker(
        const char *target,
        const char *post_action,
        const char *get_action,
        const char *csrf_token,
        const pick_view_t *pv,
        int auto_submit);

bud_node *site_ui_row_replace_picker(
        const char *target,
        int row_idx,
        const char *cur_id,
        const char *cur_title,
        const char *post_action,
        const char *back_href,
        const char *csrf_token,
        const pick_view_t *pv);
```

### 4. Automated Scoped Query Collection (`mods/index/index.h`, `pick.c`)
```c
XY_DECL(int, pick_view_collect_auto,
	char *body,
	const form_field_t *fields,
	const char **vals_in,
	const char **vals_out,
	pick_view_t *pv,
	int *active_scope_out);

XY_DECL(int, pick_view_collect_auto_fd,
	int, fd,
	const form_field_t *fields,
	const char **vals_in,
	const char **vals_out,
	pick_view_t *pv,
	int *active_scope_out);

XY_DECL(int, pick_view_collect_desc,
	const char *qs,
	const source_desc_t *defs,
	pick_view_t *pv,
	int *active_scope_out);

XY_DECL(int, pick_view_collect_desc_fd,
	int, fd,
	const source_desc_t *defs,
	pick_view_t *pv,
	int *active_scope_out);
```

### 5. Pluggable Storage Driver (`external/hyle/c/libhyle-source/include/hyle-source/store.h`)
```c
typedef struct {
	int (*scan)(hyle_source_store_t *store, const struct hyle_source_def_s *def);
	int (*load)(hyle_source_store_t *store, const struct hyle_source_def_s *def,
	            const char *id, unsigned *row_out);
	int (*put)(hyle_source_store_t *store, const struct hyle_source_def_s *def,
	           const char *id, unsigned data_handle);
	int (*put_field)(hyle_source_store_t *store,
	                 const struct hyle_source_def_s *def, const char *id,
	                 const char *field, const char *value);
	int (*del)(hyle_source_store_t *store, const struct hyle_source_def_s *def,
	           const char *id);
} hyle_source_store_ops_t;

struct hyle_source_store_s {
	const hyle_source_store_ops_t *ops;
	void *user;
};

const hyle_source_store_ops_t *hyle_source_store_fs_ops(void);
hyle_source_store_t hyle_source_store_fs(const char *items_path);

const hyle_source_store_ops_t *hyle_source_store_mem_ops(void);
hyle_source_store_t hyle_source_store_mem(void);
```

### 6. Standalone Source Engine API (`external/hyle/c/libhyle-source/include/hyle-source/hyle_source.h`)
```c
hyle_source_def_t *hyle_source_find(const char *dataset_id);
int hyle_source_item_exists(const char *dataset_id, const char *item_id);
int hyle_source_register_def(const hyle_source_def_t *def);
uint32_t hyle_source_setup(const char *source_id, const char *key_field,
                           size_t record_size, const char *items_path,
                           const hyle_source_desc_t *defs, int field_count,
                           unsigned flags, const hyle_source_list_view_t *list_view);
int hyle_source_validate_row(const hyle_source_def_t *def, unsigned data_handle, char **json_errors_out);
int hyle_source_update_item(int fd, const char *dataset_id, const char *id, unsigned data_handle);
int hyle_source_delete_item(int fd, const hyle_source_def_t *def, const char *item_id);
unsigned hyle_source_query_dataset(const char *dataset_id, const char *query_str);
int hyle_source_build_state_json(const char *dataset_id, const char *item_id,
                                 const hyle_source_state_field_t *specs, json_object **out);
int hyle_source_state_overlay(json_object *jo, const hyle_source_state_kv_t *kvs);
int hyle_source_overlay_from_desc(json_object *jo, const void *state,
                                  const hyle_source_desc_t *fields, int int_kind, int str_kind);
```

---

## Multi-Stage Execution Summary

- **Stage 1: Declarative Schema-Driven Form Generation**: Implemented `site_ui_form_from_desc` across `poem`, `grp`, `song`, and `gig`.
- **Stage 2: String-Based Target & Smart Default Pickers**: Automated URL templates in `picker.c`, implemented `site_ui_picker` & `site_ui_row_replace_picker`, deleted `song_picker.c`.
- **Stage 3: Automated Query Collection & Scoped Resolution**: Implemented `pick_view_collect_auto` & `pick_view_collect_auto_fd`, simplified `gig.c` query handling.
- **Stage 4: Standard Generic CRUD Handlers in Index**: Implemented `index_generic_add_get_handler` & `index_generic_edit_get_handler` using `source_get_desc`, deleted `poem/ux/form.c`, `grp/ux/add.c`, `grp/ux/edit.c`.
- **Stage 5: Streamlined Child Rows & Action Forms in Gig & Grp**: Implemented `site_ui_action_form` & `site_ui_item_row`, refactored `detail.c` in `gig` and `grp`.
- **Stage 6: Core Hyle Encapsulation**: Extracted framework-neutral picker types to `external/hyle/include/hyle/picker.h`.
- **Stage 7: Standalone `libhyle-source` Decoupling**: Created `external/hyle/c/libhyle-source/` standalone storage and persistence library with pluggable stores (`store_fs`, `store_mem`). Refactored `mods/source` to a thin XY/HTTP transport wrapper.
- **Stage 8: Unified Schema Descriptors**: Defined canonical `hyle_schema_desc_t` in `external/hyle/include/hyle/schema.h`. Unified `bud_field_desc_t` and `source_desc_t`, eliminating redundant casts across consumer modules.

---

## Acceptance Criteria & Quality Checklist
- [x] Zero duplicate `form_field_t` arrays in `poem`, `song`, `grp`, `gig`.
- [x] Zero manual `*_form_values` struct offset unpackers.
- [x] No `song_picker.c` wrapper file in `gig` (deleted).
- [x] Pickers callable with simple string target (e.g. `"song.items"`).
- [x] Automatic sibling GET forms for all pickers (No-JS guaranteed).
- [x] Single-call scoped picker query collection in `gig.c`.
- [x] Standard Add/Edit CRUD handlers usable with 0 custom C lines for simple modules (`poem`, `grp`).
- [x] Framework-neutral picker types encapsulated in core `hyle` (`include/hyle/picker.h`).
- [x] `libhyle-source` standalone persistence & schema library created at `external/hyle/c/libhyle-source/`.
- [x] `mods/source` refactored to thin XY/HTTP transport layer over `libhyle-source`.
- [x] Pluggable `source_store_ops_t` drivers (`store_fs`, `store_mem`) implemented and tested.
- [x] Unified `hyle_schema_desc_t` defined in `external/hyle/include/hyle/schema.h`.
- [x] `bud_field_desc_t` and `source_desc_t` aliased to `hyle_schema_desc_t`.
- [x] Redundant casts eliminated across consumer modules.
- [x] All targeted unit and E2E tests passing with zero regressions.
- [x] `make test` full test suite passing with 100% success (99/99 passed).
- [x] Clean module boundaries and zero compiler warnings.

---

## Task Refinements & User Feedback Loops
- 2026-08-26: Initial task creation and architectural audit.
- 2026-08-26: Refinement 1: Introduced string-first target picker APIs (`site_ui_picker`, `site_ui_row_replace_picker`).
- 2026-08-26: Refinement 2: Added auto-CRUD handlers to `index` engine and reusable child row action helpers.
- 2026-08-26: Refinement 3: Enhanced `.pi/extensions/task-journal.ts` for deep planning and clean reboot resumes.
- 2026-08-26: Refinement 4: Encapsulation Upgrade — extracted framework-neutral picker & option types into core `hyle` (`include/hyle/picker.h`), refactored `libhyle-bud` to be a pure renderer.
- 2026-08-26: Refinement 5: Decoupled `source` engine into standalone `libhyle-source` (`external/hyle/c/libhyle-source/`) with pluggable storage backends (`store_fs`, `store_mem`), turning `mods/source` into a thin XY/HTTP wrapper.
- 2026-08-26: Refinement 6: Step 3 — Unified Schema Descriptors (`hyle_schema_desc_t`) in core `hyle` (`include/hyle/schema.h`), eliminating duplicate definitions and typecasts across `bud.h`, `source.h`, and consumer modules.
- 2026-08-26: Full test suite verification (99/99 passed).

## Why this matters
The `hyle` ecosystem is now cleanly layered:
1. `external/hyle/`: Core data structures, queries, purify validation, picker data types, and canonical `hyle_schema_desc_t`.
2. `external/hyle/c/libhyle-source/`: Standalone storage and persistence engine with pluggable drivers (`store_fs`, `store_mem`, etc.) and JSON state overlays.
3. `external/hyle/c/libhyle-bud/`: Framework-neutral Bud DOM component rendering (tables, pickers, filters).
4. `mods/source/`: Thin XY module and HTTP REST router.
5. Consumer modules (`poem`, `song`, `grp`, `gig`): Extremely lightweight declarative modules with zero boilerplate form code, zero bespoke query loops, and unified schemas.

## Decisions made
- Kept `hyle` core pure C without DOM or HTTP dependencies.
- Placed canonical `hyle_schema_desc_t` in `external/hyle/include/hyle/schema.h`.
- Structured `libhyle-source` in `external/hyle/c/libhyle-source/` alongside `libhyle-bud`.
- Provided pluggable storage driver interface (`hyle_source_store_ops_t`) with `store_fs` and `store_mem`.
- Cleanly separated validation logic in `libhyle-source` from HTTP 422 response encoding in `mods/source`.

## Files touched
- `Makefile`
- `build.mk`
- `docs/current/simplify-consumer.md`
- `docs/PICKERS.md`
- `external/hyle/include/hyle/schema.h` (created)
- `external/hyle/include/hyle/picker.h` (created)
- `external/hyle/include/hyle/hyle.h`
- `external/bud/include/bud/bud.h`
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `external/hyle/c/libhyle-bud/Makefile`
- `external/hyle/c/libhyle-bud/objects-set.mk`
- `external/hyle/c/libhyle-bud/hyle-bud-wasm.mk`
- `external/hyle/c/libhyle-source/include/hyle-source/hyle_source.h` (created)
- `external/hyle/c/libhyle-source/include/hyle-source/store.h` (created)
- `external/hyle/c/libhyle-source/src/libhyle-source.c` (created)
- `external/hyle/c/libhyle-source/src/source_utils.h` (created)
- `external/hyle/c/libhyle-source/src/source_utils.c` (created)
- `external/hyle/c/libhyle-source/src/store_fs.c` (created)
- `external/hyle/c/libhyle-source/src/store_mem.c` (created)
- `external/hyle/c/libhyle-source/src/meta.c` (created)
- `external/hyle/c/libhyle-source/src/dsv.c` (created)
- `external/hyle/c/libhyle-source/src/json.c` (created)
- `external/hyle/c/libhyle-source/src/engine.c` (created)
- `external/hyle/c/libhyle-source/Makefile` (created)
- `external/hyle/c/libhyle-source/objects-set.mk` (created)
- `mods/common/ux/site_ui.h`
- `mods/common/ux/site_forms.c`
- `mods/source/source.h`
- `mods/source/source.c`
- `mods/source/Makefile`
- `mods/source/dsv.c` (deleted, migrated to `libhyle-source`)
- `mods/source/source_store_fs.c` (deleted, migrated to `libhyle-source`)
- `mods/source/source_internal.h` (deleted)
- `mods/source/source_store.h` (deleted)
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
- `tests/unit/dsv_legacy_test.c`
- `tests/unit/run-dsv-legacy.sh`
- `scripts/check-ux-purity.sh`

---

## Remaining work
- None. All tasks, refinements, encapsulation upgrades, and quality gates completed.

## Open questions / risks
- None.

## Next recommended step
1. Review the full architecture with user.

## Resume prompt
> Task simplify-consumer: All 8 stages completed and verified. libhyle-source standalone library created, schema descriptors unified as hyle_schema_desc_t, consumer boilerplate eliminated, and all 99/99 tests passing 100%.
