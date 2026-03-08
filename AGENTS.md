# Agent Development Guide

This document provides guidelines for AI coding agents working on this NDC + Deno SSR site.

## Project Overview

Hybrid web application:
- **NDC HTTP server** (C) on port 8080 - handles HTTP, auth, sessions, file uploads
- **Fresh framework** (Deno/TypeScript/Preact) on port 3000 - renders UI components
- **Proxy**: Client → NDC:8080 → proxy.c → Fresh:3000 → Preact

**Stack:** qmap, ndx, ndc, Fresh 1.7.3, Preact 10, Tailwind CSS 3.4

## Build Commands

```bash
make                    # Build all C modules + CSS
make clean              # Remove build artifacts (*.so files)
make run                # Build and start both servers
npm run build:css       # Build Tailwind CSS
deno task start         # Start Fresh dev server (auto-creates hard links)
./start.sh              # Start NDC server (port 8080)
```

## Test Commands

```bash
make test               # Run all tests (unit + pages)
make unit-tests         # Run all module test.sh scripts
cd mods/poem && ./test.sh   # Run single module test
make pages-test         # Quick SSR rendering smoke tests
make integration-tests  # Run integration tests (requires server)
sh tests/integration/01-auth-poem-flow.sh  # Single integration test
```

**Pre-commit hook:**
```bash
ln -s ../../.githooks/pre-commit .git/hooks/pre-commit
```

## Code Style - TypeScript/TSX

### Imports
```tsx
// Use Preact (NOT React)
import { Handlers, PageProps } from "$fresh/server.ts";

// Use @/ alias for mods/ imports
import { Layout } from "@/ssr/ui.tsx";

// Islands use islands/ path (after hard-linking)
import TransposeControls from "../../islands/TransposeControls.tsx";

// Middleware state
import type { State } from "../../_middleware.ts";

// Deno std with JSR imports
import { dirname, fromFileUrl, resolve } from "@std/path";
```

### Type Annotations
- **Always type** function parameters and return types
- Use `interface` for component props
- Use `Record<string, string>` for dynamic objects
- Mark optional fields with `?`

```tsx
interface ModuleEntry {
  id: string;
  title: string;
  flags?: number;
}

function MyComponent({ user }: { user: string | null }) {
  return <div>{user}</div>;
}
```

### Formatting
- Double quotes for strings
- 2-space indentation (not tabs)
- Be consistent with semicolons within files

### Error Handling
```tsx
try {
  const data = await Deno.readTextFile(path);
  return <Component data={data} />;
} catch (e) {
  console.error("Failed to read file:", e);
  return null;
}
```

### Minimal Route Files
For simple index/add pages, derive module from URL automatically:
```tsx
// Just re-export - module name derived from file path
export { handler, default } from "@/index/IndexList.tsx";
// or
export { handler, default } from "@/index/IndexAdd.tsx";
```

## Code Style - C Modules

### Indentation
- **Use tabs** (not spaces)
- Order includes: system, then local headers

```c
#include <stdio.h>
#include <string.h>
#include <ttypt/ndc.h>
#include "../common/common.h"
```

### Memory Management (CRITICAL!)
**NEVER free() qmap-managed values!** qmap owns the memory.
```c
// WRONG
char *value = qmap_get(map, key);
free(value);

// CORRECT - let qmap manage
qmap_put(map, key, new_value);
```

### Error Handling (CRITICAL!)
**Always check syscall return values!**
```c
if (mkdir(path, 0755) && errno != EEXIST) {
    ndc_head(500);
    ndc_body("Failed to create directory");
    free(allocated_memory);
    return;
}
```

### Naming
- Functions/structs/variables: `snake_case`
- Macros/constants: `UPPER_CASE`

### Exported Functions
Use `NDX_DEF` macro:
```c
NDX_DEF(const char *, get_session_user, const char *, token) {
    if (!token || !*token) return NULL;
    return qmap_get(sessions_map, token);
}
```

## Fresh + C Integration Pattern

### Handler Registration
Register handlers in `ndx_install()`:
```c
void ndx_install(void) {
    ndc_register_handler("GET:/song/:id", song_details_handler);
    ndc_register_handler("GET:/song/:id/edit", song_edit_get_handler);
    ndc_register_handler("POST:/song/:id/edit", song_edit_post_handler);
}
```

### C → Fresh Proxy Pattern (Edit Pages)
For edit forms, C reads files then POSTs to Fresh:

**1. C GET handler reads files:**
```c
static int song_edit_get_handler(int fd, char *body) {
    // Read title, data.txt from filesystem
    char title[256] = {0};
    FILE *tfp = fopen(path, "r");
    if (tfp) { fgets(title, sizeof(title), tfp); fclose(tfp); }
    
    // Build URL-encoded POST body
    char post_body[4096];
    snprintf(post_body, "title=%s&data=%s", url_encode(title), url_encode(data));
    
    // POST to Fresh
    return call_core_post(fd, post_body, strlen(post_body));
}
```

**2. Fresh handler receives POST body:**
```tsx
export const handler: Handlers<EditData, State> = {
    async POST(req, ctx) {
        const body = await req.text();
        const { title, data } = parseBody(body); // URLSearchParams
        return ctx.render({ user: ctx.state.user, title, data });
    },
    async GET(req, ctx) {
        const body = await req.text();
        const { title, data } = parseBody(body);
        return ctx.render({ title, data });
    },
};
```

**3. Form posts back to C for saving:**
```tsx
<form method="POST" action={`/song/${id}/edit`}>
```

## Fresh Hard Link Architecture

- Source in `mods/<module>/routes/` → hard linked to `routes/<module>/`
- Source in `mods/<module>/islands/` → hard linked to `islands/`
- Run `deno task start` or `deno task setup-routes` to regenerate

**Module URL path = directory name** (e.g., `songbook` module → `/songbook/` routes)

## Common Pitfalls

1. DON'T free qmap-managed memory - causes crashes
2. ALWAYS check syscall return values
3. Create required directories: `mkdir -p items/poem/items items/song/items items/songbook/items`
4. DON'T commit build artifacts: *.so, mods.load, swap files
5. Run `make test` after EVERY change
6. Fresh routes take precedence over `[...slug]` catch-all

## Troubleshooting

- **Module not loading?** Check `cat mods.load` and verify .so exists
- **SSR 502 errors?** Check Deno: `pgrep -a deno` and port 3000: `lsof -i :3000`
- **Fresh routes missing?** Run `deno task setup-routes`
- **Hard link verification:** `stat` both files - inodes should match
- **C handler not called?** Check ndc logs for registered handlers
