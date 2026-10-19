# index — Generic CRUD Routes, List Views, and Picker Options

Provides standardized routing, dataset list serialization, and omni-dropdown option endpoints for all entity modules across the platform.

## Overview

The `index` module connects data schemas (`hyle_schema_desc_t`) to the HTTP interface. It automatically generates and mounts standard CRUD endpoints:
- `GET /{module}/` — Server-rendered list page with sorting, pagination, and filters
- `GET /{module}/add` — Declarative item creation form (via `site_ui_form_from_desc`)
- `POST /{module}/add` — Item creation handler with CSRF and validation
- `GET /{module}/:id` — Item detail view
- `GET /{module}/:id/edit` — Declarative item edit form
- `POST /{module}/:id/edit` — Item update handler
- `POST /{module}/:id/delete` — Item deletion handler
- `GET /_options/{dataset}` — Option endpoint for No-JS/WASM omni-dropdown pickers

## Key APIs (via `index.h`)

### `index_module_init`

One-call initialization helper that sets up dataset storage, registers schemas, mounts list views, and registers CRUD routes:

```c
index_module_def_t def = {
    .name = "artist",
    .display_name = "Artist",
    .schema = artist_fields,
    .field_count = ARTIST_FIELD_COUNT,
    .record_size = sizeof(artist_cache_t),
    .key_field = "id",
    .items_path = "var/artist",
    .list_view = &artist_list_view,
    .handlers = { /* optional custom overrides */ }
};
index_module_init(&def);
```

### `list_fill_state`

Serializes dataset query results and URL query parameters into a framework-neutral `list_state_t` structure. This structure is shared between native C SSR and the browser WASM bundle (`htdocs/list.wasm`).

## Dependencies

- `mods/common/common` — Page rendering, response helpers, storage
- `mods/source/source` — Dataset query engine and persistence
- `mods/auth/auth` — Access control and CSRF validation
- `mods/mpfd/mpfd` — Multipart upload parsing

## Related Docs

- `docs/ARCHITECTURE.md` — Generic routing and module lifecycle.
- `docs/PICKERS.md` — Omni-dropdown picker options endpoint.
- `docs/C-ISOMORPHIC-BUD.md` — Isomorphic list view rendering (`htdocs/list.wasm`).
