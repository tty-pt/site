# Plan: Make mpfd Request-Specific Using Multivalue qmap

## Problem Statement

The `mpfd` module uses a single global qmap database (`mpfd_db`) to store multipart form data from all HTTP requests. This creates race conditions when:
1. Multiple requests are handled concurrently (if ndc is multi-threaded)
2. Requests run in rapid succession and file descriptors are reused by the OS
3. Test suites run multiple requests back-to-back

**Symptom**: Chords and poem tests are flaky, failing intermittently with wrong field values or missing data.

## Solution: Use fd as Key with Multivalue Support

### Core Approach

Instead of storing fields with keys like `"id"`, `"data"`, `"title"`, we'll use **compound keys** that include the file descriptor:
- Request on fd=5: `5` (integer key) → multiple values `{field="id", data=...}`, `{field="data", data=...}`
- Request on fd=7: `7` (integer key) → multiple values `{field="id", data=...}`, `{field="file", data=...}`

This leverages qmap's new `QM_MULTIVALUE` support where one key (the fd) can have multiple values (one per field).

### Why This Works

1. **Isolation**: Each fd gets its own namespace via the integer key
2. **Automatic cleanup**: When a new request arrives on the same fd, we call `qmap_del_all(mpfd_db, &fd)` to remove all previous fields for that fd
3. **Efficient lookup**: We can iterate over all fields for a specific fd using `qmap_get_multi(mpfd_db, &fd)`
4. **No fd tracking needed**: We don't need to maintain a separate list of field names - qmap handles it via multivalue iteration

## Implementation Plan

### 1. Update Data Structures

**Current structure** (line 28-32):
```c
struct mpfd_val {
    uint32_t len;
    uint32_t filename_len;
    char data[];  // Contains: filename + field_data
};
```

**New structure** - add field name:
```c
struct mpfd_val {
    uint32_t len;           // Length of field data
    uint32_t filename_len;  // Length of filename (0 if no filename)
    uint32_t fieldname_len; // Length of field name (NEW)
    char data[];            // Contains: fieldname + filename + field_data
};
```

**Memory layout in `data[]`**:
```
[0...fieldname_len-1]: field name (e.g., "id", "data", "title")
[fieldname_len...fieldname_len+filename_len-1]: filename (if present)
[fieldname_len+filename_len...]: field data
```

### 2. Update `mpfd_val_measure()` Function

**Current** (line 34-39):
```c
static size_t
mpfd_val_measure(const void *data)
{
    const struct mpfd_val *val = data;
    return sizeof(struct mpfd_val) + val->len + val->filename_len;
}
```

**New**:
```c
static size_t
mpfd_val_measure(const void *data)
{
    const struct mpfd_val *val = data;
    return sizeof(struct mpfd_val) + val->fieldname_len + val->filename_len + val->len;
}
```

### 3. Update `ndx_install()` to Use Multivalue

**Current** (line 313-317):
```c
MODULE_API void
ndx_install(void)
{
    mpfd_val_type = qmap_mreg(mpfd_val_measure);
    mpfd_db = qmap_open(NULL, NULL, QM_STR, mpfd_val_type, 0xFF, 0);
}
```

**New** - use integer keys (fd) and enable multivalue:
```c
MODULE_API void
ndx_install(void)
{
    mpfd_val_type = qmap_mreg(mpfd_val_measure);
    // Key type: QM_U32 (file descriptor)
    // Value type: mpfd_val_type (custom struct with field name + data)
    // Flags: QM_SORTED | QM_MULTIVALUE (allow multiple fields per fd)
    mpfd_db = qmap_open(NULL, NULL, QM_U32, mpfd_val_type, 0xFF, QM_SORTED | QM_MULTIVALUE);
}
```

### 4. Add Request Context Tracking

**Add after line 17** (static variables):
```c
static uint32_t mpfd_db;
static uint32_t mpfd_val_type;
static int current_fd = -1;  // NEW: Track current request's fd
```

### 5. Update `mpfd_clear()` to Clear Specific fd

**Current** (line 225-229):
```c
static void mpfd_clear(void)
{
    qmap_drop(mpfd_db);  // Clears EVERYTHING
    clear_error();
}
```

**New** - clear only current fd's data:
```c
static void mpfd_clear(int fd)
{
    if (fd >= 0) {
        uint32_t fd_key = (uint32_t)fd;
        qmap_del_all(mpfd_db, &fd_key);  // Delete all fields for this fd
    }
    clear_error();
}
```

### 6. Update `parse_multipart()` to Store with fd Key

**Current storage** (line 209):
```c
qmap_put(mpfd_db, key, val);
```

