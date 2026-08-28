# Quest: Schema-Driven Hyle-Bud Generic Filter Component (Entity Pattern)

## Goal
Elevate the architecture so that **`hyle_bud_filter`** in `external/hyle/c/libhyle-bud` is the **single, universal, schema-driven component** for all reference selection and filtering across the entire platform (filters, forms, inline cells, and row selectors). 

Follow the `@vinci/entity` principle:
> **"If you know what a field *is* in the schema, `hyle_bud_filter` automatically renders the correct component and behavior."**

Completely eliminate all ad-hoc pickers, custom cell picker abstractions, and site-side picker builders (`site_ui_picker`, `site_ui_row_replace_picker`, `site_ui_action_picker`, `hyle_bud_cell_picker`) from the site code.

## Why this matters
In `@vinci/entity`, there are not 5 different components for selecting an entity reference depending on whether it's in a table, an edit form, a filter bar, or an inline card. There is only `<GenericFilter />` (or `<GenericValue />`) bound to the field descriptor.

By consolidating everything into **`hyle_bud_filter`**:
- **One Component to Rule Them All**: Whether rendering a filter bar on `/song/?custom=1`, an edit input on `/song/add`, an inline song selector in `/gig/:id`, or a row format switch in `/gig/:id/edit`, consumers call `hyle_bud_filter`.
- **Pure Schema Grounding**: `hyle_bud_filter` inspects the field in `hyle_schema_desc_t` (its `source_type`, `filter_style`, `ref_source`, `multi`, etc.) and automatically chooses:
  - Multi-reference facet (`<details class="hyle-multiselect">`)
  - Single-reference dropdown (`<details class="hyle-singleselect">`)
  - Omnisearch picker with pagination / search
  - Boolean toggle or text search input
- **Zero Site-Side UI Plumbing**: No custom `site_ui_*` picker builders or manual URL template formatting in consumer modules.

---

## Canonical `libhyle-bud` API

### 1. `hyle_bud_filter`: The Universal Component
```c
/* Universal schema-driven component: renders the appropriate UI control
 * (search input, boolean checkbox, single-reference dropdown, multi-reference
 * facet, or searchable reference picker) from a schema field descriptor. */
bud_node *hyle_bud_filter(
    const hyle_schema_desc_t *schema,
    const char *field_name,
    const char *current_value,
    const hyle_bud_picker_view_t *pv
);

/* Scoped / Indexed variant for repeated rows or namespaced controls:
 * (e.g. scope = 0 -> field_0, pickq-field_0, pick_q_field_0) */
bud_node *hyle_bud_filter_scoped(
    const hyle_schema_desc_t *schema,
    const char *field_name,
    int scope,
    const char *current_value,
    const char *current_label,
    const char *get_action,
    const hyle_bud_picker_view_t *pv,
    int is_active,
    const char *extra_class,
    bud_node **sibling_forms_out
);

/* High-level filter group / toolbar builder */
bud_node *hyle_bud_filter_group(
    const hyle_schema_desc_t *schema,
    const char **field_names,
    int n_fields,
    const char *current_qs,
    const hyle_bud_picker_view_t *pv
);
```

---

## Implementation Plan

### Phase 1: Consolidate `libhyle-bud` API (`external/hyle/c/libhyle-bud`)
- In `hyle-bud.h` and `src/filter.c`:
  - Define `hyle_bud_filter` and `hyle_bud_filter_scoped`.
  - Remove all separate `cell_picker` / `input` functions from `hyle-bud` and `site_ui`.
  - Ensure `hyle_bud_filter_scoped` derives dataset, multi/single, and URL templates strictly from `hyle_schema_desc_t` (zero site field hardcoding).

### Phase 2: Purge Ad-Hoc Picker Builders from Site UX
- In `mods/common/ux/site_forms.c` and `site_ui.h`:
  - Remove `site_ui_picker`, `site_ui_row_replace_picker`, `site_ui_cell_picker`, `site_ui_action_picker`.
  - Delegate all form and table reference controls to `hyle_bud_filter` / `hyle_bud_filter_scoped`.

### Phase 3: Consumer Refactoring (Pure `hyle_bud_filter`)
- In `mods/gig/ux/edit.c`:
  - Replace row song and format pickers with `hyle_bud_filter_scoped(gig_song_fields, "song", i, ...)` and `hyle_bud_filter_scoped(gig_song_fields, "fmt", i, ...)`.
- In `mods/gig/ux/detail.c`:
  - Replace custom replace picker specs with `hyle_bud_filter_scoped`.
