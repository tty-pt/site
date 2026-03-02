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

## NDX Proxy APIs

The ssr module exports two NDX APIs that other C modules can use to forward requests to Deno SSR.

### call_ssr_proxy_get()

**Purpose:** Proxy a GET request to Deno SSR without modifications.

**Signature:**
```c
NDX_DEF(int, ssr_proxy_get, int, fd, const char *, path);
```

**Parameters:**
- `fd` - Client socket file descriptor
- `path` - Request path to forward (e.g., `/chords/test-transpose`)

**Returns:** 0 on success, -1 on error

**Usage:**
```c
const char *ndx_deps[] = { "./mods/ssr/ssr.so", NULL };

NDX_DEF(int, ssr_proxy_get, int, fd, const char *, path);

static int
my_handler(int fd, char *body)
{
    const char *id = getenv("PATTERN_PARAM_ID");
    char path[256];
    snprintf(path, sizeof(path), "/mymodule/%s%s", id, 
             getenv("QUERY_STRING") ?: "");

    return call_ssr_proxy_get(fd, path);
}
```

**When to use:**
- Simple pass-through to SSR rendering
- No data processing needed in C
- Dynamic routing based on parameters

### call_ssr_proxy_post()

**Purpose:** Proxy a POST request to Deno SSR with pre-processed data in the request body.

**Signature:**
```c
NDX_DEF(int, ssr_proxy_post, int, fd, const char *, path, 
        const char *, body_data, size_t, body_len);
```

**Parameters:**
- `fd` - Client socket file descriptor
- `path` - Request path to forward
- `body_data` - Request body data (processed content)
- `body_len` - Length of body data in bytes

**Returns:** 0 on success, -1 on error

**Usage:**
```c
const char *ndx_deps[] = { "./mods/ssr/ssr.so", NULL };

NDX_DEF(int, ssr_proxy_get, int, fd, const char *, path);
NDX_DEF(int, ssr_proxy_post, int, fd, const char *, path, 
        const char *, body_data, size_t, body_len);

static int
chords_ssr_handler(int fd, char *body)
{
    const char *id = getenv("PATTERN_PARAM_ID");

    // Read file from filesystem
    char filepath[512];
    snprintf(filepath, sizeof(filepath), "items/chords/items/%s/data.txt", id);

    FILE *fp = fopen(filepath, "r");
    if (!fp) {
        // File not found - let Deno handle 404
        char path[256];
        snprintf(path, sizeof(path), "/chords/%s", id);
        return call_ssr_proxy_get(fd, path);
    }

    // Read and process data
    fseek(fp, 0, SEEK_END);
    long len = ftell(fp);
    fseek(fp, 0, SEEK_SET);
    char *data = malloc(len + 1);
    fread(data, 1, len, fp);
    data[len] = '\0';
    fclose(fp);

    // Transform data (e.g., transpose chords)
    char *processed = transform_data(data, params);
    free(data);

    // Send processed data to Deno via POST
    char path[256];
    snprintf(path, sizeof(path), "/chords/%s", id);
    int ret = call_ssr_proxy_post(fd, path, processed, strlen(processed));

    free(processed);
    return ret;
}
```

**When to use:**
- Data transformation before rendering (e.g., chord transposition)
- Performance-critical operations in C
- Integration with C libraries
- Avoid FFI complexity in Deno

**Module receives body in render():**
```typescript
export async function render({ user, path, params, body }: {
  user: string | null;
  path: string;
  params: Record<string, string>;
  body?: string | null;  // POST body if sent from C handler
}) {
  let content;

  if (body) {
    // Use pre-processed data from C
    content = body;
  } else {
    // No processing, read file directly
    content = await Deno.readTextFile(filepath);
  }

  return React.createElement("div", null, content);
}
```

**Memory Management:**
- C module allocates and processes data
- `call_ssr_proxy_post()` sends data to Deno
- C module frees data after call returns
- No FFI, no shared memory, no deadlock risk

**Implementation Notes:**
- POST method used (not GET) to send body
- `Content-Length` header set automatically
- No `Content-Type` header added (module can add if needed)
- Request buffer size: 8192 bytes (headers + partial body)
- Body sent separately after headers

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
