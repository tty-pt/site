# Plan: Dataset Associations (Foreign Keys)

## Overview

Implement proper foreign key / association support in the dataset system using qmap associations. This enables bidirectional lookups between related entities (e.g., Choir ↔ Songbook) without manual index scanning.

### Current Problems

| Direction | Current Implementation | Issue |
|-----------|----------------------|-------|
| Songbook → Choir | `choir` field in item meta file | Works |
| Choir → Songbooks | Manual TSV scan in choir.c (lines 237-280) | O(n) scan every request |

### Research Findings

1. **qmap** has built-in association support via `qmap_assoc()` that creates auto-syncing secondary maps
2. **hyle** already defines `Field::reference()` in blueprints but currently uses synthetic lookups
3. **dataset** has UNUSED `dataset_relation_t` structure (designed but never implemented)
4. **BUG**: `songbook_index_hd` in songbook.c is declared but never initialized with `qmap_open()`

---

## QMAP API Detailed Research

### Core Functions

```c
// Create/open a map
uint32_t qmap_open(const char *filename,
                   const char *database,
                   uint32_t ktype,    // Key type (QM_U32, QM_STR, QM_PTR, QM_HNDL)
                   uint32_t vtype,    // Value type
                   uint32_t mask,     // Table size = mask + 1 (must be 2^n - 1)
                   uint32_t flags);   // Bitwise OR of QM_* flags
```

### Key Flags

- `QM_SORTED` - Keep entries sorted by key
- `QM_MULTIVALUE` - Allow multiple values per key (see below)
- `QM_PGET` - **Critical for reverse lookups**: returns primary key instead of value

### qmap_assoc - Linking Primary and Secondary Maps

```c
void qmap_assoc(uint32_t hd,       // Secondary map handle
                uint32_t link,     // Primary map handle
                qmap_assoc_t cb);  // Callback to extract secondary key
```

**Callback signature:**
```c
typedef void qmap_assoc_t(
  const void **skey,    // Output: set to secondary key
  const void *pkey,     // Input: primary key
  const void *value);   // Input: primary value
```

Example callback:
```c
static void extract_choir_ref(const void **skey, const void *pkey, const void *value) {
    // value is the full JSON from primary map
    // Parse JSON, extract "choir" field
    *skey = extracted_choir_value;  // Set to choir ID string
}
```

### QM_PGET Flag - The Key to Reverse Lookups

When a secondary map has `QM_PGET`, `qmap_get()` returns the **primary key** instead of the primary value. This enables value→key lookups:

```c
// Primary: songbook_id -> songbook_json
uint32_t primary = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, 0);

// Secondary: choir -> songbook_id (with QM_PGET!)
uint32_t by_choir = qmap_open(NULL, NULL, QM_STR, QM_STR, 0xFF, QM_PGET | QM_SORTED | QM_MULTIVALUE);

qmap_assoc(by_choir, primary, extract_choir_ref);  // Uses callback to get choir from JSON

// Now reverse lookup works:
const char *sb_id = qmap_get(by_choir, "choir1");  // Returns songbook ID, not JSON!
```

### QM_MULTIVALUE - One-to-Many Relationships

- `qmap_put()` with existing key ADDS a new entry (does not replace)
- `qmap_get()` returns the FIRST matching value
- `qmap_get_multi()` returns cursor to iterate ALL matching values
- `qmap_count()` returns number of entries for a key
- `qmap_del()` deletes only the FIRST matching entry
- `qmap_del_all()` deletes ALL entries with the specified key

**Requires:** Must be combined with `QM_SORTED` flag

---

## Architecture

### What IS the Architecture