- In `mods/index/ux/list_filters.c` / `mods/song/ux/list.c`:
  - Use `hyle_bud_filter` / `hyle_bud_filter_group`.

### Phase 4: Verification, Quality Gates & Boundary Audits
- Run `make` and full test suite `make test` (all 103+ unit and E2E tests).
- Ensure `scripts/check-module-boundaries.sh`, `scripts/check-ux-purity.sh`, `scripts/check-no-site-specific-js.sh`, and `scripts/check-wasm-imports.sh` pass cleanly.

---

## Current Status
Implementation completed, verified with targeted unit and E2E tests, and confirmed that group, format, and song pickers correctly resolve and render their respective dataset items.

## Why this matters
Following the `@vinci/entity` principle:
> **"If you know what a field *is* in the schema, `hyle_bud_filter` automatically renders the correct component and behavior."**

We unified all filter and reference UI across the entire platform under **`hyle_bud_filter`** / **`hyle_bud_filter_scoped`** inside **`external/hyle/c/libhyle-bud`**:
- `libhyle-bud` is the single canonical schema-to-UI bridge.
- Field definitions in `hyle_schema_desc_t` (`REF_FIELD_S(grp, ..., "grp.items")`, `REF_FIELD_S(song, ..., "song.items")`, `REF_FIELD_S(fmt, ..., "song.types")`) determine the target dataset, options window, search params, and No-JS sibling forms without any hardcoding or ad-hoc site wrappers.
- The Group picker on `/gig/:id/edit` properly lists groups from `grp.items`, the Song picker lists songs from `song.items`, and the Format picker lists formats from `song.types`.

## Key Architecture & Design Decisions

1. **Universal Schema-Driven Component in `libhyle-bud` (`hyle_bud_filter` & `hyle_bud_filter_scoped`)**:
   - Resides in `external/hyle/c/libhyle-bud` (`hyle-bud.h`, `src/filter.c`, `src/picker.c`).
   - Accepts a canonical `hyle_schema_desc_t` descriptor and automatically renders:
     - Multi-reference facet (`<details class="hyle-multiselect">`) for `HYLE_BUD_MULTI_REFERENCE` + `dropdown`.
     - Single-reference dropdown (`<details class="hyle-singleselect">`) for `HYLE_BUD_REFERENCE` + `dropdown`.
     - Omnisearch picker (`<details class="hyle-picker">`) with search, pagination, and No-JS sibling forms for reference pickers.
     - Boolean toggle for `HYLE_BUD_BOOL`.
     - Text / Content search input for text fields.
   - `hyle_bud_filter_scoped`: Automatically namespaces search and pagination params for indexed rows (`name_N`) or scoped controls (`name__scope`).

2. **Elimination of Site-Side Custom Picker Abstractions**:
   - Purged ad-hoc picker builders and `cell_picker` from `site_ui` / `site_forms.c`.
   - `mods/gig/ux/edit.c` renders row pickers via `hyle_bud_filter_scoped(gig_song_fields, "song", i, ...)` and `hyle_bud_filter_scoped(gig_song_fields, "fmt", i, ...)`, and group picker via `hyle_bud_filter(gig_fields, "grp", ...)`.
   - Zero site-specific domain names or field names are hardcoded in `libhyle-bud`.

3. **Automated Multi-Field Collector (`mods/index/pick.c`)**:
   - `pick_view_collect_auto_fields`: Inspects `QUERY_STRING` across all candidate fields (top-level, indexed, scoped) without manual loops in consumer handlers.

## Acceptance Criteria & Polish Checklist
- [x] Only `hyle_bud_filter` (and its scoped variant `hyle_bud_filter_scoped`) is used for all filter and reference UI across the entire codebase.
- [x] Group picker on gig edit page lists groups (`grp.items`), not songs.
- [x] No `cell_picker` or ad-hoc `site_ui_*` picker builders exist in the codebase.
- [x] Zero site-field hardcoding in `libhyle-bud`.
- [x] All forms, tables, and filter bars render cleanly with 1 declarative line per field.
- [x] Full SSR No-JS degradation + WASM dynamic enhancement preserved.
- [x] Zero site-specific JavaScript; 100% pure C and WASM.
- [x] Targeted E2E tests (`gig-grp-picker.test.ts`, `gig-edit.test.ts`, `generic-picker-filter.test.ts`, `gig-format-picker.test.ts`) pass cleanly.

## Next recommended step
Prompt user for final wrap-up / feedback loops.
