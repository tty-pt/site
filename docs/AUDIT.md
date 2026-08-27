# Audit — Technical Debt & Security Backlog

> **Directive for Agents:** When addressing an item from this backlog, create an active task with `/task <name>` before making code modifications.

This document serves as the authoritative, prioritized backlog of open technical debt, security hardening opportunities, and library improvements across the site and its external submodules (`axil`, `libxylem`, `hyle`, `bud`, `stoma`, `libqmap`).

---

## 1. Verified & Resolved Items (Historical Record)

Many historical findings from earlier audit iterations have been permanently addressed and verified by automated regression tests (`make test`) and boundary checks (`make boundary-check`). Do not reopen or re-implement these:

- **Auth & CSRF**:
  - Secure CSRF cookie parser, strict `SameSite=Lax` / `HttpOnly`, timing-safe comparison (`mods/auth/auth.c`).
  - Strict ID validation (`is_safe_id`) preventing traversal across all storage paths and route handlers.
  - Safe-origin redirect target validation in `libaxil-auth`.
  - Canonical ownership enforcement across all item mutations and dataset API endpoints (`source-http.c`).
- **Data Layer & Hyle / Bud Boundary**:
  - All item mutations route through `source_update_item` / `source_delete_item` and hyle `put`/`del` (`stoma_dirty` lifecycle).
  - Pure generic JS transports (`htdocs/*.js`): 0 site-specific identifiers or business logic; omni-dropdowns and action forms operate via generic progressive enhancement and `data-hyle-*` hooks.
  - Complete separation between `hyle` (pure schema/query, 0 DOM) and `bud` (pure UI, 0 DB), bridged solely by `libhyle-bud`.
  - Bounded dataset pagination (`per_page` clamped to 100 max in `source-http.c`).
  - `JSON` parsing in `bud` hardened with `jsmn` parser.
- **Multipart & Transport**:
  - Hardened `mpfd` multipart parser with exact Content-Length bounding, strict header validation, and OOB protection.
  - Safe bounds on `axil_env_get`, non-blocking read loop caps, and TLS termination safeguards.
  - `XY_ERR_NOTFOUND` handling and unique-inode shared object loading in `libxylem`.

---

## 2. Priority 1 — Site-Level Security & Auth Hardening

### 1.1 Gate `AUTH_SKIP_CONFIRM` to Development Environments
- **Location:** `start.sh:24`
- **Issue:** `export AUTH_SKIP_CONFIRM=1` is enabled unconditionally in `start.sh`, allowing unconfirmed registrations to auto-login.
- **Remediation:** Only export `AUTH_SKIP_CONFIRM=1` when explicitly passed or when `AUTH_ENV=dev`. In production boots, require real confirmation token flow.

### 1.2 Form-Based CSRF & Method Hardening for Auth Endpoints
- **Location:** `external/axil-auth/src/libaxil-auth.c:797-800`, `mods/auth/ux/login.c`, `register.c`
- **Issue:**
  - `/auth/logout` and `/auth/confirm` are registered without strict method prefixes, allowing GET requests to trigger logout (vulnerable to cross-site top-level GET logout CSRF).
  - Login and register endpoints lack CSRF tokens.
- **Remediation:**
  - Register `/auth/logout` strictly as `POST:/auth/logout` and update navigation to submit a POST form.
  - Add CSRF validation to `handle_login` and `handle_register`.

### 1.3 Strict Password Validation & Buffer Safety
- **Location:** `external/axil-auth/src/libaxil-auth.c:557,620,641`
- **Issue:** Password buffer of size 64 silently truncates passwords longer than 63 characters. Minimum password length is only 4 characters.
- **Remediation:** Expand password buffer to 128+ bytes, reject inputs exceeding buffer limits with HTTP 422, and enforce minimum password length of 8–12 characters.

### 1.4 Make `/api/song/prefs` Mutation POST+CSRF Only
- **Location:** `mods/song/song.c:121-148`, `mods/song/song.c:504-506`
- **Issue:** `GET:/api/song/prefs` allows query parameters to mutate user viewer preferences (`chords-bemol`, `chords-latin`, `chords-media`, zoom) without CSRF protection (`<img src="/api/song/prefs?z=50">` attack).
- **Remediation:** Restrict preference mutations to `POST:/api/song/prefs` with CSRF validation. Keep GET read-only or deprecate GET handler. Ensure isomorphic WASM and SSR preference controls dispatch via POST.

