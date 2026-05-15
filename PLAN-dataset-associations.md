# Plan: Fix Dataset Usage with load_dataset_item_json

## Overview

Fix songbook and choir SSR to use `load_dataset_item_json()` for single-item lookups instead of `load_dataset_json()` which loads all items.

## Status (2026-05-15)

- [x] `dataset_get_item_json()` hook implemented
- [x] `load_dataset_item_json()` Rust wrapper implemented
- [x] Songbook SSR updated to use item JSON for detail and edit
- [x] Songbook view page uses dataset-based rendering
- [x] Choir SSR updated to use item JSON
- [ ] **ISSUE: Choir detail still returns 404 for newly created choirs**
- [ ] **ISSUE: Songbook edit dropdown shows "No songs yet"**
- [ ] Verify all tests pass

---

## Current Test Results

### Passing (19 tests)
- songbook-view, songbook-edit, song-add, song-edit, song-detail-ui, song-detail-transpose, song-list
- poem-add, poem-edit, poem-delete, poem-ownership
- auth-login, auth-logout, auth-register
- accessibility-login, csrf-stability, index-add-button, login-ret

### Failing (6 tests)
- **choir-create.test.ts** - "404 Not found" - choir detail returns 404
- **choir-ownership.test.ts** - "404 Not found" - same issue
- **choir-songbook-flow.test.ts** - Timeout (depends on choir-create)
- **choir-songbooks.test.ts** - Timeout (depends on choir-create)
- **songbook-edit-row.test.ts** - "No songs yet" - song dropdown in edit form is empty
- **songbook-randomize.test.ts** - Timeout (depends on songbook-edit-row)

---

## Architecture

```
song.items      → all songs (one dataset)
songbook.items → all songbooks (one dataset)
choir.items     → all choirs (one dataset)
```

Each item's `songs` field stores IDs only:
```
songs: "coracao_de_papel:0:any:0\noutro_song:5:entrada:2"
```

### Rendering Flow (Correct)

1. Load **single** songbook via `load_dataset_item_json("songbook.items", id)` → small JSON
2. Parse `songs` field to extract song IDs
3. For each song ID, load individual song via `load_dataset_item_json("song.items", song_id)` → gets title/data

---

## Root Cause: Choir 404

The choir 404 issue appears to be related to how choir items are stored vs. how the dataset system expects them.

**Observations:**
- Choir uses `choir_index_hd` separately from `index_hd` used by songs
- `dataset_get_item_json()` uses `def->source_hd` which may not match `choir_index_hd`
- New choirs are created via C handlers that add to `choir_index_hd`
- But the dataset system iterates `source_hd` which may be different

**Debug commands:**
```bash
make test-single-capture TEST=choir-create.test.ts
tail -100 debug/runtime/ndc.log
```

See `mods/choir/README.md` for more troubleshooting info.

---

## Root Cause: Songbook Edit Dropdown

The song dropdown in the songbook edit form shows "No songs yet" even though songs exist.

**Observations:**
- `load_dataset_json("song.items")` returns empty or too-small result
- Song-list page works fine with same `load_dataset_json("song.items")`
- Inline scan in `dataset_rows_json_build` may not be triggering

**Debug commands:**
```bash
make test-single-capture TEST=songbook-edit-row.test.ts
tail -100 debug/runtime/ndc.log | grep "DEBUG dataset_rows"
```

See `debug/songbook-edit-row-debugging.md` for detailed investigation.

---

## API: load_dataset_item_json

```rust
pub fn load_dataset_item_json(fd: c_int, dataset_id: &str, id: &str) -> Option<String>
```

Returns JSON for single item like: `{"id": "...", "title": "...", ...}`

Returns `None` if item not found.

---

## Files to Update for Future Fixes

### For Choir 404 Issue
- `mods/common/common_dataset.c` - May need to understand choir's index structure
- `mods/choir/choir.c` - How choir_index_hd relates to dataset's source_hd

### For Songbook Edit Dropdown
- `mods/common/common_dataset.c` - dataset_rows_json_build inline scan
- Verify `dataset_scan_item` properly populates source_hd for new songs

---

## Benefits (when fixed)

1. **Fast**: Single item JSON is ~500 bytes vs 3.8MB for full dataset
2. **Scalable**: Works regardless of dataset size (10 or 10000 items)
3. **Correct**: No parsing failures on large JSON
4. **Efficient**: Only loads data that's actually needed