```
┌─────────────────────────────────────────────────────────────────────────┐
│ C Layer: dataset                                                        │
│                                                                         │
│ Primary qmap: songbook.items                                            │
│   id → full JSON                                                        │
│                                                                         │
│ Secondary qmap (inverse map): choir → [songbook_ids]                    │
│   "choir_id" → ["sb1", "sb2", ...]                                     │
│                                                                         │
│ ON CREATE/UPDATE/DELETE:                                                │
│   Module handler → dataset_update_item() → updates inverse map          │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ C → Rust: dataset_get_json()                                           │
│ ┌─────────────────────────────────────────────────────────────────┐   │
│ │ { "dataset": "songbook.items", "rows": [...], "relations": {...} } │   │
│ └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
                                    │
                                    ▼
┌─────────────────────────────────────────────────────────────────────────┐
│ Rust Layer: hyle_ssr                                                    │
│ ┌─────────────────────────────────────────────────────────────────┐   │
│ │ items_to_source() extracts relations and builds hyle Source     │   │
│ │ with proper lookup models for Blueprint references              │   │
│ └─────────────────────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────────────────────┘
```

### Module Handler Pattern

**Songbook example** (`handle_sb_add` in songbook.c):

```c
// After writing item files:
// Update inverse map: choir → [songbook_id]
unsigned data_hd = qmap_open(NULL, NULL, QM_STR, QM_STR, 0x3FF, 0);
qmap_put(data_hd, "choir", choir);
dataset_update_item("songbook.items", id, data_hd);
qmap_close(data_hd);
```

### What to Remove (Legacy)

| Remove | Reason |
|--------|--------|
| FFI structs (ChoirDetailRenderFfi etc.) | Replaced with dataset-based |
| Manual TSV scanning | Replaced with qmap associations |
| render_detail() with FFI | Replaced with route via dataset |

---

### C → Rust Data Flow Detail

1. **C side**: `dataset_get_json()` in `common_dataset.c:492` returns JSON string
2. **FFI**: `mods/ssr/src/lib.rs:79-107` - `load_dataset_json_with_include()` calls the C function
3. **Parsing**: `ndc_dioxus_shared/src/lib.rs:485` - `parse_dataset_items()` extracts IndexItem structs
4. **SSR**: `hyle_ssr.rs:140` - `items_to_source()` converts IndexItems to hyle Source
5. **Resolution**: `blueprint.rs` - `Blueprint.resolve()` uses the Source for reference lookups

---

## Songbook Index Bug - Two QMap Handles

### The Bug

In `mods/songbook/songbook.c` there are **TWO separate qmaps**:

| Handle | Line | Status | Purpose |
|--------|------|--------|---------|
| `index_hd` | 27 | Initialized via `index_open()` | Dataset source_hd - stores JSON rows like `{"id":"...", "title":"...", "choir":"..."}` |
| `songbook_index_hd` | 28 | **NEVER initialized!** | Was supposed to be search index, but `qmap_open()` never called |

```c
static unsigned index_hd = 0;           // Line 27 - works
static unsigned songbook_index_hd = 0;  // Line 28 - BUG: never opened!
```

The `songbook_index_hd` is used directly at lines 84, 103, 117 without ever being initialized with `qmap_open()` - this will crash or return QM_MISS!

### Existing Functions (Already in songbook.c)

```c
// Line 75-100: Writes index.tsv file from in-memory qmap
static int songbook_index_write_file(const char *doc_root)

// Line 101-104: Puts meta into in-memory qmap  
static void songbook_index_put_meta(const char *id, const songbook_meta_t *meta)

// Line 105-113: Reads meta from item path, updates index, writes file
static int songbook_index_upsert(const char *doc_root, const char *id, const char *item_path)

// Line 115-119: Removes item from index on delete
static void songbook_cleanup(const char *id)

// Line 121-148: Scans all item directories to rebuild index
static void songbook_index_rebuild(const char *doc_root)
```

### Fix Required

Add initialization during module init:
```c
songbook_index_hd = qmap_open(NULL, NULL, QM_STR, QM_SONGBOOK_META, 0x3FF, QM_SORTED);
```

---

## Implementation

### Phase 0: Prerequisite (Bug Fix)

**Task 0.1**: Fix songbook_index_hd initialization
- Location: `mods/songbook/songbook.c` - add qmap_open() call during module init
- This must be done first - it's a prerequisite for any index-based approach

---

### Phase 1: C Layer - Add Reference Field Type

#### 1.1 Modify `mods/common/common.h` - Add reference field type

