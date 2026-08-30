# source — Dataset Registration and Persistence Dispatch

Thin XY module wrapper exposing `libhyle-source` functionality to the rest of the application.

## Overview

`mods/source` acts as the bridge between site modules and the underlying `libhyle-source` persistence engine. It registers datasets, executes full-text searches and multi-reference queries, and guarantees that all data writes update the FTS indexing pipeline.

## Core Invariant

> **All row writes MUST go through `source_update_item` or `source_delete_item`.**
> Directly writing files or mutating qmaps bypasses index invalidation and freezes search.

## Key APIs (via `source.h`)

### Dataset Setup & Queries

```c
/* Register a dataset with schema and list view */
int source_setup(const char *id, const char *key_field, size_t record_size,
                 const char *items_path, const hyle_schema_desc_t *fields,
                 int field_count, unsigned flags,
                 const source_list_view_t *list_view);

/* Execute query with FTS search, multi-ref pre-filtering, and pagination */
unsigned source_query(const char *dataset_id, const char *query_str);
```

### Row CRUD & Persistence

```c
/* Create or update an item */
int source_update_item(int fd, const char *dataset_id,
                       const char *id, unsigned data_handle);

/* Delete an item */
int source_delete_item(int fd, const char *dataset_id, const char *id);

/* Check existence */
int source_item_exists(const char *dataset_id, const char *id);
```

## Storage Engine

Under the hood, `mods/source` uses `libhyle-source` with the filesystem storage driver (`store_fs`). Records are stored under `var/{dataset_id}/{item_id}/` with scalar metadata in single-line files and long-text/binary content in dedicated files.

## Related Docs

- `external/hyle/c/libhyle-source/README.md` — `libhyle-source` library architecture.
- `docs/FILTERS.md` — Query filtering and multi-reference semantics.
- `docs/GOALS.md` — Data-layer invariants.
