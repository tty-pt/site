# Gig Module

Manages gigs - collections of songs with individual transpose settings, organized by format categories defined in their parent grp.

## Purpose

The gig module provides:
- Create gigs within a grp
- Add/remove/reorder songs with per-song transpose values
- View mode: display all songs with transposed chords
- Edit mode: manage songs, transpose values, and formats

## Architecture

- **Backend:** `gig.c` - C module with axil HTTP handlers
- **Frontend & UI:** `ux/` directory handles HTML rendering via `libbud` (`detail.c`, `edit.c`, `add.c`) with declarative forms (`site_ui_form_from_desc`), string-first pickers (`site_ui_picker`), and inline row replacement (`site_ui_row_replace_picker`)
- **Storage:** Filesystem using `libhyle-source` ordered DSV (`data.txt`)
- **Dependencies:** auth, common, mpfd, grp, song, index, source

## Storage

### Directory Structure
```
var/gig/
└── {gig_id}/
    ├── owner             # Plain text username
    ├── title             # Display title
    ├── grp               # Associated group slug
    └── data.txt          # Ordered gig songs (DSV format: {song_id}:{transpose}:{format})
```

### Data Format (`data.txt`)

The collection list is tracked by the `hyle_source_ordered` API, meaning the order of lines matches the order in the gig:
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

None. All legacy synchronization issues related to the outdated `grp.repertoire` system were resolved with the migration to `hyle_source_ordered`.
