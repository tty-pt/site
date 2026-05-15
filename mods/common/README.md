# common - Shared Utilities

Shared utility functions used across multiple modules.

## Overview

The common module provides foundational utilities that are used by multiple other modules, including URL parameter parsing, cookie extraction, JSON escaping, and URL encoding.

## Exported API

All functions are exported via the ndx system. Call them using `call_<function_name>()` from other modules.

### query_param

Parse URL-encoded query parameters from a query string.

```c
int query_param(char *query, const char *key, char *out, size_t out_len)
```

**Parameters:**
- `query` - Query string (e.g., from POST body or URL query)
- `key` - Parameter name to find
- `out` - Output buffer
- `out_len` - Size of output buffer

**Returns:**
- `0` on success
- `-1` if parameter not found or invalid arguments

**Features:**
- URL decoding: `+` → space, `%XX` → character
- Handles multiple parameters separated by `&`
- Extracts value up to next `&` or end of string

**Example:**
```c
char username[64] = {0};
int result = query_param(body, "username", username, sizeof(username));
if (result == 0) {
    // username contains the decoded value
}
```

**Used by:**
- `mods/auth/auth.c` - Parse login/register form data

---

### get_cookie

Extract the `QSESSION` cookie value from an HTTP Cookie header.

```c
int get_cookie(const char *cookie, char *token, size_t len)
```

**Parameters:**
- `cookie` - Cookie header value
- `token` - Output buffer for session token
- `len` - Size of output buffer

**Returns:**
- `0` on success (token extracted or cookie not found)
- `-1` on error (NULL cookie or buffer)

**Features:**
- Searches for `QSESSION=` in cookie string
- Handles multiple cookies separated by `;` or `&`
- Null-terminates the extracted token

**Example:**
```c
char cookie[256] = {0};
char token[64] = {0};

ndc_env_get(fd, cookie, "HTTP_COOKIE");
get_cookie(cookie, token, sizeof(token));

if (token[0]) {
    // Session token found
}
```

**Used by:**
- `mods/ssr/ssr.c` - Extract session for authentication forwarding

---

### json_escape

Escape a string for safe inclusion in JSON output.

```c
int json_escape(const char *in, char *out, size_t outlen)
```

**Parameters:**
- `in` - Input string
- `out` - Output buffer for escaped string
- `outlen` - Size of output buffer

**Returns:**
- `0` on success

**Escapes:**
- `"` → `\"`
- `\` → `\\`
- `\n` → `\\n`
- `\r` → `\\r`
- `\t` → `\\t`
- Control characters (< 0x20) → `\uXXXX`

**Example:**
```c
const char *title = "Hello \"World\"";
char escaped[256];
json_escape(title, escaped, sizeof(escaped));
// Result: Hello \"World\"
```

**Used by:**
- `mods/ssr/ssr.c` - Escape module titles for X-Modules JSON header

---

### url_encode

URL-encode a string (percent encoding).

```c
int url_encode(const char *in, char *out, size_t outlen)
```

**Parameters:**
- `in` - Input string
- `out` - Output buffer for encoded string
- `outlen` - Size of output buffer

**Returns:**
- `0` on success

**Encoding:**
- Alphanumeric characters (`a-zA-Z0-9`) → unchanged
- Unreserved characters (`-_.~`) → unchanged
- All others → `%XX` (uppercase hex)

**Example:**
```c
const char *str = "Hello World!";
char encoded[256];
url_encode(str, encoded, sizeof(encoded));
// Result: Hello%20World%21
```

**Used by:**
- `mods/ssr/ssr.c` - URL-encode X-Modules header (though ultimately uses JSON, so this may be redundant)

## Implementation Details

**Source:** `common.c`

- Lines 11-32: `get_cookie()` - Cookie extraction
- Lines 34-76: `query_param()` - URL parameter parsing
- Lines 78-107: `json_escape()` - JSON string escaping
- Lines 109-121: `url_encode()` - URL encoding

**No dependencies:** This module is foundational and has no external dependencies.

**NDX hooks:**
- `ndx_install()` - Empty (no handlers to register)
- `ndx_open()` - Empty (no initialization needed)

## Dependencies

None - this is a foundational module.

## Used By

- **mods/auth/auth.c** - Uses `query_param()` to parse form data
- **mods/ssr/ssr.c** - Uses `get_cookie()`, `json_escape()`, `url_encode()` for header building

## Testing

No dedicated test file currently. Testing is performed via modules that use these utilities (auth and ssr tests).

## Design Rationale

### Why a Common Module?

Originally, these utilities were duplicated across modules:
- `query_param` in auth.c
- `get_cookie`, `json_escape`, `url_encode` in ssr.c

The common module:
- Eliminates code duplication
- Provides single source of truth
- Makes utilities available to all modules
- Simplifies maintenance

### Buffer Safety

All functions include buffer overflow protection:
- Check output buffer size before writing
- Null-terminate output strings
- Return error codes for invalid arguments

### URL Decoding Implementation

The `query_param()` function handles URL decoding inline:

```c
// '+' becomes space
if (out[i] == '+') {
    out[j++] = ' ';
}
// '%XX' becomes character
else if (out[i] == '%' && out[i+1] && out[i+2]) {
    int c;
    sscanf(out + i + 1, "%2x", &c);
    out[j++] = c;
    i += 2;
}
```

This single-pass approach is efficient and handles malformed input gracefully.

## Usage Example

From `mods/auth/auth.c`:

```c
// In handle_login()
char username[64] = {0};
char password[64] = {0};
char redirect[256] = {0};