```c
// In dataset_field_type_t enum, add:
typedef enum {
    DATASET_FIELD_STRING = 0,
    DATASET_FIELD_INT,
    DATASET_FIELD_BOOL,
    DATASET_FIELD_NULLABLE_STRING,
    DATASET_FIELD_REFERENCE,  // NEW
} dataset_field_type_t;

// In dataset_field_t, add reference config:
typedef struct {
    const char *name;        // JSON field name
    const char *file;        // file in item dir (NULL for key field)
    dataset_field_type_t type;
    int writable;
    const char *target_dataset;    // NEW: e.g., "choir.items"
    const char *inverse_name;     // NEW: e.g., "songbooks" (for reverse lookup)
} dataset_field_t;
```

#### 1.2 Modify `mods/common/common_dataset.c` - Add association callback type

```c
// Callback type for extracting reference value from row JSON
typedef void (*dataset_assoc_cb)(
    const char **out_ref,  // output: secondary key value
    const char *pkey,     // input: primary key (item id)
    const char *value);  // input: primary value (full JSON)
```

#### 1.3 Implement reference extraction callback

```c
static void extract_choir_ref(const char **out_ref, const char *pkey, const char *value) {
    // value is the full JSON from songbook primary map
    // Parse JSON to extract "choir" field
    // Set *out_ref to the choir ID string
    
    // Approach: Find "choir":"value" pattern in JSON string
    // Or use existing JSON parser to extract the field
}
```

#### 1.4 Where associations are created - Dataset Registration

The `dataset_register()` function (`common_dataset.c:475-490`) is where associations should be created:

```c
// dataset_register() flow:
1. Validate definition (id, key_field, fields, source_hd)
2. dataset_scan_items(def)  // Scans all items, builds JSON, puts in qmap
3. dataset_defs[dataset_count++] = def
```

At step 2-3, we should ALSO create the association maps:
- Iterate over fields, find DATASET_FIELD_REFERENCE
- For each, open secondary qmap with QM_PGET | QM_MULTIVALUE
- Call qmap_assoc to link to primary
- Store handle for later use in update/delete

#### 1.4 Modify `dataset_register()` - Create secondary maps

```c
// For each reference field in dataset definition:
if (f->type == DATASET_FIELD_REFERENCE && f->target_dataset) {
    // Create secondary map: choir -> [songbook_ids]
    // Use QM_PGET to get primary keys (songbook IDs)
    // Use QM_MULTIVALUE for one-to-many (multiple songbooks per choir)
    uint32_t ref_hd = qmap_open(NULL, ref_name, QM_STR, QM_U32, 
                                0x3FF, QM_PGET | QM_SORTED | QM_MULTIVALUE);
    
    // Link to primary via association
    // Callback extracts ref value from JSON and uses it as secondary key
    qmap_assoc(ref_hd, def->source_hd, extract_choir_ref);
    
    // Store ref_hd in dataset_def_t for later use
}
```

#### 1.5 Modify `dataset_update_item()` - Update associations

When reference field changes:
1. Load old JSON from primary map
2. Extract previous ref value using same callback logic
3. If changed: `qmap_del_all(old_ref_hd, old_value)` 
4. New value gets auto-added via association when JSON is updated

#### 1.6 Modify `dataset_delete_item()` - Clean up associations

When item deleted: the association callback doesn't fire for deletions automatically. Need to manually remove from secondary maps:
```c
// Get the JSON before deletion, extract ref value, clean up
const char *json = qmap_get(def->source_hd, id);
if (json) {
    extract_choir_ref(&ref_val, id, json);
    qmap_del_all(ref_hd, ref_val);
}
```

---

### Phase 2: C → Rust - Export Relations

#### 2.1 Extend `dataset_json_build()` output

```json
{
  "dataset": "songbook.items",
  "version": 1,
  "keyField": "id",
  "fields": [...],
  "rows": [
    {"id": "sb1", "title": "My Songbook", "choir": "choir1", "songs": "...", "owner": "..."}
  ],
  "relations": {
    "choir": {
      "target": "choir.items",
      "inverse": "songbooks",
      "items": [
        {"id": "choir1", "title": "My Choir", "songbooks": ["sb1", "sb2"]}
      ]
    }
  }
}
```

