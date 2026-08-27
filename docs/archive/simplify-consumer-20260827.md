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
- [x] **3. Feature Implementation**: Implemented Stages 1 through 11 (including declarative forms, smart pickers, generic CRUD, `libhyle-source`, bud purification, auto-meta sync, and declarative lifecycle).
- [x] **4. Build & Run**: Built and ran project with zero build errors and zero warnings.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Full Test Suite**: Executed FULL test suite (`make test`) with zero errors (99/99 passed).

---

## Architecture Clean Separation & Independence
- **`external/bud/` (Pure UI)**: Zero database or storage dependencies. `bud_field_desc_t` defines only the 5 UI state binder fields (`key`, `offset`, `size`, `is_int`, `kind`).
- **`external/hyle/` (Pure Data Engine)**: Zero DOM or JSX dependencies. `hyle_schema_desc_t` in `external/hyle/include/hyle/schema.h` owns canonical data schema, storage attributes, and validation rules.
- **`external/hyle/c/libhyle-bud/` (Bridge Layer)**: The only place where Hyle schemas meet Bud UI components, providing `hyle_bud_state_apply` and `site_ui_form_from_desc`.
- **`external/hyle/c/libhyle-source/` (Storage & Persistence)**: Standalone persistence library with pluggable storage drivers (`hyle_source_store_ops_t`) supporting filesystem (`store_fs`), in-memory (`store_mem`), native qmap DB files, or custom databases.

---

## Detailed In-Depth Analysis & Implemented Solutions

### 1. Form Field Duplication & Manual Form Plumbing
- Added `site_ui_form_from_desc` in `mods/common/ux/site_forms.c` that accepts `hyle_schema_desc_t *` + `struct_ptr`, auto-generating inputs, value extraction from struct offsets, CSRF tokens, action buttons, and sibling GET forms for No-JS picker support.

### 2. String-First Pickers & Omni-Dropdown Automation
- In `libhyle-bud/src/picker.c`, auto-generates `url_tmpl` if NULL or empty, using `source`, `key`, `multi`, and parameter names.
- Added `site_ui_picker(target_source, ...)` and `site_ui_row_replace_picker(target_source, row_idx, ...)` in `site_forms.c`.
- Completely eliminated `mods/gig/ux/song_picker.c`.

### 3. Active Picker Scope Detection in Handlers
- Added `pick_view_collect_auto` and `pick_view_collect_auto_fd` in `mods/index/pick.c` that accept the query string and target source / fields, automatically detect scoped query parameters or replace requests, collect options for the active scope, and return the active index in a single call.

### 4. Boilerplate CRUD Route Handlers in Modules
- Stored `defs` inside `source_def_t` and added `source_get_desc` and `source_get_record_size`.
- Implemented `index_generic_add_get_handler` and `index_generic_edit_get_handler` in `mods/index/index.c` using schema descriptors and `site_ui_form_from_desc`.
- Implemented `pick_view_collect_desc` and `pick_view_collect_desc_fd` in `mods/index/pick.c`.
- Deleted custom form files and route handlers from `poem` (`poem/ux/form.c`) and `grp` (`grp/ux/add.c`, `grp/ux/edit.c`), letting `index_open` manage standard CRUD automatically with 0 lines of custom C code.

### 5. Bespoke View Construction & Action Forms in Detail/Edit Views
- Introduced lightweight building blocks in `site_ui`: `site_ui_item_row` and `site_ui_action_form`.
- Refactored `mods/gig/ux/edit.c` and `detail.c` to use declarative forms, standard action forms, and `site_ui_row_replace_picker`.

### 6. Core Hyle Encapsulation & Data Separation
- Created `external/hyle/include/hyle/picker.h` defining pure framework-neutral types: `hyle_option_t`, `hyle_picker_desc_t`, `hyle_picker_entry_t`, `hyle_picker_view_t`, `hyle_picker_buffer_t`.
- `libhyle-bud` includes `<hyle/picker.h>` and acts strictly as a pure rendering binding.

### 7. Standalone `libhyle-source` Decoupling
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

### 8. Pure `bud` Decoupling & Canonical `hyle_schema_desc_t`
- Purified `bud.h` to define only the 5 UI binder fields (`key`, `offset`, `size`, `is_int`, `kind`) as `bud_field_desc_t`.
- Implemented `bud_state_apply_stride_len` in `external/bud/src/libbud.c` for stride-aware struct unpacking.
- Defined `hyle_schema_desc_t` in `external/hyle/include/hyle/schema.h` with zero `bud` dependencies.
- Provided `hyle_bud_state_apply` in `libhyle-bud` as the bridge for `hyle_schema_desc_t` state unpacking.

### 9. Automatic Metadata Persistence & Handlers Cleanup
- `libhyle-source`'s store driver (`store_fs`) automatically writes all schema fields (`title`, `data.txt`, etc.) on `source_update_item`.
- Removed manual `write_meta_file` plumbing from `mods/index/index.c`.

### 10. Declarative Module Lifecycle (`index_module_init`)
- Introduced `index_module_init(&(index_module_def_t){...})` in `mods/index/index.h` and `mods/index/index.c`.
- Refactored `mods/poem/poem.c` `xy_install()` into a single declarative block.

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

