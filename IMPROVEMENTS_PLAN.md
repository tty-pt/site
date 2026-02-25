# Improvements Plan

## Overview

This document outlines a plan to simplify the codebase while maintaining the same functionality.

---

## Current Issues

### 1. Duplicate Session Mechanisms
- `auth.c` stores sessions in `auth.qmap` (table "sess")
- `ssr.c` stores sessions as files in `sessions/` directory
- These are incompatible - ssr.c cannot see sessions created by auth.c

### 2. Duplicate Utility Functions
- `get_cookie`, `json_escape`, `url_encode`, `get_field` in `ssr.c`
- `query_param` in `auth.c`
- These should be shared

### 3. Repeated module.db Opens
- `ssr.c` calls `qmap_open("./module.db"...)` in multiple places without caching

### 4. Unused Tailwind Build Files
- `tailwind.config.js`, `TAILWIND_MAPPING.md`, `mods/ssr/input.css` are not needed
- Compiled CSS already exists in `htdocs/styles.css`

### 5. Unused mod.db Files
- `mods/ssr/mod.db`, `mods/poem/mod.db`, `mods/mpfd/mod.db` exist but are unused
- Only root `module.db` is used

---

## Responsibility Split

### Common Module (mods/common/)
Generic utilities shared across modules:
- `query_param` - URL parameter parsing
- `json_escape` - JSON string escaping
- `url_encode` - URL encoding

### Auth Module (mods/auth/)
Auth-specific functionality:
- `get_session_user` - session lookup in auth.qmap
- `get_auth_db` - access auth.qmap
- `get_cookie` - parse cookie header (moved from ssr)
- All current auth handlers (login, register, logout, confirm)

### SSR Module (mods/ssr/)
- `get_field` - parse module.db values (stays here)
- Uses common utilities (`json_escape`, `url_encode`)
- Uses auth's `get_session_user()` and `get_cookie()`

---

## Dependency Table

| Module | Current Deps | New Deps |
|--------|-------------|----------|
| common | - | [] |
| auth | [ssr] | [common] |
| ssr | [] | [auth, common] |
| mpfd | [] | [] |
| index_api | [] | [] |
| poem | [ssr, mpfd] | [ssr, mpfd] |

---

## Implementation Phases

### Phase 1: Create Common Module
Create `mods/common/` with shared utilities:
- `mod.json`
- `Makefile`
- `common.h` - header with declarations
- `common.c` - implementations

### Phase 2: Update Auth Module
Modify `mods/auth/`:
- Add dependency on common
- Move `query_param` to common (update to use from common)
- Add `get_auth_db()` with caching
- Add `get_session_user()` 
- Add `get_cookie()` (moved from ssr)

### Phase 3: Update SSR Module
Modify `mods/ssr/`:
- Add dependencies on auth and common
- Remove duplicate functions (use from common/auth)
- Remove `sessions/` file handling - use auth's session lookup
- Add cached module.db handle

### Phase 4: Update Dependencies
Update mod.json files:
- `mods/ssr/mod.json`: deps = ["auth", "common"]
- `mods/auth/mod.json`: deps = ["common"]

### Phase 5: Remove Unused Files
Delete:
- `tailwind.config.js`
- `TAILWIND_MAPPING.md`
- `mods/ssr/input.css`
- `mods/ssr/mod.db`
- `mods/poem/mod.db`
- `mods/mpfd/mod.db`
- `sessions/` directory (if exists)

### Phase 6: Rebuild
```sh
make module.db
make clean && make test
```

---

## Files to Create/Modify/Delete

| Action | Files |
|--------|-------|
| CREATE | `mods/common/common.h`, `mods/common/common.c`, `mods/common/Makefile`, `mods/common/mod.json` |
| MODIFY | `mods/ssr/ssr.c`, `mods/auth/auth.c`, `mods/ssr/mod.json`, `mods/auth/mod.json` |
| DELETE | `tailwind.config.js`, `TAILWIND_MAPPING.md`, `mods/ssr/input.css`, `mods/ssr/mod.db`, `mods/poem/mod.db`, `mods/mpfd/mod.db`, `sessions/` |

---

## Implementation Order

```
1. Create mods/common/ with utilities
2. Update mods/auth/mod.json (add common dep)
3. Update mods/ssr/mod.json (add auth, common deps)
4. Modify mods/auth/auth.c
5. Modify mods/ssr/ssr.c
6. Delete unused files
7. make module.db
8. make clean && make test
```

---

## Phase 1 Details: Create Common Module

### mods/common/mod.json
```json
{
  "id": "common",
  "title": "Common",
  "deps": [],
  "routes": [],
  "ssr": "",
  "be": "mods/common/common.so"
}
```

### mods/common/Makefile
```makefile
include ../../build.mk
```

### mods/common/common.h
```c
#ifndef COMMON_H
#define COMMON_H

#include <stddef.h>

void query_param(char *query, const char *key, char *out, size_t out_len);
void json_escape(const char *in, char *out, size_t outlen);
void url_encode(const char *in, char *out, size_t outlen);

#endif
```

### mods/common/common.c
```c
#include <ttypt/ndc.h>
#include <ttypt/ndx.h>
#include <ttypt/qmap.h>
#include <ctype.h>
#include <string.h>
#include <stdio.h>

#include "common.h"

void query_param(char *query, const char *key, char *out, size_t out_len) {
    if (!out || !out_len)
        return;
    out[0] = 0;
    if (!query)
        return;

    size_t key_len = strlen(key);
    for (char *p = query; *p; ) {
        while (*p == '&')
            p++;
        if (!strncmp(p, key, key_len) && p[key_len] == '=') {
            char *val = p + key_len + 1;
            size_t n = 0;
            while (val[n] && val[n] != '&')
                n++;
            if (n >= out_len)
                n = out_len - 1;
            memcpy(out, val, n);
            out[n] = 0;

            size_t j = 0;
            for (size_t i = 0; out[i]; i++) {
                if (out[i] == '+') {
                    out[j++] = ' ';
                } else if (out[i] == '%' && out[i+1] && out[i+2]) {
                    int c;
                    sscanf(out + i + 1, "%2x", &c);
                    out[j++] = c;
                    i += 2;
                } else {
                    out[j++] = out[i];
                }
            }
            out[j] = 0;
            return;
        }
        while (*p && *p != '&')
            p++;
    }
}

void json_escape(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 2 < outlen; i++) {
        unsigned char c = (unsigned char)in[i];
        if (c == '"' || c == '\\') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = c;
        } else if (c == '\n') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = 'n';
        } else if (c == '\r') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = 'r';
        } else if (c == '\t') {
            if (j + 2 >= outlen) break;
            out[j++] = '\\';
            out[j++] = 't';
        } else if (c < 0x20) {
            if (j + 6 >= outlen) break;
            j += snprintf(out + j, outlen - j, "\\u%04x", c);
        } else {
            out[j++] = c;
        }
    }
    out[j] = '\0';
}

void url_encode(const char *in, char *out, size_t outlen) {
    size_t j = 0;
    for (size_t i = 0; in[i] && j + 4 < outlen; i++) {
        unsigned char c = (unsigned char)in[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out[j++] = c;
        } else {
            j += snprintf(out + j, outlen - j, "%%%02X", c);
        }
    }
    out[j] = '\0';
}
```

---

## Expected Results

- **Single source of truth** for sessions (qmap via auth)
- **Shared utilities** in common module
- **Faster requests** from cached module.db
- **Cleaner build** without unused files
- **No circular dependencies**

---

## Verification

After implementing changes, run:
```sh
make clean && make test
```

All tests should pass with the same functionality.