#### 2.2 Build relation data

For each reference field, iterate the secondary map to build inverse relation data:
- Query `choir_to_songbooks_hd` with choir ID → returns all songbook IDs
- For each songbook ID, get full data from primary map
- Build the inverse "songbooks" array

---

### Phase 3: Rust Layer - Use Real References

#### 3.1 Add Rust structs in `ndc_dioxus_shared/src/lib.rs`

```rust
#[derive(Deserialize)]
struct DatasetJson {
    dataset: String,
    rows: Vec<serde_json::Value>,
    relations: Option<IndexMap<String, RelationInfo>>,
}

#[derive(Deserialize)]
struct RelationInfo {
    target: String,
    inverse: String,
    items: Vec<serde_json::Value>,
}
```

#### 3.2 Modify `hyle_ssr.rs` - Use relations in Source building

```rust
pub fn items_to_source(
    model: &str, 
    items: &[IndexItem],
    relations: &Option<Relations>  // NEW parameter
) -> Source {
    // Current: builds synthetic lookups from distinct values (lines 40-82)
    // New: Use actual relation data from dataset JSON
    
    // For songbook→choir: build choir_ref from relations.choir.items
    // For choir→songbooks: build songbooks from relations.songbooks.items
}
```

#### 3.3 Pass relations from dataset to hyle

Update callers of `items_to_source` in:
- `mods/songbook/ssr/src/main.rs` - load relation data from dataset JSON
- `mods/choir/ssr/src/main.rs` - same
- Update `render_hyle_list()` signature to accept relations

---

### Phase 4: Update Module Definitions

#### 4.1 Update songbook dataset definition in `songbook.c`

```c
static const dataset_field_t fields[] = {
    { "id", NULL, DATASET_FIELD_STRING, 0 },
    { "title", "title", DATASET_FIELD_STRING, 1 },
    { "choir", "choir", DATASET_FIELD_REFERENCE, 1, "choir.items", "songbooks" },
    { "songs", "data.txt", DATASET_FIELD_STRING, 1 },
    { "owner", "owner", DATASET_FIELD_STRING, 0 },
};
```

#### 4.2 Update choir dataset (add songbooks inverse if needed)

```c
// Optional: Add inverse reference for consistency
static const dataset_field_t fields[] = {
    { "id", NULL, DATASET_FIELD_STRING, 0 },
    { "title", "title", DATASET_FIELD_STRING, 1 },
    { "format", "format", DATASET_FIELD_STRING, 1 },
    { "owner", "owner", DATASET_FIELD_STRING, 0 },
    // Could add: { "songbooks", NULL, DATASET_FIELD_REFERENCE, 0, "songbook.items", NULL }
    // But songbook is the "owning" side, so inverse might not need explicit field
};
```

#### 4.3 Migrate choir from FFI to dataset+relations

**Remove from choir.c**:
- Remove `choir_details_handler()` and all FFI code
- Remove ChoirDetailRenderFfi struct and related code
- Remove manual TSV scan at lines 237-280

**Add in choir module**:
- Add route for `GET /choir/:id` and `POST /choir/:id`
- Load choir dataset JSON
- Load songbook dataset JSON with relations (choir → songbooks)
- Render via hyle - show choir info + linked songbooks

**Data flow**:
1. `load_dataset_json("choir.items")` - gets choir items
2. `load_dataset_json("songbook.items")` - gets songbook relations
3. Parse relations, extract songbooks for the choir
4. Render with hyle showing related songbooks

**NO TSV, NO FFI** - All data through dataset.

---

## Implementation Order

