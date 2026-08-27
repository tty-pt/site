# Task: Unauthorized Login Redirect (`unauthorized-login-redir`)

## Goal
When an unauthenticated user navigates to a protected page requiring login (such as `/song/a/edit`, `/poem/p/edit`, etc.), the 401 login page should include a redirect (`ret`) back to the requested page (including query parameters if present), so that upon logging in, the user is redirected back to the page they originally intended to visit.

## Current Status
- Completed and verified. All unit, smoke, and 100/100 E2E tests pass.

## Why this matters
Previously, hitting an unauthorized protected page rendered a 401 login form without passing the requested URI into the `ret` parameter, resulting in the user being redirected to `/` instead of returning to what they were doing.

## Architectural & Code Findings
1. In `external/axil-auth/src/libaxil-auth.c`:
   - `require_login(int fd, const char *username)` calls `on_auth_login_error(fd, 401, "Login required", "")`.
   - If `redirect` is empty `""`, `on_auth_login_error` in `mods/auth/auth.c` previously passed empty `redirect` to `auth_render_login(user, redirect, msg)`.
2. In `mods/auth/auth.c`:
   - `on_auth_login_error(int fd, int status, const char *msg, const char *redirect)`:
     - When `redirect` is `NULL` or empty `""`, inspect `DOCUMENT_URI` and `QUERY_STRING` from the request environment (`axil_env_get(fd, uri, sizeof(uri), "DOCUMENT_URI")` and `axil_env_get(fd, qs, sizeof(qs), "QUERY_STRING")`).
     - If `DOCUMENT_URI` is present and is not an internal auth endpoint (`/auth/login`, `/auth/logout`, `/auth/register`), construct `target = uri` (or `uri?qs` if query string is non-empty).
     - Pass `target` to `auth_render_login`.
3. In `mods/auth/ux/login.c`:
   - `auth_render_login(user, ret, error)` puts `<input type="hidden" name="ret" value="{ret}">` if `ret && ret[0]`.
4. In `external/axil-auth/src/libaxil-auth.c`:
   - `handle_login`: Parses `ret` parameter, validates with `redirect_target(redirect_path)`, and passes `ret` to `login_as`, which calls `on_auth_login_ok(fd, username, target)` to redirect to the original target.

## Decisions made
- In `on_auth_login_error` (`mods/auth/auth.c`), when `redirect` is NULL or empty, inspect `DOCUMENT_URI` and optional `QUERY_STRING` from the request environment (`axil_env_get`).
- If `DOCUMENT_URI` is present and is not an internal auth endpoint (`/auth/login`, `/auth/logout`, `/auth/register`), construct the full redirect target (`uri` or `uri?qs`) and pass it as `target` to `auth_render_login`.
- This ensures the 401 login page renders `<input type="hidden" name="ret" value="...">` pre-populated with the requested URI so the user returns to the page after login.

## Files touched
- `docs/current/unauthorized-login-redir.md`
- `mods/auth/auth.c`
- `tests/e2e/login-unauthorized-redir.test.ts`

## Remaining work
- None. Fully implemented and verified with tests.
