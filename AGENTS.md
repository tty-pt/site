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
- `module.db` - qmap database with module metadata (routes, titles, etc.)
- `mods.load` - List of module .so files to load at startup

### How SSR Works

1. ndc receives HTTP request on port 8080
2. ssr.so intercepts via registered routes (defined in module.db)
3. ssr.so forwards request to Deno SSR (port 3000) with X-Modules header
4. Deno loads appropriate module SSR component (e.g., mods/auth/ssr/index.tsx)
5. Module's `render()` returns React element or null
6. React rendered to string, returned to ssr.so
7. ssr.so parses HTTP response and forwards to client

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

## Module System

### Adding a Module

Use `scripts/mod_add` to add a module:

```sh
./scripts/mod_add <modulename>
```

This:
- Appends the module's `.so` path to `mods.load`
- Adds an entry to `module.db` with title and SSR path (if SSR exists)

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
   - Add to `mods.load` and `module.db` using `./scripts/mod_add <modname>`

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

# View module database
qmap -l module.db
```

---

## File Locations

| Path | Purpose |
|------|---------|
| `mods/*/` | Module directories |
| `mods/*/*.c` | C module source |
| `mods/*/ssr/*.tsx` | SSR React components |
| `module.db` | Module metadata database |
| `mods.load` | List of .so files to load |
| `scripts/mod_add` | Adds module to system |
| `build.mk` | Common build rules |
| `start.sh` | Starts ndc + Deno SSR |
