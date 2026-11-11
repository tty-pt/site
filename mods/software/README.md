# software — Software Catalog & Documentation Viewer

Declarative software registry and Markdown documentation viewer built on `mods/index`, `libhyle`, and `libhyle-bud`.

## Overview

The `software` module manages software project entries stored in `var/software/{id}/`, providing searchable/filterable catalog listings (`/software`) and rich detail views (`/software/{id}`) that render each project's `README.md` alongside its structured metadata (`title`, `category`, `version`, `summary`, `repo_url`, `license`, and `owner`).

## Routes & Endpoints

All standard CRUD and listing routes are registered via `index_module_init`:

| Method | Route | Description |
|--------|-------|-------------|
| `GET` | `/software` | Filterable, sortable software catalog listing |
| `GET` | `/software/{id}` | Software detail view with rendered `README.md` body |
| `GET` / `POST` | `/software/add` | Add a new software entry (authenticated) |
| `GET` / `POST` | `/software/{id}/edit` | Edit software metadata and `README.md` (owner) |
| `POST` | `/software/{id}/delete` | Delete a software entry (owner) |

## Storage Structure

Entries live under `var/software/{id}/`:

```text
var/software/{id}/
  ├── meta.dsv       # Structured fields (title, category, version, summary, repo_url, license)
  ├── README.md      # Markdown project documentation rendered on the detail page
  └── owner          # Maintainer username
```

## Architecture & Files

- `software.c` — Module registration (`xy_install`), `index_module_init` wiring, and `software_detail_handler`.
- `fields.h` — Canonical `hyle_schema_desc_t` (`software_fields`) defining schema fields and filter hints.
- `markdown.c` / `markdown.h` — Pure-C Markdown-to-HTML renderer used for `README.md` display.
- `dict.h` — Internationalization dictionary entries registered via `i18n_register_dict`.
- `ux/all.c` — Isomorphic `libbud` detail view fragment (`software_render_detail_body`).

## Dependencies

- `mods/index/index` — Declarative module lifecycle, list/filter table, and CRUD form handlers.
- `mods/common/common`, `mods/source/source`, `mods/auth/auth`, `mods/i18n/i18n`.

## Building & Testing

```sh
make -C mods/software
make test-mod MOD=software
```