---

## 3. Priority 2 — Storage & Filesystem Safety

### 2.1 Atomic and Hardened File Writes (`write_file_path`)
- **Location:** `mods/common/common_storage.c:105-121`
- **Issue:** `write_file_path` opens files with `fopen(path, "w")` without `O_NOFOLLOW` or `fsync`. Sudden power loss or crashes during write could result in truncated files.
- **Remediation:** Write to a temporary file (`.tmp.<pid>`) with `O_CREAT|O_WRONLY|O_TRUNC|O_NOFOLLOW` (mode `0644`), `fsync`, and atomic `rename()` into place.

### 2.2 Path Prefix Check on Recursive Directory Removal
- **Location:** `mods/common/common_storage.c:268-305` (`item_remove_path_recursive`)
- **Issue:** `item_remove_path_recursive` accepts an arbitrary `item_path` without asserting that the resolved path is strictly within the application's `var/` subtree.
- **Remediation:** Assert that `realpath(item_path)` has the expected `var/<module>/` root prefix before traversing and unlinking directory entries.

---

## 4. Priority 3 — External Submodule Architectural Improvements

### 4.1 `axil`: Request Line & Heap Allocation Caps
- **Location:** `external/axil/src/libaxil.c:661-680`, `libaxil.c:2220`
- **Issue:**
  - Global `input` buffer grows dynamically on large request payloads but is never shrunk, retaining large heap allocations indefinitely.
  - URI length parsing lacks an explicit HTTP 414 URI Too Long check before decode operations.
- **Remediation:** Enforce maximum URI length (e.g. 4096 bytes) returning 414, and shrink or reset client connection buffers after serving large requests.

### 4.2 `libxylem`: Allocation Error Safety & Debug Log Cleanup
- **Location:** `external/libxylem/src/libxylem.c:1018-1079`, `libxylem-module.c:5`
- **Issue:**
  - `xy_bind` writes debug trace statements to `/tmp/xy_bind.log` on every module load in production builds.
  - Several memory allocations in internal table growth paths (`xy_deny`, `region_ensure_root`) do not fail closed.
- **Remediation:** Gate `/tmp/xy_bind.log` behind `#ifdef XY_DEBUG_LOG`, and ensure all allocator failures return `XY_ERR_ALLOC` to callers.

### 4.3 `hyle` / `stoma`: Incremental Indexing for FTS
- **Location:** `external/hyle/src/source.c:642-679`
- **Issue:** When any row in a dataset is inserted or updated, `stoma_dirty` triggers a complete rebuild of the entire full-text search index across all rows on the next query.
- **Remediation:** Implement single-row token eviction and re-indexing in `stoma` so mutations have $O(\text{tokens in item})$ update complexity instead of $O(\text{total dataset tokens})$.

### 4.4 `bud`: Dynamic Attribute & Tag Name Validation
- **Location:** `external/bud/src/libbud.c:373-380`
- **Issue:** Element tag names and attribute names are serialized without checking for forbidden delimiters or control characters (while attribute values and text content are properly escaped).
- **Remediation:** Add allowlist validation `^[A-Za-z_][A-Za-z0-9_-]*$` for element and attribute names in debug/test builds.

---

## 5. Priority 4 — Build System & Codebase Hygiene

### 5.1 Clean Up Orphaned `mods/site_core`
- **Location:** `mods/site_core/`
- **Issue:** `mods/site_core` is neither included in `mods.load` nor built in the top-level `Makefile`. It represents dead code superseded by `mods/common`.
- **Remediation:** Remove `mods/site_core/` from the repository or archive it under a dedicated reference directory.

### 5.2 Build-Time Asset Hashing for Cache Busting
- **Location:** `mods/common/ux/site_ui.c:28` (`SITE_CSS_V`)
- **Issue:** Cache busting versions for `styles.css` and `hyle.css` rely on manually bumped define strings (e.g. `?v=9`).
- **Remediation:** Compute a short content hash of `htdocs/styles.css` and `htdocs/hyle.css` during the build and emit it into `mods/common/ux/version.gen.h`.
