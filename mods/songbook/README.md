# Songbook Module

Manages songbooks - collections of songs with individual transpose settings, organized by format categories defined in their parent choir.

## Purpose

The songbook module provides:
- Create songbooks within a choir
- Add/remove/reorder songs with per-song transpose values
- View mode: display all songs with transposed chords
- Edit mode: manage songs, transpose values, and formats

## Architecture

- **Backend:** `songbook.c` - C module with axil HTTP handlers
- **Frontend:** `ux/` directory handles HTML rendering via `libbud` (e.g. `detail.c`, `edit.c`, `add.c`)
- **Storage:** Filesystem using `hyle_source_ordered` and `data.txt`
- **Dependencies:** auth, common, mpfd, choir

## Storage

### Directory Structure
```
items/songbook/items/
├── index.db              # qmap database: songbook_id → title
└── {songbook_id}/
    ├── .owner            # Plain text username
    └── data.txt          # Songbook list (DSV format via hyle_source_ordered)
```

### Data Format (`data.txt`)

The collection list is tracked by the `hyle_source_ordered` API, meaning the order of lines matches the order in the songbook:
```
{song_id}:{transpose}:{format}
{song_id}:{transpose}:{format}
...
```

## Troubleshooting

**Problem:** "Failed to open data handles"
- **Cause:** Underlying dataset or source files were missing, or the module lacks `-lhyle` in its `Makefile`.

**Problem:** Empty song list on edit page
- **Cause:** If `data.txt` fails to load, verify its schema matches the struct format and the `hyle_source_ordered` definition.

## Known Issues

None. All legacy synchronization issues related to the outdated `choir.repertoire` system were resolved with the migration to `hyle_source_ordered`.
