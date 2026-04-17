# Agent Development Guide

## Architecture

Hybrid web application:
- **NDC HTTP server** (C) on port 8080 — handles HTTP, auth, sessions, file uploads, all business logic
- **Fresh/Deno** (TypeScript/Preact) on port 3000 — renders HTML only, never fetches data
- **Proxy**: Browser → NDC:8080 → `proxy.c` → Fresh:3000

**NDC manages Deno internally** via `mods/ssr/ssr.c`. Do NOT start Deno separately. NDC forks `deno task serve` on startup.

**Stack:** qmap, ndx, ndc, Fresh 1.7.3, Preact 10, Tailwind CSS 3.4

## The cardinal rule: Deno/Fresh never makes requests

Fresh only *receives* requests — it never calls `fetch()`, reads files, or queries databases.

- **C GET handler** reads filesystem, builds a POST body, calls `call_core_post(fd, body, len)` to proxy to Fresh
- **Fresh POST handler** receives that body, parses it, calls `ctx.render(data)`
- **Browser form** POSTs directly to NDC (C), never to Fresh
- **Fresh GET handler** is only for rendering forms that don't need data (e.g. `/song/add`); redirect to login if unauthenticated

Islands (`fetch()` calls in browser JS) are the **only** exception — they call NDC endpoints directly, never Fresh.

### Correct pattern for data pages

```c
// C: read files, POST body to Fresh
static int my_handler(int fd, char *body) {
    char post_body[4096];
    snprintf(post_body, sizeof(post_body), "title=%s&data=%s", ...);
    return call_core_post(fd, post_body, strlen(post_body));
}
```

```tsx
// Fresh: only POST handler — receives C-built body
export const handler: Handlers<Data, State> = {
  async POST(req, ctx) {
    const body = await req.text();
    const params = new URLSearchParams(body);
    return ctx.render({ title: params.get("title") ?? "" });
  },
};
```

```tsx
// Form targets NDC, not Fresh
<form method="POST" action={`/song/${id}/edit`}>
```

### Correct pattern for add/form pages (no data needed)

```tsx
// Fresh: only GET handler
export const handler: Handlers<AddData, State> = {
  GET(req, ctx) {
    if (!ctx.state.user) return Response.redirect(...);
    return ctx.render({ user: ctx.state.user });
  },
};
```

## Build & Run

```bash
make                    # Build all C .so modules + CSS
make clean              # Remove *.so build artifacts
deno task start         # (dev) setup hard links + watch mode
```

**Starting servers (for tests/production):**
```bash
cd /home/quirinpa/site
setsid ndc -C . -p 8080 -d >> /tmp/site.log 2>&1 &
# NDC spawns Deno internally; wait for "deno ready" in /tmp/site.log
```

## Chroot Setup

The site runs inside a chroot at the repo root. `ndx_install()` in `mods/auth/auth.c` creates the necessary `./etc/` files automatically on first start, but the shell and its libraries must be set up manually once per deployment.

### Linux

`./etc/nsswitch.conf` is written automatically by `ndx_install()` on first start — no manual step needed.

Copy `sh` and its shared libraries into the chroot:

```bash
mkdir -p ./bin ./lib ./lib/x86_64-linux-gnu
cp /bin/sh ./bin/sh

# Mirror every library listed by ldd
ldd /bin/sh
# e.g. copy /lib/x86_64-linux-gnu/libc.so.6 → ./lib/x86_64-linux-gnu/libc.so.6
#          /lib64/ld-linux-x86-64.so.2       → ./lib64/ld-linux-x86-64.so.2

# NSS files resolver (needed for getpwnam() to read ./etc/passwd)
cp /lib/x86_64-linux-gnu/libnss_files.so.2 ./lib/x86_64-linux-gnu/
```

Paths vary by distro — use `ldd /bin/sh` and `ldd /lib/x86_64-linux-gnu/libnss_files.so.2` to find all transitive deps.

`./etc/passwd`, `./etc/shadow`, `./etc/group` are maintained automatically by the auth module.

### OpenBSD

Run once after initial deployment to bootstrap `pwd.db`/`spwd.db` from the existing `master.passwd`:

```bash
pwd_mkdb -d /path/to/site/etc /path/to/site/etc/master.passwd
```

`pwd_mkdb` is called automatically by `handle_register()` on every new registration, so this one-time step is only needed to cover pre-existing users.

Copy `sh` and its libraries the same way as Linux, adjusting paths for OpenBSD conventions.

## Testing

```bash
make test                          # unit tests + pages smoke tests
make unit-tests                    # all mods/*/test.sh
cd mods/poem && ./test.sh          # single module
make pages-test                    # SSR smoke tests (requires servers)
make e2e-tests                     # full Playwright suite (requires servers)
# Run one e2e file at a time to avoid timeout:
deno test --allow-all tests/e2e/song-add.test.ts
```

**e2e prerequisites:** NDC must be running on :8080, and `/tmp/site.log` must be writable (tests poll it for auth confirmation rcodes). Run tests one file at a time (≤60s each) to avoid bash timeout.

**Pre-commit hook:**
```bash
ln -s ../../.githooks/pre-commit .git/hooks/pre-commit
```

## C Module Conventions

### Redirect pattern (CRITICAL)
Every `303` redirect **must** set `Connection: close` + `DF_TO_CLOSE` **before** `ndc_head`. Wrong order causes HTTP pipelining hangs:

```c
ndc_header(fd, "Location", location);
ndc_header(fd, "Connection", "close");
ndc_set_flags(fd, DF_TO_CLOSE);   // MUST be before ndc_head
ndc_head(fd, 303);
ndc_close(fd);
return 0;
```

`DF_TO_CLOSE = 8` — defined in `/usr/include/ttypt/ndc.h`.

### Form parsing
- Use `call_query_parse` + `call_query_param` for `application/x-www-form-urlencoded` forms
- Use `call_mpfd_parse` + `call_mpfd_get` only for `multipart/form-data` (file uploads)
- Never use `call_mpfd_parse` for plain form fields — prefer url-encoded + `call_query_parse`

```c
// url-encoded form
call_query_parse(body);
char title[256] = {0};
call_query_param("title", title, sizeof(title) - 1);

// multipart (file uploads only)
call_mpfd_parse(fd, body);
call_mpfd_get("file", buf, buflen);
```

### Memory management (CRITICAL)
**Never `free()` qmap-managed values.** qmap owns that memory.

```c
// WRONG — crash
char *val = qmap_get(map, key);
free(val);

// CORRECT — qmap manages it
qmap_put(map, key, new_value);
```

### Exported functions
Use `NDX_DEF` macro; declare with `NDX_DECL` in the corresponding `.h`:

```c
NDX_DEF(const char *, get_session_user, const char *, token) { ... }
```

### Handler registration
```c
void ndx_install(void) {
    ndx_load("./mods/index/index");   // load dependencies first
    ndc_register_handler("GET:/song/:id", song_details_handler);
    ndc_register_handler("POST:/song/:id/edit", song_edit_post_handler);
}
```

### Style
- Tabs for indentation
- System includes first, then local headers
- `snake_case` for functions/vars, `UPPER_CASE` for macros

## TypeScript/TSX Conventions

### Imports
```tsx
import { Handlers, PageProps } from "$fresh/server.ts";
import { Layout } from "@/ssr/ui.tsx";        // @/ = mods/
import type { State } from "#/routes/_middleware.ts";  // #/ = repo root
import { join } from "@std/path";
```

### Route structure
- Simple list page: `export { handler, default } from "@/index/IndexList.tsx";`
- Simple add page: `export { handler, default } from "@/index/IndexAdd.tsx";`
- Module-specific pages need their own handler + component

### Auth state
User is available via `ctx.state.user` (set by `_middleware.ts` from `X-Remote-User` header, which NDC sets from cookie lookup). Auth cookie is `QSESSION`.

### Hard link architecture
- Source: `mods/<module>/routes/` → hard linked to `routes/<module>/`
- Source: `mods/<module>/islands/` → hard linked to `islands/`
- Regenerate: `deno task setup-routes`
- Verify: `stat` both files — inodes must match

**Module URL path = directory name** (`songbook` module → `/songbook/` routes)

## Common Pitfalls

1. Fresh makes NO outbound requests — all data comes from C via POST body
2. Every C `303` redirect needs `Connection: close` + `ndc_set_flags(fd, DF_TO_CLOSE)` before `ndc_head`
3. Don't use `call_mpfd_parse` for url-encoded forms — use `call_query_parse`
4. Don't `free()` qmap-managed values
5. `IndexList.tsx` POST handler: use `.filter(Boolean)` before `.pop()` to handle trailing-slash URLs
6. Don't commit build artifacts: `*.so`, `mods.load`, swap files
7. Required data dirs: `mkdir -p items/poem/items items/song/items items/songbook/items items/choir/items`

## Troubleshooting

- **Module not loading?** Check `cat mods.load` and verify `.so` exists
- **SSR 502?** Check `/tmp/deno-ssr.log`; verify port 3000: `lsof -i :3000`
- **Fresh routes missing?** Run `deno task setup-routes`
- **C handler not called?** Check `/tmp/site.log` for registered handler lines
- **Pipelining hang after redirect?** Missing `Connection: close` + `DF_TO_CLOSE` before `ndc_head(fd, 303)`
- **Only ONE NDC + ONE Deno process at a time:** `kill -9 $(lsof -ti:8080) $(lsof -ti:3000) 2>/dev/null`

## NDC internals: POST body buffering

NDC's `ndc_read()` returns as soon as a `recv()` call returns fewer bytes than `BUFSIZ`. On loopback, HTTP headers often arrive in one TCP segment and the body in a second — so handlers can receive a `body` pointer pointing past the end of the actually-received data, into stale `input` buffer content from the previous request.

**Fix (applied in `../ndc/src/libndc.c`):** After `headers_get()` in `request_handle()`, a loop reads from the socket until `Content-Length` bytes of body are present. This makes all POST handlers — especially `multipart/form-data` to pattern routes — reliable.

**Symptom of regression:** `call_mpfd_parse` returns 0 but `call_mpfd_get("title", ...)` returns -1; body first bytes show stale data from previous request rather than the multipart boundary.

### `fresh.gen.ts` must be updated for new routes

Fresh uses a static manifest (`fresh.gen.ts`) in serve mode. Adding a new route file requires manually adding an import and entry there. Only `dev.ts` auto-regenerates it.

### `qmap_put` takes ownership — never `free()` the pointer

`qmap_put(map, key, ptr)` takes ownership of `ptr`. Calling `free(ptr)` afterwards is a use-after-free crash.

### `ndc_register_handler` is last-registration-wins

Registering two handlers for the same path: the second wins.

## e2e Test Status

Tests in `tests/e2e/`. Run with `make e2e-tests` or one file at a time.

All 13 tests pass when servers are running.