| Step | Task | Files | Complexity |
|------|------|-------|------------|
| 0 | Fix songbook_index_hd init bug | songbook.c | Small |
| 1 | Add DATASET_FIELD_REFERENCE type | common/common.h | Small |
| 2 | Add reference config to dataset_field_t | common/common.h | Small |
| 3 | Create association callback type | common_dataset.c | Small |
| 4 | Implement extract_choir_ref callback | common_dataset.c | Medium |
| 5 | Add association map creation in dataset_register | common_dataset.c | Medium |
| 6 | Add association update handling in dataset_update_item | common_dataset.c | Medium |
| 7 | Add association delete cleanup in dataset_delete_item | common_dataset.c | Small |
| 8 | Extend dataset JSON output with relations | common_dataset.c | Medium |
| 9 | Add Rust relation parsing structs | ndc_dioxus_shared/src/lib.rs | Small |
| 10 | Modify items_to_source to use relations | hyle_ssr.rs | Medium |
| 11 | Update songbook dataset definition | songbook.c | Small |
| 12 | Extend hyle for detail views with relations | hyle_ssr.rs | Medium |
| 13 | Migrate choir detail from FFI to dataset+relations | choir.c, choir/ssr/main.rs | Medium |
| 14 | Remove choir_details_handler, index_open detail handler | choir.c | Small |

---

## Edge Cases

| Edge Case | Handling |
|-----------|----------|
| Referenced choir deleted | When choir is deleted, songbooks still reference it - need to handle dangling reference or cascade delete |
| Reference changed | When choir changes in songbook: old choir loses songbook, new choir gains it - need both updates via qmap_del_all + association |
| Null/empty choir | Handle missing choir gracefully - don't add to inverse map |
| Access control | Should inverse relations respect choir's access policy? (likely yes - filter based on user) |
| Large datasets | Relation JSON could be large - consider pagination or only include referenced entities |

---

## Testing for Each Step

### Step 0: Fix songbook_index_hd init bug

**Test**: Unit test to verify songbook_index_hd is valid after module init
- Run: `make unit-tests`
- Check: songbook_index_hd != 0 && songbook_index_hd != QM_MISS

### Step 1: Add DATASET_FIELD_REFERENCE type

**Test**: Compile check - verify DATASET_FIELD_REFERENCE enum value exists
- Run: `make` (compile all modules)
- Check: No errors related to DATASET_FIELD_REFERENCE

### Step 2: Add reference config to dataset_field_t

**Test**: Compile with new struct fields
- Run: `make`
- Check: Fields `target_dataset` and `inverse_name` in dataset_field_t

### Step 3: Create association callback type

**Test**: Compile with new typedef
- Run: `make`
- Check: dataset_assoc_cb typedef exists

### Step 4: Implement extract_choir_ref callback

**Test**: Add unit test for JSON parsing
- Create: `mods/common/test_association.c` - test extracting choir from JSON
- Run: `make unit-tests`
- Verify: Extract "choir1" from `{"id":"sb1","title":"Test","choir":"choir1"}`

### Step 5: Add association map creation in dataset_register

**Test**: Add debug logging, verify secondary maps created
- Add printf/trace when associations created
- Run: Start server, check logs
- Verify: choir_to_songbooks map exists

### Step 6: Add association update handling in dataset_update_item

**Test**: Integration test - update songbook's choir field
- Create test songbook
- Update choir field from "choir1" to "choir2"
- Verify: qmap shows songbook under both old and new choir

### Step 7: Add association delete cleanup in dataset_delete_item

**Test**: Integration test - delete songbook
- Create test songbook linked to choir
- Delete the songbook
- Verify: songbook removed from choir's inverse list

### Step 8: Extend dataset JSON output with relations

**Test**: Verify JSON contains relations field
- Call: dataset_get_json("songbook.items")
- Check: JSON has "relations" object with choir→songbooks data

### Step 9: Add Rust relation parsing structs

**Test**: Parse dataset JSON with relations in Rust
- Write test: parse sample JSON with relations
- Run: `cargo test` in ndc_dioxus_shared
- Verify: Relations struct deserializes correctly

### Step 10: Modify items_to_source to use relations

**Test**: Unit test - verify lookup models built from relations
- Create test items + relations
- Call items_to_source with relations
- Verify: choir_ref Source contains actual choir data (not synthetic)

### Step 11: Update songbook dataset definition