// Parse form-encoded POST body
query_param(body, "username", username, sizeof(username));
query_param(body, "password", password, sizeof(password));
query_param(body, "ret", redirect, sizeof(redirect));

// username and password now contain decoded values
```

From `mods/ssr/ssr.c`:

```c
// Extract session token from cookie
char cookie[256] = {0};
char token[64] = {0};
ndc_env_get(fd, cookie, "HTTP_COOKIE");
get_cookie(cookie, token, sizeof(token));

// Look up username
const char *username = get_session_user(token);

// Escape for JSON
char title_esc[256];
json_escape(title, title_esc, sizeof(title_esc));
```

## Edge Cases Handled

### query_param
- Empty query string → returns -1
- Key not found → returns -1
- Multiple identical keys → returns first match
- Malformed percent encoding → skips invalid sequences

### get_cookie
- NULL cookie header → sets token to empty string, returns 0
- QSESSION not found → sets token to empty string, returns 0
- Multiple cookies → extracts QSESSION value

### json_escape
- Control characters → Unicode escape `\uXXXX`
- Buffer too small → truncates safely
- NULL input → writes empty string

### url_encode
- Reserved characters → percent encoded
- Unicode/UTF-8 → encodes as bytes (each byte becomes %XX)
- Buffer too small → truncates safely

## Performance Notes

- **In-place decoding:** `query_param()` decodes in the output buffer (single pass)
- **No allocations:** All functions use caller-provided buffers
- **Minimal copying:** String processing is efficient with pointer arithmetic

## See Also

- [mods/auth/README.md](../auth/README.md) - Uses query_param
- [mods/ssr/README.md](../ssr/README.md) - Uses get_cookie, json_escape, url_encode
- [AGENTS.md](../../AGENTS.md) - Development guidelines

## Dataset System

The common module also includes the dataset system for loading and querying item data.

### Overview

The dataset system provides a unified way to load item data across modules. It uses:
- `source_hd` - A qmap that stores cached JSON for each item
- `dataset_scan_item()` - Reads item files from disk and populates the cache
- `dataset_rows_json_build()` - Builds JSON array of all items

### Data Flow

```
index_hd (qmap) ← populated at startup by index_open
       ↓
dataset_def.source_hd = index_hd
       ↓
dataset_rows_json_build iterates over source_hd
       ↓
For each ID, looks up json_val from source_hd
       ↓
If json_val is empty, calls dataset_scan_item to read from disk
```

### Key Functions

#### dataset_scan_item()
Reads item files from disk and populates the source_hd cache.

```c
static int dataset_scan_item(const dataset_def_t *def, const char *id);
```

**Parameters:**
- `def` - Dataset definition containing items_path and field definitions
- `id` - Item identifier

**Behavior:**
1. Builds path: `{doc_root}/{def->items_path}/{id}/`
2. For each field with a file, reads the file content
3. Creates JSON object with id and field values
4. Stores JSON in `def->source_hd` via qmap_put

#### dataset_rows_json_build()
Builds JSON array of all items in a dataset.

```c
static int dataset_rows_json_build(
        const dataset_def_t *def, const char *include, char **out_json);
```

**Parameters:**
- `def` - Dataset definition
- `include` - Optional comma-separated list of extra fields to include from disk
- `out_json` - Output parameter for allocated JSON string

**Behavior:**
1. Iterates over all IDs in `def->source_hd`
2. For each ID with non-empty JSON, adds to output array
3. For IDs with empty JSON, calls `dataset_scan_item` then re-checks
4. If `include` is specified, reads additional files from disk

### Adding Debug Output

To debug dataset loading issues, add fprintf statements:

```c
fprintf(stderr, "DEBUG dataset_rows: id=%s json_val=%s\n", id, json_val ? json_val : "NULL");
```

Check output in `debug/runtime/ndc.log` (server must be running via `make watch`).

### Common Issues

1. **Item not appearing in dataset**
   - Check if item ID is in `source_hd` (qmap iteration)
   - Verify item directory exists on disk at `{items_path}/{id}/`
   - Check if `dataset_scan_item` successfully reads files

2. **Empty JSON for existing item**
   - The inline scan in `dataset_rows_json_build` should handle this
   - Verify `dataset_scan_item` is populating the cache correctly
   - Check file permissions on item directory

3. **Index not populated at startup**
   - Songs use `index_hd` which is populated by `index_open`
   - New items added to disk after startup need `dataset_scan_item` to be queried
