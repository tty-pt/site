# Agent Notes

This is the development documentation for the site. It uses ndc + ndx + qmap + Deno SSR.

For the reference implementation, see [../tty.pt](../tty.pt).

## Table of Contents

1. [Library Documentation & Headers](#library-documentation--headers)
2. [Architecture](#architecture)
3. [Code Style](#code-style)
4. [Module System](#module-system)
5. [SSR System](#ssr-system)
6. [Common Development Tasks](#common-development-tasks)
7. [Testing](#testing)
8. [Troubleshooting](#troubleshooting)
9. [Useful Commands](#useful-commands)

---

## Library Documentation & Headers

| Library | README | Headers |
|---------|--------|---------|
| ndc | `../ndc/README.md` | `../ndc/include/ttypt/ndc.h`, `../ndc/include/ttypt/ndc-ndx.h` |
| ndx | `../ndx/README.md` | `../ndx/include/ttypt/ndx.h` |
| qmap | `../qmap/README.md` | `../qmap/include/ttypt/qmap.h` |

---

## Architecture

### Request Flow

```
Client → ndc (port 8080) → ssr.so (C proxy) → Deno SSR (port 3000) → React → HTML
```

### Key Components

- `mods/ssr/ssr.c` - C proxy that forwards HTTP requests to Deno SSR, parses responses
- `mods/ssr/server.ts` - Deno SSR server that renders React pages
- `mods.load` - List of module .so files to load at startup

### How SSR Works

1. ndc receives HTTP request on port 8080
3. ssr.so forwards request to Deno SSR (port 3000) with X-Modules header
4. Deno loads appropriate module SSR component (e.g., mods/auth/ssr/index.tsx)
5. Module's `render()` returns React element or null
6. React rendered to string, returned to ssr.so
7. ssr.so parses HTTP response and forwards to client

### Server-Side Processing Pattern (POST Body)

For modules that need to pre-process data before SSR (e.g., chord transposition), use this pattern:

**Architecture:**
```
Client → ndc → C handler → process data → POST to Deno SSR → React render → HTML
```

**Pattern:**
1. C module registers URL pattern handler (e.g., `GET:/chords/:id`)
2. Handler extracts parameters using `getenv("PATTERN_PARAM_NAME")`
3. Handler processes data (e.g., file I/O, transformation)
4. Handler calls `call_ssr_proxy_post(fd, path, body_data, body_len)` with processed data
5. Deno SSR receives POST with processed data in request body
6. SSR component checks for `body` parameter in `render()` function
7. If body present, use it; otherwise read/fetch data normally

**Benefits:**
- Clean memory management (C frees after POST)
- No FFI complexity or permissions needed
- Server-side processing in performant C code
- Deno focuses on rendering, not data processing

**Example (chords module):**

C handler (`mods/chords/chords.c`):
```c
const char *ndx_deps[] = { "./mods/ssr/ssr.so", NULL };

NDX_DEF(int, ssr_proxy_get, int, fd, const char *, path);
NDX_DEF(int, ssr_proxy_post, int, fd, const char *, path, const char *, body_data, size_t, body_len);

static int
chords_ssr_handler(int fd, char *body)
{
    const char *id = getenv("PATTERN_PARAM_ID");

    // Read and process file
    char *data = read_file(id);
    char *transposed = transpose_data(data, semitones);

    // Send processed data to Deno SSR via POST
    call_ssr_proxy_post(fd, path, transposed, strlen(transposed));

    free(data);
    free(transposed);
    return 0;
}

MODULE_API void
ndx_install(void)
{
    ndc_register_handler("GET:/chords/:id", chords_ssr_handler);
}
```

SSR component (`mods/chords/ssr/index.tsx`):
```tsx
export async function render({ user, path, params, body }: { 
  user: string | null; 
  path: string; 
  params: Record<string, string>;
  body?: string | null;
}) {
  let content;

  if (body) {
    // Use pre-processed data from C handler
    content = body;
  } else {
    // No processing needed, read directly
    content = await Deno.readTextFile(filepath);
  }

  return React.createElement("div", null, content);
}
```

**When to use this pattern:**
- Data transformation (transposition, formatting)
- Performance-critical operations (large file processing)
- Integration with C libraries
- Avoid FFI complexity in Deno

**When NOT to use:**
- Simple file serving (use proxy GET)
- No processing needed (use proxy GET)
- Pure UI logic (keep in React)

---

## Code Style

### C Modules

- K&R style with tabs for indentation
- Include order: standard library → local headers → ttypt headers
- Required functions:
  - `MODULE_API void ndx_install(void)` - called at install time
  - `MODULE_API void ndx_open(void)` - called at runtime
  - `MODULE_API ndx_t *get_ndx_ptr(void)` - returns &ndx (required)
- Use `MODULE_API` for function exports
- Include `"papi.h"` for module private declarations
- Declare dependencies via `const char *ndx_deps[]`

Example:
```c
#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>

#include "papi.h"

const char *ndx_deps[] = { "./mods/ssr/ssr.so", NULL };

ndx_t ndx;

MODULE_API void
ndx_install(void)
{
    ndc_register_handler("GET:/path", handler);
}

MODULE_API ndx_t *
get_ndx_ptr(void)
{
    return &ndx;
}
```

### TypeScript/React (SSR)

- React 18 with `React.createElement` or JSX
- Deno imports from esm.sh and deno.land
- SSR components export:
  - `routes` - array of route strings
  - `render()` - async function returning React element or null

Example:
```tsx
export const routes = ["/route", "/route/:param"];

export async function render({ user, path, params }: { user: string | null; path: string; params: Record<string, string> }) {
  if (path === "/route") {
    return React.createElement("div", null, "Hello");
  }
  return null;
}
```

---

## Error Handling Best Practices

All C modules must follow these patterns to avoid silent failures and provide meaningful error messages.

### System Call Error Checking

**Always check return values** from system calls and handle errors appropriately.

#### mkdir() - Directory Creation

```c
#include <errno.h>

if (mkdir(path, 0755) == -1 && errno != EEXIST) {
    // Clean up any allocated resources first
    free(allocated_memory);
    
    // Return descriptive HTTP error
    ndc_header(fd, "Content-Type", "text/plain");
    ndc_head(fd, 500);
    ndc_body(fd, "Failed to create directory");
    return 1;
}
```

**Key points:**
- Check for `-1` return value
- Allow `EEXIST` (directory already exists is OK)
- Free allocated memory before returning
- Return HTTP 500 with descriptive message

#### fopen() - File Operations

```c
FILE *fp = fopen(path, "w");
if (!fp) {
    free(allocated_memory);  // Clean up first
    ndc_header(fd, "Content-Type", "text/plain");
    ndc_head(fd, 500);
    ndc_body(fd, "Failed to write file");
    return 1;
}

// Use file...
fwrite(data, 1, len, fp);
fclose(fp);
```

**Key points:**
- Check for NULL return
- Clean up resources before returning
- Always `fclose()` when done

### Memory Management with qmap

**Critical rules** for using qmap:

#### Rule 1: Never free() qmap-managed values

```c
// ✅ CORRECT
const char *value = qmap_get(db, "key");
// Use value, but DON'T free it - qmap owns this memory

// ❌ WRONG - causes use-after-free crashes
const char *value = qmap_get(db, "key");
free((void*)value);  // NEVER DO THIS
```

#### Rule 2: Use custom types for variable-size data

For structures with variable-size data, register a custom type:

```c
struct my_val {
    uint32_t len;
    char data[];  // Variable-size
};

// Measurement function
static size_t
my_val_measure(const void *data)
{
    const struct my_val *val = data;
    return sizeof(struct my_val) + val->len;
}

// In ndx_install():
uint32_t my_type = qmap_mreg(my_val_measure);
db = qmap_open("my.db", "rw", QM_STR, my_type, 0xFF, 0);
```

#### Rule 3: Free temporary structs after qmap_put()

```c
// Allocate temporary struct
struct my_val *val = malloc(sizeof(*val) + data_len);
val->len = data_len;
memcpy(val->data, data, data_len);

// qmap makes a copy
qmap_put(db, key, val);

// ✅ Free your temporary struct (qmap has its own copy)
free(val);
```

### Error Handling Pattern

Standard error handling flow:

```c
static int
my_handler(int fd, char *body)
{
    char *buffer = NULL;
    
    // Validate input
    if (!body || !*body) {
        ndc_header(fd, "Content-Type", "text/plain");
        ndc_head(fd, 400);
        ndc_body(fd, "Missing request body");
        return 1;
    }
    
    // Allocate resources
    buffer = malloc(1024);
    if (!buffer) {
        ndc_header(fd, "Content-Type", "text/plain");
        ndc_head(fd, 500);
        ndc_body(fd, "Memory allocation failed");
        return 1;
    }
    
    // Perform operations
    if (mkdir(path, 0755) == -1 && errno != EEXIST) {
        free(buffer);  // Clean up
        ndc_header(fd, "Content-Type", "text/plain");
        ndc_head(fd, 500);
        ndc_body(fd, "Failed to create directory");
        return 1;
    }
    
    FILE *fp = fopen(path, "w");
    if (!fp) {
        free(buffer);  // Clean up
        ndc_header(fd, "Content-Type", "text/plain");
        ndc_head(fd, 500);
        ndc_body(fd, "Failed to write file");
        return 1;
    }
    
    // Success path
    fwrite(buffer, 1, 1024, fp);
    fclose(fp);
    free(buffer);
    
    ndc_header(fd, "Location", "/success");
    ndc_head(fd, 303);
    ndc_close(fd);
    return 0;
}
```

### HTTP Response Codes

Use appropriate HTTP status codes:

| Code | Meaning | When to Use |
|------|---------|-------------|
| 200 | OK | Successful GET/POST with response body |
| 303 | See Other | Successful POST, redirect to result page |
| 400 | Bad Request | Invalid input (missing fields, malformed data) |
| 401 | Unauthorized | Authentication required or failed |
| 405 | Method Not Allowed | Wrong HTTP method (e.g., GET on POST-only endpoint) |
| 415 | Unsupported Media Type | Wrong Content-Type (e.g., expected multipart/form-data) |
| 500 | Internal Server Error | System errors (mkdir, fopen, malloc failures) |

### Examples from Recent Bug Fixes

#### mpfd Module (March 2026)

**Problem:** Use-after-free bug from calling `free()` on qmap values.

**Fix:** See `mods/mpfd/mpfd.c`
- Lines 34-39: Added `mpfd_val_measure()` function
- Line 315: Registered custom type with `qmap_mreg()`
- Removed all manual `free()` calls on retrieved values

#### poem Module (March 2026)

**Problem:** Silent failures when directory didn't exist or file writes failed.

**Fix:** See `mods/poem/poem.c`
- Line 6: Added `#include <errno.h>`
- Lines 66-72: Check `mkdir()` return, return HTTP 500 on error
- Lines 74-82: Check `fopen()` return, return HTTP 500 on error
- Clean up allocated memory before all error returns

### Debugging Tips

**Check return values:**
```c
int ret = system_call();
fprintf(stderr, "system_call returned: %d, errno: %d (%s)\n", 
        ret, errno, strerror(errno));
```

**Log errors to stderr:**
```c
fprintf(stderr, "[ERROR] Failed to create %s: %s\n", path, strerror(errno));
```

**Use descriptive error messages:**
```c
// ❌ Bad
ndc_body(fd, "Error");

// ✅ Good
ndc_body(fd, "Failed to create poem directory");
```

---

## Module System

### Module Documentation

Each module should have a `README.md` documenting:
- Purpose and functionality
- API/endpoints with parameters and responses
- SSR routes (if applicable)
- Dependencies on other modules
- Storage requirements (directories, databases)
- Testing approach and test file location
- Usage examples with code snippets

See existing module READMEs for examples:
- [mods/auth/README.md](mods/auth/README.md)
- [mods/chords/README.md](mods/chords/README.md)
- [mods/poem/README.md](mods/poem/README.md)
- [mods/mpfd/README.md](mods/mpfd/README.md)
- [mods/ssr/README.md](mods/ssr/README.md)
- [mods/common/README.md](mods/common/README.md)

### Directory Requirements

Some modules require specific directories to exist before they can function:

| Module | Required Directory | Purpose | Failure Mode |
|--------|-------------------|---------|--------------|
| poem | `items/poem/items/` | Store uploaded poems | HTTP 500 on upload |
| chords | `items/chords/items/` | Store uploaded chord charts | HTTP 500 on upload |

**Setup:**
```sh
mkdir -p items/poem/items items/chords/items
```

**Important:** Tests may delete these directories during cleanup. Always recreate them after running tests, or use separate test directories.

**Future improvement:** Add module init hooks or setup script to create required directories automatically.

### Adding a Module

Use `scripts/mod_add` to add a module:

```sh
./scripts/mod_add <modulename>
```

This:
- Appends the module's `.so` path to `mods.load`

Module must be located at `mods/<modulename>/<modulename>.so`

### Building Modules

Each module has its own Makefile that includes the root build.mk:

```makefile
# mods/modname/Makefile
EXTRA_LDLIBS += -lextralib
include ../../build.mk
```

Available variables:
- `EXTRA_CFLAGS` - additional C compiler flags
- `EXTRA_LDLIBS` - additional libraries to link

---

## SSR System

### Adding SSR Pages

1. Create `mods/modname/ssr/index.tsx` with:
   - `routes` array listing handled routes
   - `render()` function returning React element

2. Add module to system:
   - Ensure module .so is built
   - Add to `mods.load` using `./scripts/mod_add <modname>`

### SSR Component Pattern

```tsx
import React from "https://esm.sh/react@18";

export const routes = ["/myroute", "/myroute/:id"];

export async function render({ user, path, params }: { 
  user: string | null; 
  path: string; 
  params: Record<string, string> 
}) {
  if (path === "/myroute") {
    return React.createElement("div", null, "Hello world");
  }
  return null;
}
```

### Styling (tty.pt style)

This site aims to match the visual style of [../tty.pt](../tty.pt). The reference implementation uses:

- **Minimal inline CSS** - No CSS framework, just simple inline styles
- **Module listing**: Homepage displays available modules as buttons
- **Shared Layout**: `mods/ssr/ui.tsx` with common header/menu

Reference implementation: see `../tty.pt/ssr/server.ts` and `../tty.pt/ssr/ui.tsx`

---

## Common Development Tasks

### Adding a New C Module

1. Create directory: `mods/newmod/`
2. Create `newmod.c` with ndx hooks:
   ```c
   #include <ttypt/ndc.h>
   #include <ttypt/ndx.h>
   #include <ttypt/qmap.h>
   #include "papi.h"
   
   ndx_t ndx;
   
   MODULE_API void ndx_install(void) {
       ndc_register_handler("GET:/newmod", handler);
   }
   
   MODULE_API ndx_t *get_ndx_ptr(void) {
       return &ndx;
   }
   ```
3. Create `Makefile`: `include ../../build.mk`
4. Build: `make -C mods/newmod`
5. Add to system: `./scripts/mod_add newmod`
6. Add test in `test.sh`

### Modifying SSR Pages

1. Edit `mods/modname/ssr/index.tsx`
2. Restart Deno SSR (or reload if auto-restart configured)
3. Test at route URL

### Working with qmap Databases

```sh
# List all entries
qmap -l database.db

# Get value by key
qmap -g key database.db

# Put key:value pair
qmap -p "key:value" database.db

# Reverse get (find key by value)
qmap -rg value database.db
```

Note: Use absolute paths in handlers (build from DOCUMENT_ROOT).

---

## Testing

### Run All Tests

```sh
make test
```

This runs:
1. Module unit tests (`make unit-tests`)
2. Page smoke tests (`make pages-test`)

### Unit Tests Only

```sh
make unit-tests
```

### Page Smoke Tests Only

```sh
make pages-test
```

This starts the site in background, runs smoke tests, then stops services.

### Manual Testing

```sh
# Start site
./start.sh

# Or manually:
ndc -C . -p 8080 -d

# Test endpoint
curl http://127.0.0.1:8080/your/endpoint
```

---

## Troubleshooting

### Module fails to load

- Check `ndx_deps` array points to correct paths
- Verify .so file exists and was built: `ls mods/*/*.so`
- Run `make` in module directory to rebuild

### SSR not responding

- Ensure Deno SSR is running on port 3000
- Check Deno process: `pgrep -a deno`
- Check logs in /tmp

### qmap errors

- Check file permissions
- Use absolute paths in handlers
- Verify database file exists

### Build errors

- Verify include paths in build.mk match sibling lib locations
- Ensure ../ndc, ../ndx, ../qmap are built and installed
- Check compiler: `which clang`

---

## Useful Commands

```sh
# Build everything
make all

# Build and run all tests
make test

# Add a module to the system
./scripts/mod_add <modulename>

# Start site
./start.sh

# Start site manually (if Deno already running)
ndc -C . -p 8080 -d

# Run only module unit tests
make unit-tests

# Run page smoke tests
make pages-test

# Run integration tests
make integration-tests

# Clean build artifacts
make clean

# List built modules
cat mods.load
```

---

## File Locations

| Path | Purpose |
|------|---------|
| `mods/*/` | Module directories |
| `mods/*/*.c` | C module source |
| `mods/*/ssr/*.tsx` | SSR React components |
| `mods.load` | List of .so files to load |
| `scripts/mod_add` | Adds module to system |
| `build.mk` | Common build rules |
| `start.sh` | Starts ndc + Deno SSR |