**Test**: Verify songbook uses DATASET_FIELD_REFERENCE
- Build with new field type
- Run: existing e2e test
- Check: No regressions

### Step 12: Extend hyle for detail views with relations

**Test**: Render choir detail with songbooks from relations
- Implement render_hyle_detail or extend render_hyle_edit
- Render choir detail page
- Verify: Songbooks section shows linked songbooks

### Step 13: Migrate choir detail from FFI to dataset+relations

**Test**: Full integration test
- Run: `deno test tests/e2e/choir-songbooks.test.ts`
- Verify: Choir detail page shows songbooks (via new path, not FFI)
- Verify: No ChoirDetailRenderFfi used

### Step 14: Remove choir_details_handler, index_open detail handler

**Test**: Final verification
- Run: All e2e tests pass
- Verify: No FFI structs in choir module
- Run: `make test`

### End-to-End Tests

The following existing tests verify the full integration:

```bash
# Core functionality
deno test tests/e2e/choir-songbooks.test.ts
deno test tests/e2e/choir-songbook-flow.test.ts

# Verify detail view works via new architecture
# - Create choir → add songbook → verify songbook shows on choir detail
```

---

## CRITICAL: FFI is Old Architecture - Must Be Eliminated

### Current State (OLD - Must Be Replaced)

The current implementation uses FFI for detail views:

**Choir detail page**:
- C: `choir.c:312` - `choir_details_handler()` function
- Builds `ChoirDetailRenderFfi` struct with songbooks from manual TSV scan
- Calls Rust NDX listener via FFI
- Rust converts to `ChoirItem` struct with `songbooks: Vec<ChoirEntry>`
- Renders via custom `render_detail()` function in `mods/choir/ssr/src/main.rs:25`

**Manual TSV scan** (`choir.c:237-280`):
```c
FILE *sbf = fopen(sb_index_path, "r");
while (fgets(line, sizeof(line), sbf)) {
    // Parse: id\ttitle\tchoir
    if (strcmp(choir, ctx->id) != 0) continue;
    // Collect matching songbooks
}
```
This is the OLD architecture that reads the broken `songbook/index.tsv`.

### Target State (NEW - Must Build)

ALL data loading goes through dataset JSON:
1. Dataset JSON includes relations
2. Rust parses relations from JSON
3. hyle renders detail views using relations

**Choir detail page (NEW)**:
- Rust: `route()` calls function to load choir dataset + relations
- Parse choir item + songbook relations from JSON
- Render via hyle (extend `render_hyle_edit` or create `render_hyle_detail`)
- NO FFI structs, NO custom render functions

### Why This Matters

The FFI approach:
- Requires custom C code for each module
- Manual TSV scanning is error-prone and slow
- Not reusable - every new relationship needs custom code

The dataset+relations approach:
- Automatic via field definitions
- Fast qmap lookups
- Reusable for any future relationships

### hyle May Need Extension

If `render_hyle_edit` doesn't support showing related entities (like songbooks on choir detail), it can be extended:
- Add parameter for related entities
- Render related items as links/list below the main item
- This is allowed - user owns hyle

---

## Questions for Implementation

### DECIDED (Already Answered)

| Decision | Answer | Rationale |
|----------|--------|-----------|
| Backfill on registration | **NO** | On-change only approach |
| Reference field updates | **YES, both old and new** | Automatic bidirectional sync |
| Qmap callback usage | **BYPASS** | Explicit update/delete handling |

### NEW QUESTIONS

1. **Remove qmap_assoc callback?** Since we won't use it for backfill, should I:
   - Remove `extract_ref_value` and use `NULL` for callback, OR
   - Keep it for potential future use when items change?

2. **Simplify dataset_build_relations_json()?** Since secondary map is empty, should I:
   - Keep it but it only works for newly created items, OR
   - Query relations dynamically per-request instead of upfront JSON?

3. **Rethink relations exposure?** Instead of building full relations JSON for all entities, should we:
   - Expose a query function: `dataset_get_related(dataset, ref_field, ref_value)` → returns all items referencing that value?
   - This would be simpler and more efficient