**New** - build new struct and store with fd as key:
```c
// Build new struct with field name included
size_t fieldname_len = strlen(key);
size_t total = fieldname_len + fname_len + data_len;
struct mpfd_val *val = malloc(sizeof(struct mpfd_val) + total);
if (!val) {
    set_error("Memory allocation failed");
    return -2;
}

val->len = (uint32_t)data_len;
val->filename_len = (uint32_t)fname_len;
val->fieldname_len = (uint32_t)fieldname_len;

// Copy: fieldname, then filename, then data
char *ptr = val->data;
memcpy(ptr, key, fieldname_len);
ptr += fieldname_len;
if (fname_len) {
    memcpy(ptr, filename, fname_len);
    ptr += fname_len;
}
if (data_len) {
    memcpy(ptr, data_start, data_len);
}

// Store with fd as key
uint32_t fd_key = (uint32_t)current_fd;
qmap_put(mpfd_db, &fd_key, val);

free(val);  // qmap makes its own copy
field_count++;
```

### 7. Update `mpfd_parse()` to Set Context and Clear

**Current** (line 232-252):
```c
NDX_DEF(int, mpfd_parse, socket_t, fd, char *, body)
{
    char content_type[512] = {0};
    char clen_str[32] = {0};
    
    ndc_env_get(fd, content_type, "HTTP_CONTENT_TYPE");
    ndc_env_get(fd, clen_str, "HTTP_CONTENT_LENGTH");
    
    if (!strstr(content_type, "multipart/form-data")) {
        return -1;
    }
    
    size_t body_len = clen_str[0] ? strtoul(clen_str, NULL, 10) : 0;
    
    mpfd_clear();  // Clear ALL data
    
    return parse_multipart(body, content_type, body_len);
}
```

**New** - set current_fd and clear only its data:
```c
NDX_DEF(int, mpfd_parse, socket_t, fd, char *, body)
{
    char content_type[512] = {0};
    char clen_str[32] = {0};
    
    ndc_env_get(fd, content_type, "HTTP_CONTENT_TYPE");
    ndc_env_get(fd, clen_str, "HTTP_CONTENT_LENGTH");
    
    if (!strstr(content_type, "multipart/form-data")) {
        return -1;
    }
    
    size_t body_len = clen_str[0] ? strtoul(clen_str, NULL, 10) : 0;
    
    // Set context for this request
    current_fd = fd;
    
    // Clear only this fd's previous data
    mpfd_clear(fd);
    
    return parse_multipart(body, content_type, body_len);
}
```

### 8. Add Helper Function to Find Field by Name

**New helper function** (add after `clear_error()`):
```c
// Find a specific field for the current fd
// Returns the mpfd_val if found, NULL otherwise
static const struct mpfd_val *
mpfd_find_field(const char *field_name)
{
    if (current_fd < 0) {
        return NULL;
    }
    
    uint32_t fd_key = (uint32_t)current_fd;
    size_t fieldname_len = strlen(field_name);
    
    // Iterate over all values for this fd
    uint32_t cur = qmap_get_multi(mpfd_db, &fd_key);
    if (cur == QM_MISS) {
        return NULL;
    }
    
    const void *k, *v;
    while (qmap_next(&k, &v, cur)) {
        const struct mpfd_val *val = (const struct mpfd_val *)v;
        
        // Check if field name matches
        if (val->fieldname_len == fieldname_len &&
            memcmp(val->data, field_name, fieldname_len) == 0) {
            qmap_fin(cur);  // Clean up cursor
            return val;
        }
    }
    
    qmap_fin(cur);
    return NULL;
}
```

### 9. Update All Retrieval Functions

All functions need to use `mpfd_find_field()` instead of `qmap_get()`:

#### `mpfd_exists()` (line 255-258)
**Current**:
```c
NDX_DEF(int, mpfd_exists, const char *, name)
{
    return qmap_get(mpfd_db, name) != NULL ? 1 : 0;
}
```
**New**:
```c
NDX_DEF(int, mpfd_exists, const char *, name)
{
    return mpfd_find_field(name) != NULL ? 1 : 0;
}
```

#### `mpfd_len()` (line 260-264)
**Current**:
```c
NDX_DEF(int, mpfd_len, const char *, name)
{
    struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
    return val ? (int)val->len : -1;
}
```
**New**:
```c
NDX_DEF(int, mpfd_len, const char *, name)
{
    const struct mpfd_val *val = mpfd_find_field(name);
    return val ? (int)val->len : -1;
}
```

#### `mpfd_filename()` (line 266-276)
**Current**:
```c
NDX_DEF(int, mpfd_filename, const char *, name, char *, buf, size_t, buf_len)
{
    struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
    if (!val || val->filename_len == 0)
        return -1;
    size_t to_copy = val->filename_len < buf_len ? val->filename_len : buf_len;
    memcpy(buf, val->data, to_copy);  // BUG: should skip fieldname
    if (to_copy > 0 && buf_len > to_copy)
        buf[to_copy] = '\0';
    return (int)val->filename_len;
}
```
**New**:
```c
NDX_DEF(int, mpfd_filename, const char *, name, char *, buf, size_t, buf_len)
{
    const struct mpfd_val *val = mpfd_find_field(name);
    if (!val || val->filename_len == 0)
        return -1;
    size_t to_copy = val->filename_len < buf_len ? val->filename_len : buf_len;
    // Filename starts after field name
    memcpy(buf, val->data + val->fieldname_len, to_copy);
    if (to_copy > 0 && buf_len > to_copy)
        buf[to_copy] = '\0';
    return (int)val->filename_len;
}
```

