# ssr - Server-Side Rendering Proxy

HTTP proxy that forwards requests to Deno SSR server for React rendering.

## Overview

The ssr module acts as a bridge between ndc (the C HTTP server) and a Deno-based SSR server that renders React components. It intercepts HTTP requests, forwards them to Deno, and returns the rendered HTML.

## Architecture

```
Client → ndc (port 8080) → ssr.c → Deno SSR (port 3000) → React → HTML
         └─ C modules      └─ Proxy  └─ server.ts      └─ index.tsx
```

### Request Flow

1. Client sends HTTP request to ndc on port 8080
2. C modules handle registered routes (e.g., `/poem/add` POST)
3. Unmatched routes fall through to `ssr.c`
4. `ssr.c` reads `module.db` to discover available modules
5. Builds `X-Modules` header with module metadata (JSON)
6. Forwards request to Deno SSR on localhost:3000
7. Deno loads appropriate module SSR components
8. Module `render()` returns React element
9. React rendered to HTML string
10. Deno returns HTTP response
11. `ssr.c` parses response and forwards to client via ndc

## Components

### ssr.c (C Proxy)

**Purpose:** Intercept and proxy requests to Deno SSR

**Key Functions:**
- `build_modules_json()` - Read module.db and build JSON array
- `ssr_handler()` - Main proxy handler
- `parse_http_response()` - Parse upstream HTTP response
- `get_field()` - Parse module.db field values

**Configuration:**
- `PROXY_HOST`: `127.0.0.1`
- `PROXY_PORT`: `3000`

**Module Discovery:**
Reads `module.db` (qmap) to build JSON like:
```json
[
  {
    "id": "auth",
    "title": "Auth",
    "routes": [],
    "ssr": "mods/auth/ssr/index.tsx"
  },
  {
    "id": "poem",
    "title": "Poem",
    "routes": [],
    "ssr": "mods/poem/ssr/index.tsx"
  }
]
```

This JSON is URL-encoded and sent in the `X-Modules` header.

**Authentication Integration:**
Extracts session from cookie and calls `get_session_user()` to get username, forwarding it in the `X-Remote-User` header.

### server.ts (Deno SSR)

**Purpose:** Render React components to HTML

**Listening on:** `http://localhost:3000`

**Process:**
1. Read `X-Modules` header (URL-decode, JSON parse)
2. Read `X-Remote-User` header for current user
3. Match request path against module routes
4. Dynamically import matching module: `mods/{id}/ssr/index.tsx`
5. Call module's `render()` function with context
6. Render React element to HTML string
7. Wrap in full HTML document with layout
8. Return HTTP response

**Route Matching:**
Uses `matchRoute()` to support parameterized routes:
- `/poem` matches `/poem`
- `/poem/:id` matches `/poem/my-poem` with `params.id = "my-poem"`

**Index Page:**
If path is `/`, renders a module listing with buttons linking to each module.

### ui.tsx (Shared Layout)

**Purpose:** Common page layout and navigation

**Exports:** `Layout` component

**Features:**
- Top menu with hamburger toggle
- Navigation links (Home, Login, Register)
- User-specific menu items when logged in
- Responsive design with mobile menu

## Module SSR Interface

Modules provide SSR rendering via `mods/{id}/ssr/index.tsx`.

### Required Exports

#### routes

Array of route patterns the module handles:

```typescript
export const routes = ["/myroute", "/myroute/:id", "/myroute/add"];
```

Supports:
- Exact paths: `/poem`
- Parameterized paths: `/poem/:id`
- Trailing slashes are normalized

#### render

Async function that returns a React element (or null):

```typescript
export async function render({ 
  user, 
  path, 
  params 
}: { 
  user: string | null; 
  path: string; 
  params: Record<string, string> 
}) {
  if (path === "/myroute") {
    return React.createElement("div", null, "Hello");
  }
  return null;
}
```

**Parameters:**
- `user` - Current username (or null if not logged in)
- `path` - Request path (e.g., `/poem/my-poem`)
- `params` - Route parameters (e.g., `{id: "my-poem"}`)

**Returns:**
- React element to render
- `null` if the module doesn't handle this path

### Example Module