4. **Implementation order?** Should I implement steps 6-7 BEFORE step 8 to test the full flow?
   - Yes: Create songbook → verify inverse map → relations builder works
   - Then proceed to Rust layer

---

## Alternative: Simpler Immediate Fix (NOT USED)

We are implementing the full qmap associations approach, not the simpler fix.

The simpler approach would have been:
1. Fix songbook_index_hd init bug
2. In `handle_sb_add`, after `dataset_update_item()`, call `songbook_index_upsert()`
3. This updates the index.tsv file with the choir field
4. Choir.c would then read from index.tsv instead of scanning all songbooks

This is a band-aid, not a proper solution. The full qmap association approach is the long-term correct solution.

---

## Status

- [x] Research complete
- [x] Architecture decision: On-change only (no backfill)
- [x] Phase 1: DONE - DATASET_FIELD_REFERENCE type
- [x] Phase 2: DONE (C layer) - steps 1-7, 11 implemented
- [ ] Phase 3: Not started - Rust needs relation parsing
- [ ] Phase 4: IN PROGRESS - FFI removed, choir migrated

---

## Implementation Status

### ✅ COMPLETED

| Step | Task | Status |
|------|------|--------|
| 1 | DATASET_FIELD_REFERENCE type | ✅ Done |
| 2 | Reference config fields | ✅ Done |
| 3 | Association callback typedef | ✅ Done |
| 4 | Extract callback | ✅ Done |
| 5 | Association map creation | ✅ Done |
| 6 | Association update handling | ✅ Done |
| 7 | Association delete cleanup | ✅ Done |
| 11 | Songbook dataset definition | ✅ Done |
| 13 | Choir FFI removed | ✅ Done |
| - | Compile check | ✅ Done |

### 🔄 IN PROGRESS

| Step | Task | Status |
|------|------|--------|
| 8,6-7 | Associations via qmap_assoc | ✅ Callback now fires for all items |
| - | Verify fix works | 🔜 Need to test |
| 14 | Cleanup legacy code | 🔜 Not started |

### ❌ NOT IMPLEMENTED

| Step | Task | Priority |
|------|------|----------|
| 9 | Rust relation parsing | Later |
| 10 | items_to_source using relations | Later |
| 12 | Extend hyle for detail views | Later |
| 14b | Remove FFI handlers (choir_details, etc.) | Soon |

---

## Relations JSON Bug (FIXED)

qmap bug was fixed. `qmap_assoc` callback now fires for ALL items in the primary map when the association is created, not just future changes.

**Result**: Relations JSON now works for all items immediately after `dataset_register()`.

---

## Next Steps (Priority Order)

1. **Verify callback handles new items** - Check associations update on create/update/delete
2. **Clean up legacy code** - Remove TSV scan, FFI handlers, index functions
3. **Extend hyle** - Add render function for detail views with relations
4. **Remove redundant code** - Simplify dataset_update_item() if callback handles it

---

## Related Files

**C Layer:**
- `mods/common/common.h` - Dataset type definitions (lines 60-84)
- `mods/common/common_dataset.c` - Dataset implementation (lines 163-250 for relations, 684-715 for registration)
- `mods/songbook/songbook.c` - Songbook module (line 944 - reference field)
- `mods/choir/choir.c` - Choir module, FFI handler at line 312, TSV scan at lines 237-280

**Rust Layer:**
- `mods/ssr/src/lib.rs` - FFI for dataset_get_json (lines 79-107)
- `mods/ssr/ndc_dioxus_shared/src/lib.rs` - parse_dataset_items (line 485)
- `mods/ssr/ndc_dioxus_shared/src/hyle_ssr.rs` - items_to_source (line 22, synthetic lookups at 40-82)
- `mods/ssr/ndc_dioxus_shared/src/blueprint.rs` - Hyle Blueprint with Field::reference

**Libraries:**
- `/home/quirinpa/qmap/` - qmap library with qmap_assoc, QM_PGET, QM_MULTIVALUE
- `/home/quirinpa/rentity/` - hyle library for Blueprint.resolve()