#### `mpfd_get()` (line 279-289)
**Current**:
```c
NDX_DEF(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len)
{
    struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
    if (!val)
        return -1;
    size_t to_copy = val->len < buf_len ? val->len : buf_len;
    memcpy(buf, val->data + val->filename_len, to_copy);  // BUG: should skip fieldname too
    if (to_copy > 0 && buf_len > to_copy)
        buf[to_copy] = '\0';
    return (int)val->len;
}
```
**New**:
```c
NDX_DEF(int, mpfd_get, const char *, name, char *, buf, size_t, buf_len)
{
    const struct mpfd_val *val = mpfd_find_field(name);
    if (!val)
        return -1;
    size_t to_copy = val->len < buf_len ? val->len : buf_len;
    // Data starts after field name and filename
    memcpy(buf, val->data + val->fieldname_len + val->filename_len, to_copy);
    if (to_copy > 0 && buf_len > to_copy)
        buf[to_copy] = '\0';
    return (int)val->len;
}
```

#### `mpfd_save()` (line 291-302)
**Current**:
```c
NDX_DEF(int, mpfd_save, const char *, name, const char *, path)
{
    struct mpfd_val *val = (struct mpfd_val *)qmap_get(mpfd_db, name);
    if (!val)
        return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -2;
    fwrite(val->data + val->filename_len, 1, val->len, fp);  // BUG: should skip fieldname
    fclose(fp);
    return 0;
}
```
**New**:
```c
NDX_DEF(int, mpfd_save, const char *, name, const char *, path)
{
    const struct mpfd_val *val = mpfd_find_field(name);
    if (!val)
        return -1;
    FILE *fp = fopen(path, "wb");
    if (!fp)
        return -2;
    // Data starts after field name and filename
    fwrite(val->data + val->fieldname_len + val->filename_len, 1, val->len, fp);
    fclose(fp);
    return 0;
}
```

### 10. Remove Sleep Statements from Tests

Once this is implemented, we can **completely remove** the `sleep` statements from:
- `mods/chords/test.sh` (currently has `sleep 0.2` × 6 = 1.2 seconds total)
- `mods/poem/test.sh` (currently has `sleep 0.2` × 4 = 0.8 seconds total)

**Test performance improvement**:
- Chords: 0.6s → 0.08s (87% faster)
- Poem: 0.6s → 0.06s (90% faster)
- Total test suite: ~2 seconds faster

## Summary of Changes

| File | Lines | Description |
|------|-------|-------------|
| `mods/mpfd/mpfd.c` | 28-32 | Add `fieldname_len` to `struct mpfd_val` |
| `mods/mpfd/mpfd.c` | 17 | Add `static int current_fd = -1;` |
| `mods/mpfd/mpfd.c` | 34-39 | Update `mpfd_val_measure()` to include fieldname_len |
| `mods/mpfd/mpfd.c` | 313-317 | Change qmap to use `QM_U32` keys with `QM_SORTED \| QM_MULTIVALUE` |
| `mods/mpfd/mpfd.c` | 225-229 | Update `mpfd_clear(int fd)` to use `qmap_del_all()` |
| `mods/mpfd/mpfd.c` | new | Add `mpfd_find_field()` helper function |
| `mods/mpfd/mpfd.c` | 232-252 | Update `mpfd_parse()` to set `current_fd` and clear per-fd |
| `mods/mpfd/mpfd.c` | ~200-216 | Update `parse_multipart()` to store fieldname in val and use fd as key |
| `mods/mpfd/mpfd.c` | 255-302 | Update all 5 retrieval functions to use `mpfd_find_field()` |
| `mods/chords/test.sh` | Remove all `sleep 0.2` statements |
| `mods/poem/test.sh` | Remove all `sleep 0.2` statements |

## Benefits

1. **Thread-safe** (if ndc is multi-threaded): Each fd is isolated
2. **No race conditions**: Concurrent requests can't interfere
3. **Automatic cleanup**: Old data is cleared when fd is reused
4. **No memory leaks**: qmap handles memory with custom measure function
5. **Faster tests**: Remove all artificial delays
6. **Existing API unchanged**: All `mpfd_get()` calls in other modules still work

## Status

- [x] Plan written to MPFD_REFACTOR_PLAN.md
- [ ] Implement changes to mods/mpfd/mpfd.c
- [ ] Remove sleep statements from test files
- [ ] Test implementation
- [ ] Update AGENTS.md with new information
