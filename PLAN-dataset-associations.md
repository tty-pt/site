# Plan: Fix Dataset Usage with load_dataset_item_json

## Overview

Fix songbook SSR to use `load_dataset_item_json()` for single-item lookups instead of `load_dataset_json()` which loads all items.

## Status

- [x] `dataset_get_item_json()` hook implemented
- [x] `load_dataset_item_json()` Rust wrapper implemented
- [x] Plan updated
- [ ] Update songbook SSR to use item JSON
- [ ] Update choir SSR to use item JSON
- [ ] Update poem SSR to use item JSON
- [ ] Verify all tests pass

---

## Architecture

```
song.items      → all songs (one dataset)
songbook.items → all songbooks (one dataset)
```

Each item's `songs` field stores IDs only:
```
songs: "coracao_de_papel:0:any:0\noutro_song:5:entrada:2"
```

### Rendering Flow (Correct)

1. Load **single** songbook via `load_dataset_item_json("songbook.items", id)` → small JSON
2. Parse `songs` field to extract song IDs
3. For each song ID, load individual song via `load_dataset_item_json("song.items", song_id)` → gets title/data

### Rendering Flow (Previous Bug)

1. Load **all** songbooks via `load_dataset_json("songbook.items")` → 3.8MB JSON, parse fails
2. Search for item in array
3. For each song ID, load **all** songs via `load_dataset_json("song.items")` → large JSON

---

## Files to Update

### Songbook SSR (mods/songbook/ssr/src/main.rs)

| Function | Change |
|----------|--------|
| `render_detail` | Use `load_dataset_item_json()` for songbook and each song |
| `render_edit` | Use `load_dataset_item_json()` for songbook |
| `render_randomize` | Use `load_dataset_item_json()` for songbook |

### Choir SSR (mods/choir/ssr/src/main.rs)

| Function | Change |
|----------|--------|
| `render_choir_detail` | Use `load_dataset_item_json()` for choir |
| `render_edit` | Use `load_dataset_item_json()` for choir |

### Poem SSR (mods/poem/ssr/src/main.rs)

| Function | Change |
|----------|--------|
| `render_poem_detail` | Use `load_dataset_item_json()` for poem |
| `render_poem_edit` | Use `load_dataset_item_json()` for poem |

---

## API: load_dataset_item_json

```rust
pub fn load_dataset_item_json(fd: c_int, dataset_id: &str, id: &str) -> Option<String>
```

Returns JSON for single item like: `{"id": "...", "title": "...", ...}`

Returns `None` if item not found.

---

## Testing

```bash
# Build
make

# Individual tests
deno test --allow-all tests/e2e/songbook-view.test.ts
deno test --allow-all tests/e2e/songbook-edit.test.ts
deno test --allow-all tests/e2e/choir-create.test.ts

# Full suite
make e2e-tests
```

---

## Benefits

1. **Fast**: Single item JSON is ~500 bytes vs 3.8MB for full dataset
2. **Scalable**: Works regardless of dataset size (10 or 10000 items)
3. **Correct**: No parsing failures on large JSON
4. **Efficient**: Only loads data that's actually needed
