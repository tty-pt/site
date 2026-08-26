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

Server-side, modules use unified declarative form builders and string-first picker APIs.

### 1. Declarative Form Integration (Automatic Pickers & Sibling Forms)
Define target schema properties as references in `fields.h`:
```c
MULTI_REF_FIELD_SM(type, song_cache_t, type, 2048, "song.types", "songs", 1, "dropdown", "and")
```
Render the entire form with a single call to `site_ui_form_from_desc`:
```c
bud_node *form = site_ui_form_from_desc(
    action, cancel_href, "Save", song_fields, &meta, csrf_token, &pv, data_val);
```
`site_ui_form_from_desc` automatically extracts values from the struct, renders text inputs, textareas, or pickers based on field types, and automatically attaches sibling GET forms for No-JS compatibility.

### 2. Standalone / Action Pickers (String Target)
To render an action picker for adding an item from a target dataset:
```c
bud_node *picker = site_ui_picker(
    "song.items", post_action, get_action, csrf_token, &pv, 1);
```
Target strings (e.g. `"song.items"`, `"grp.items"`) automatically configure field keys, display labels, form IDs, URL templates, and query parameters with zero boilerplate.

### 3. Inline Row Replacement Pickers
To render an inline picker for replacing row `row_idx` in a table or list:
```c
bud_node *replace_picker = site_ui_row_replace_picker(
    "song.items", row_idx, current_song_id, current_title,
    post_action, back_href, csrf_token, is_active ? &pv : NULL);
```
Row scoping (`pick_q_song_id__N`), hidden `n` inputs, header text, and cancel triggers are handled automatically.

### 4. Automated Query Collection & Scope Resolution in Handlers
In route handlers, use `pick_view_collect_desc_fd` or `pick_view_collect_auto_fd`:
```c
pick_view_t pv;
int active_scope = -1;
pick_view_collect_desc_fd(fd, defs, &pv, &active_scope);
```
This single call automatically detects if a scoped picker (`?replace=N` or `pick_q_<key>__N=`) or top picker is active and collects options accordingly.

## Naming Conventions
- Sibling forms take the ID `pickq-<first_ref_key>` (e.g. `id="pickq-type"`).
- Sibling form inputs take the names `pick_q_<key>` and `pick_page_<key>`.
- Scoped row pickers take names `pick_q_<key>__<scope>` and `pick_page_<key>__<scope>`.
- The picker fragment endpoint routes to `/pick/<dataset>/options`.