typedef struct hyle_schema_desc source_desc_t;
```

### 2. Pure UI Binder (`external/bud/include/bud/bud.h`)
```c
typedef struct bud_field_desc {
	const char *key;
	size_t offset;
	size_t size;
	int is_int;
	int kind;
} bud_field_desc_t;

void bud_state_apply_stride_len(
        void *state, const void *fields, size_t field_stride, const char *json,
        size_t len);
```

### 3. Declarative Form Builder (`mods/common/ux/site_ui.h`, `site_forms.c`)
```c
bud_node *site_ui_form_from_desc(
        const char *action,
        const char *cancel_href,
        const char *submit_label,
        const hyle_schema_desc_t *desc,
        const void *struct_ptr,
        const char *csrf_token,
        const pick_view_t *pv,
        const char *vstr_val);
```

### 4. Pluggable Storage Driver (`external/hyle/c/libhyle-source/include/hyle-source/store.h`)
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

### 5. Declarative Module Lifecycle (`mods/index/index.h`, `index.c`)
```c
typedef struct {
	const char *name;
	const char *display_name;
	const source_desc_t *schema;
	int field_count;
	size_t record_size;
	const char *key_field;
	const char *items_path;
	unsigned flags;
	const source_list_view_t *list_view;
	standard_item_handlers_t handlers;
} index_module_def_t;

XY_DECL(uint32_t, index_module_init, const index_module_def_t *, def);
```

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
- [x] Canonical `hyle_schema_desc_t` defined in `external/hyle/include/hyle/schema.h`.
- [x] `bud.h` purified to 5 UI fields with zero database dependencies.
- [x] `external/hyle` and `external/bud` have zero cross-includes.
- [x] Redundant manual `write_meta_file` plumbing removed from consumer handlers.
- [x] `index_module_init` declarative module lifecycle implemented.
- [x] All targeted unit and E2E tests passing with zero regressions.
- [x] `make test` full test suite passing with 100% success (99/99 passed).
- [x] Clean module boundaries and zero compiler warnings.

---

## Task Refinements & User Feedback Loops
- 2026-08-26: Refinements 1-5 (string-first pickers, auto-CRUD, task-journal, picker types, libhyle-source).
- 2026-08-27: Refinement 6-7: Complete Framework Decoupling:
  1. Purified `bud.h` into a standalone UI binder with 0 database/storage dependencies.
  2. Defined canonical `hyle_schema_desc_t` in `external/hyle/include/hyle/schema.h` with zero `bud` dependencies.
  3. Built stride-aware `bud_state_apply_stride_len` and `hyle_bud_state_apply`.
  4. Implemented automated metadata persistence in `libhyle-source` (`store_fs`).
  5. Implemented declarative `index_module_init` module lifecycle.
- 2026-08-27: Full test suite verification (99/99 passed).

## Why this matters
The ecosystem architecture is now clean and modular:
1. `external/bud/`: Ultra-lightweight virtual DOM, JSX tree builder, and 5-field UI state unpacker. 0 dependencies.
2. `external/hyle/`: Pure data, schema, query, indexing, and validation engine. 0 DOM dependencies.
3. `external/hyle/c/libhyle-source/`: Standalone storage and persistence engine with pluggable drivers (`store_fs`, `store_mem`, etc.) and JSON state overlays.
4. `external/hyle/c/libhyle-bud/`: The bridge layer providing Bud DOM rendering for Hyle components.
5. `mods/source/`: Thin XY module and HTTP REST router.
6. Consumer modules (`poem`, `song`, `grp`, `gig`): Extremely lightweight declarative modules with zero boilerplate form code, zero bespoke query loops, and unified schemas.

## Decisions made
- `bud` and `hyle` are completely independent (no cross-includes).
- `bud_field_desc_t` has only 5 UI binder fields.
- `hyle_schema_desc_t` owns the canonical data schema.
- Storage is pluggable via `hyle_source_store_ops_t` (`store_fs`, `store_mem`, or custom).
- `index_module_init` collapses module registration into a single declarative call.

## Files touched
- `Makefile`
- `build.mk`
- `docs/current/simplify-consumer.md`
- `docs/PICKERS.md`
- `external/hyle/include/hyle/schema.h` (created)
- `external/hyle/include/hyle/picker.h` (created)
- `external/hyle/include/hyle/hyle.h`
- `external/bud/include/bud/bud.h`
- `external/bud/src/libbud.c`
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
- `mods/common/field_macros.h`
- `mods/common/bud_adapter.h`
- `mods/common/bud_adapter.c`
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
- `mods/poem/fields.h`
- `mods/poem/ux/all.c`
- `mods/poem/ux/form.c` (deleted)
- `mods/grp/grp.c`
- `mods/grp/fields.h`
- `mods/grp/ux/all.c`
- `mods/grp/ux/detail.c`
- `mods/grp/ux/add.c` (deleted)
- `mods/grp/ux/edit.c` (deleted)
- `mods/song/fields.h`
- `mods/song/ux/form.c`
- `mods/song/ux/detail.c`
- `mods/song/song.c`
- `mods/gig/fields.h`
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
1. Review the completed architecture with user.

## Resume prompt
> Task simplify-consumer: All 11 stages completed and verified. bud is completely purified (0 DB dependencies), hyle owns schemas independently, libhyle-source provides pluggable persistence, consumer modules are streamlined, and all 99/99 tests pass 100%.
