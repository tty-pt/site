# Pickers (Omni-Dropdowns)

The `site` utilizes a unified, JS-degradable "omni-dropdown" architecture for resolving references (e.g. `type`, `grp`) within forms. This ensures forms remain strictly functional without JavaScript, while progressively enhancing into infinitely-scrollable, debounced searchable dropdowns.

## Architecture

At its core, a picker consists of two synchronized components on any given page:
1. **Main Form Integration**: Standard `<input type="checkbox">` or `<input type="radio">` tags remain physically nested inside the primary POST form (e.g., `#main-form`). The main form thus submits the selected reference IDs exactly as a raw HTML form would.
2. **Sibling GET Form**: A separate `<form method="GET" class="pick-sibling-form">` generated alongside the main form. This GET form handles pagination and search querying for the picker itself, avoiding accidental submissions or dirtying of the primary POST form.

### JavaScript OFF
- The user clicks the pagination controls or submits a search query in the `<details>` picker container.
- These controls are intrinsically tied to the sibling GET form.
- Submitting the GET form performs a full page reload, injecting the search constraints (`pick_q_<key>` and `pick_page_<key>`) into the URL.
- The server (`pick_view_collect`) intercepts these query parameters, rendering the updated search results.
- **Limitation**: Since the main form fields are not submitted via the sibling GET form, unsaved text in the main form (like `title`) is intentionally lost during a JS-off picker search/pagination reload. This is a documented, acceptable progressive enhancement degradation.

### JavaScript ON
- A `fetch()` intercepts typing/clicking within the picker container.
- `hyle-fragments.js` retrieves the raw picker slots from `/pick/<dataset>/options?q=...` and hot-swaps the `data-hyle-slot="panel"` DOM slice without reloading the page.
- Checkbox selections are summarized via `syncSummary()` directly inside the `data-hyle-slot="values"`.
- The `IntersectionObserver` auto-appends paginated chunks to the `data-hyle-slot="rows"`.

## Universal Component: `hyle_bud_filter`

All reference selection, omni-dropdowns, facets, and filter controls across the entire platform **MUST** use the universal **`hyle_bud_filter`** or **`hyle_bud_filter_scoped`** component (`<hyle-bud/hyle-bud.h>`).

There are no separate ad-hoc picker widgets. `hyle_bud_filter` inspects the schema field descriptor (`hyle_schema_desc_t`) and renders the exact control needed:
- Single-reference dropdown / omnisearch picker (e.g. `grp`, `song`, `format`)
- Multi-reference facet / multiselect (e.g. `type`)
- Boolean switch or text filter

### API

```c
/* Standard field filter / picker: */
bud_node *hyle_bud_filter(
    const hyle_schema_desc_t *schema,
    const char *field_name,
    const char *current_value,
    const hyle_bud_picker_view_t *pv
);

/* Scoped / Indexed field filter (for repeated row cells, e.g. row 0 -> song_0, pickq-song_0): */
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
```

## Usage in Forms & Views

### 1. Declarative Form Integration
Define target schema properties as references in `fields.h`:
```c
FIELD_ARRAY(FIELD_REF, type, song_cache_t, "song.types",
            .ref_inverse = "songs", .filter_style = "dropdown",
            .filter_mode = "and", .allow_add = 1, .in_meta = 1)
FIELD_REF(grp, gig_cache_t, "grp.items",
          .ref_inverse = "gigs", .filter_style = "dropdown",
          .allow_add = 1, .in_meta = 1)
```
Render forms declaratively with `site_ui_form_from_desc` (which delegates directly to `hyle_bud_filter`):
```c
bud_node *form = site_ui_form_from_desc(
    action, cancel_href, "Save", song_fields, &meta, csrf_token, &pv, data_val);
```

### 2. Standalone Reference Selection (e.g. Add Form / Edit Form Header)
```c
bud_node *grp_field = hyle_bud_filter(gig_fields, "grp", grp_id, pv);
```

### 3. Inline / Table Row Reference Selection
```c
bud_node *song_picker = hyle_bud_filter_scoped(
    gig_song_fields, "song", row_idx, current_song_id, current_title,
    action, is_active ? pv : NULL, is_active,
    "gig-song-title-picker", &row_sibs);
```

### 4. Automated Query Collection in Handlers
In route handlers, use `pick_view_collect_desc_fd`, `pick_view_collect_fd`, or `pick_view_collect_auto_fields`:
```c
pick_view_t pv;
pick_view_collect_fd(fd, field_defs, vals_in, vals_out, &pv);
```
Multiple pickers collected within the same HTTP request automatically receive isolated thread-local buffer slots in `pick.c` without cross-clobbering.

## Naming Conventions
- Sibling forms take the ID `pickq-<first_ref_key>` (e.g. `id="pickq-type"`).
- Sibling form inputs take the names `pick_q_<key>` and `pick_page_<key>`.
- Scoped row pickers take names `pick_q_<key>__<scope>` and `pick_page_<key>__<scope>`.
- The picker fragment endpoint routes to `/pick/<dataset>/options`.
