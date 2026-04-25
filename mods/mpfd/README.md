# mpfd - Multipart Form Data Parser

Parse `multipart/form-data` HTTP requests for file uploads.

## Overview

The mpfd module provides a parser for multipart form data, commonly used for HTML form file uploads. It extracts field names, filenames, and data from HTTP requests with `Content-Type: multipart/form-data`.

## Exported API

All functions are exported via the ndx system. Call them using `call_mpfd_*()` from other modules.

### mpfd_parse

```c
int mpfd_parse(int fd, char *body)
```

Parse a multipart form data request.

**Parameters:**
- `fd` - ndc file descriptor
- `body` - Request body (can be NULL for GET requests)

**Returns:**
- `0` - Success
- `-1` - Wrong content type (not multipart/form-data)
- `< 0` - Parse error

**Example:**
```c
int result = mpfd_parse(fd, body);
if (result == -1) {
    ndc_head(fd, 415);  // Unsupported Media Type
    return 1;
}
```

### mpfd_exists

```c
int mpfd_exists(const char *name)
```

Check if a field exists in the parsed data.

**Returns:** 1 if exists, 0 otherwise

### mpfd_len

```c
int mpfd_len(const char *name)
```

Get the length of a field's data.

**Returns:** Length in bytes, or 0 if field doesn't exist

### mpfd_get

```c
int mpfd_get(const char *name, char *buf, size_t len)
```

Copy field data into buffer.

**Returns:** Number of bytes copied, or 0 if field doesn't exist

**Example:**
```c
char id[64] = {0};
int len = mpfd_get("id", id, sizeof(id) - 1);
if (len > 0) {
    id[len] = '\0';
}
```

### mpfd_filename

```c
int mpfd_filename(const char *name, char *buf, size_t len)
```

Get the original filename from a file upload field.

**Returns:** Number of bytes copied, or 0 if no filename

### mpfd_save

```c
int mpfd_save(const char *name, const char *path)
```

Save field data directly to a file.

**Returns:** 0 on success, -1 on error

### mpfd_set_limits

```c
void mpfd_set_limits(size_t max_field_size, size_t max_total_size, size_t max_field_count)
```

Configure size limits for uploads.

**Example:**
```c
mpfd_set_limits(20 * 1024 * 1024,  // 20 MB per field
                     100 * 1024 * 1024, // 100 MB total
                     200);               // 200 fields max
```

## Default Limits

- **Max field size:** 10 MB (10,485,760 bytes)
- **Max total size:** 50 MB (52,428,800 bytes)
- **Max field count:** 100 fields

These can be changed via `mpfd_set_limits()` before parsing.

## Implementation Details

### Architecture

The module uses qmap for storing parsed field data:

1. **Custom Type Registration:** Uses `qmap_mreg()` to register a variable-size type for `mpfd_val` structures
2. **Memory Management:** qmap owns all value memory - never manually `free()` retrieved values
3. **Boundary Parsing:** Extracts boundary from Content-Type header and parses parts
4. **Quote Handling:** Properly handles quoted filenames with bounded search

### Data Structure

```c
struct mpfd_val {
    uint32_t len;           // Data length
    uint32_t filename_len;  // Filename length
    char data[];            // Variable-size: [data][filename]
};
```

The `data[]` field contains:
- Field data (len bytes)
- Filename string (filename_len bytes)

### Memory Management (Critical)

⚠️ **Important:** qmap manages all value memory internally.

**Correct usage:**
```c
const char *value = qmap_get(mpfd_db, "fieldname");
// Use value, but DON'T free it
```

**Incorrect usage (causes crashes):**
```c
const char *value = qmap_get(mpfd_db, "fieldname");
free(value);  // ❌ WRONG - qmap owns this memory
```

When calling `qmap_put()`:
```c
struct mpfd_val *val = malloc(sizeof(*val) + data_len);
// ... populate val ...
qmap_put(mpfd_db, key, val);
free(val);  // ✅ Correct - qmap made a copy
```

### Recent Bug Fix (March 2026)

Fixed critical use-after-free bug:
- **Problem:** Code called `free()` on qmap-managed values before `qmap_put()`
- **Solution:** 
  - Registered custom variable-size type via `qmap_mreg()`
  - Removed all manual `free()` calls on retrieved values
  - qmap now properly manages all memory for field data

See `mpfd.c:315-316` for type registration.

## Usage Example

From `mods/poem/poem.c`:

```c
// Parse the request
int parse_result = mpfd_parse(fd, body);
if (parse_result == -1) {
    ndc_header_set(fd, "Content-Type", "text/plain");
    ndc_head(fd, 415);
    ndc_body(fd, "Expected multipart/form-data");
    return 1;
}

// Check if field exists
if (!mpfd_exists("file")) {
    ndc_head(fd, 400);
    ndc_body(fd, "Missing file");
    return 1;
}

// Get field length
int file_len = mpfd_len("file");

// Allocate buffer and get data
char *content = malloc(file_len + 1);
int got = mpfd_get("file", content, file_len);
content[got] = '\0';

// Use content...
free(content);
```

## Dependencies

None - this is a standalone module.

## Used By

- `mods/poem/poem.c` - For poem file uploads

## Testing

No dedicated test file currently. Testing is performed via modules that use mpfd (e.g., poem module tests).

## Error Handling

The module tracks errors internally in `mpfd_error_buf[256]` for debugging, though these are not currently exposed via the API.

Callers should check return values:
- `mpfd_parse()` return value for parse success/failure
- `mpfd_exists()` before attempting to retrieve fields
- `mpfd_len()` to allocate correct buffer sizes
- `mpfd_get()` return value for actual bytes copied

## See Also

- [mods/poem/README.md](../poem/README.md) - Example usage for file uploads
- [AGENTS.md](../../AGENTS.md) - Memory management best practices
- `mpfd.c:34-39` - `mpfd_val_measure()` function for custom type
