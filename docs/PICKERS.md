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

## Usage & API

Server-side, modules use a unified `source_list_view_t` and `pick_view_collect` API to coordinate picker instances.

1.  **Define a target schema property as a reference** in the model `fields.h`:
    ```c
    MULTI_REF_FIELD_SM(type, song_cache_t, type, 2048, "song.types", "songs", 1, "dropdown", "and")
    ```

2.  **In the route handler for the form**, process incoming GET query parameters using `pick_view_collect` prior to rendering the UI:
    ```c
    pick_view_t pv;
    pick_view_collect(&pv, req_qs, (const source_desc_t *)song_fields,
            vals /* current model values */);
    ```

3.  **Render the form fields** via `site_ui_form_fields_ex`, which inspects the schema and the `pick_view_t` structure. If the target dataset exceeds `FF_PICKER_THRESHOLD`, `hyle_bud_picker_field` is dynamically invoked:
    ```c
    bud_node *form = site_ui_form_fields_ex(song_fields, vals, csrf_token, &pv);
    ```

4.  **Render the sibling GET form** directly adjacent to the main POST form in the DOM:
    ```c
    bud_node *sibling = site_ui_sibling_get_form(action, song_fields, vals, &pv);
    ```

## Naming Conventions
- Sibling forms take the ID `pickq-<first_ref_key>` (e.g. `id="pickq-type"`).
- Sibling form inputs take the names `pick_q_<key>` and `pick_page_<key>`.
- The picker fragment endpoint routes to `/pick/<dataset>/options`.