```typescript
import React from "https://esm.sh/react@18";

export const routes = ["/example", "/example/:id"];

export async function render({ user, path, params }: {
  user: string | null;
  path: string;
  params: Record<string, string>
}) {
  if (path === "/example") {
    return React.createElement("div", { className: "center" },
      React.createElement("h1", null, "Example Module"),
      user ? `Logged in as ${user}` : "Not logged in"
    );
  }

  if (params.id) {
    return React.createElement("div", null, 
      `Viewing item: ${params.id}`
    );
  }

  return null;
}
```

## Dependencies

- `mods/auth/auth` - For `get_session_user()` to check authentication
- `mods/common/common` - For `json_escape()` and `url_encode()` utilities

## Configuration

### Environment Variables

The Deno server can be configured via environment:

- `DENO_DIR` - Deno cache directory
- Listening port is hardcoded to 3000

### Module Database

The `module.db` file must exist and contain module metadata. Created by:

```sh
./scripts/mod_add <module_name>
```

Format (qmap key:value pairs):
```
module_id:title=Title; routes=; ssr=path/to/ssr/index.tsx; be=path/to/module.so
```

## Starting the SSR Server

### Manual Start

```sh
cd mods/ssr
deno run --allow-net --allow-read --allow-env server.ts
```

### Via start.sh

The repository's `start.sh` script starts both ndc and Deno SSR:

```sh
./start.sh
```

This:
1. Kills any existing ndc/deno processes
2. Starts Deno SSR in background (port 3000)
3. Starts ndc in foreground (port 8080)

## Implementation Details

**C Proxy:** `ssr.c`
- Lines 20-38: `get_field()` - Parse module.db values
- Lines 44-97: `build_modules_json()` - Build X-Modules header
- Lines 281-511: `ssr_handler()` - Main proxy logic
- Caches `modules_db_hd` for performance

**Deno Server:** `server.ts`
- Lines 14-30: `getModules()` - Parse X-Modules header
- Lines 46-64: `matchRoute()` - Route matching with params
- Lines 82-144: `handleRequest()` - Main request handler

## Testing

**Unit Tests:** `./test.sh` or `make -C mods/ssr test`

Basic SSR proxy tests (content TBD).

**Integration:** `tests/pages/10-pages-render.sh`

Smoke tests that verify pages render:
- Root page (/)
- Poem listing (/poem)
- Login page (/login)
- Register page (/register)

## Error Handling

The proxy includes comprehensive error handling:

- Invalid upstream responses (non-HTTP format)
- Connection failures to Deno
- Parse errors in X-Modules header
- Module import failures (Deno logs error, returns 500)

Errors are logged to stderr/console.

## Performance Considerations

- `module.db` is opened once and cached (`modules_db_hd`)
- Deno caches imported modules (no reloading on each request)
- Socket connections to Deno are short-lived (new connection per request)

**Future optimization:** Keep persistent connection pool to Deno.

## Troubleshooting

### SSR not responding / 502 errors

**Causes:**
1. Deno SSR not running
2. Deno crashed
3. Port 3000 already in use

**Debug:**
```sh
# Check if Deno is running
pgrep -a deno

# Check port 3000
lsof -i :3000

# Restart Deno
cd mods/ssr
deno run --allow-net --allow-read --allow-env server.ts
```

### Module not appearing in listing

**Causes:**
1. Not registered in `module.db`
2. Invalid SSR path in module.db
3. SSR file doesn't exist

**Debug:**
```sh
# List all modules
qmap -l module.db

# Check specific module
qmap -g poem module.db

# Verify SSR file exists
ls -l mods/poem/ssr/index.tsx
```

### X-Modules header too large

**Cause:** Too many modules or very long route lists.

**Solution:** Headers are limited by HTTP implementation. If exceeded, consider:
- Reducing route list size
- Compressing JSON
- Using alternative module discovery method

### Module render() errors

**Symptom:** 500 error when accessing module route.

**Debug:** Check Deno console output for import errors or runtime exceptions in the module's `render()` function.

## See Also

- [mods/auth/README.md](../auth/README.md) - Authentication integration
- [mods/common/README.md](../common/README.md) - Shared utilities
- [mods/poem/README.md](../poem/README.md) - Example module with SSR
- [docs/MODULE_DEVELOPMENT.md](../../docs/MODULE_DEVELOPMENT.md) - Creating new modules
- [AGENTS.md](../../AGENTS.md) - Development guidelines
