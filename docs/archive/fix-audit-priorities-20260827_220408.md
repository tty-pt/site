# Task: Fix Priority Submodule & Security Debt (libxylem & axil-auth)

## Goal
Implement targeted security & quality fixes from `docs/AUDIT.md`:
1. **Priority 4.2 (`libxylem`)**: Gate debug `/tmp/xy_bind.log` logging behind `#ifdef XY_DEBUG_LOG` to eliminate world-writable log pollution during module binding.
2. **Priority 1.2 & 1.3 (`axil-auth`)**:
   - Password validation buffer size expansion (128 bytes) and minimum length enforcement (min 8 chars).
   - Strict `0700` and `0600` permissions on user directories and confirmation rcode files.
   - Enforce `POST` prefix on `/auth/logout` handler registration.

## Current Status
- [x] done

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered submodules build process (`make -C external/libxylem`, `make -C external/axil-auth`, `make`).
- [x] **2. Write Tests First**: Test auth flows and registration password checks with `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/auth-*.test.ts`.
- [x] **3. Feature Implementation**: Apply fixes to `external/libxylem` and `external/axil-auth`.
- [x] **4. Build & Run**: Rebuild submodules and verify zero compilation errors.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restart fresh server instance and run full `make test` suite (99/99 E2E tests, unit tests, sanitizer tests).

## Acceptance Criteria & Polish Checklist
- [x] No `/tmp/xy_bind.log` writes during normal operation.
- [x] Password validation supports 128-byte buffers and rejects passwords < 8 chars.
- [x] User dirs and rcode files created with `0700` / `0600`.
- [x] Full test suite (`make test`) passes 100% green.

## Remaining work
- [x] Stage 1: Clean Up `/tmp/xy_bind.log` in `external/libxylem`.
- [x] Stage 2: Password Buffer Expansion & File Permission Hardening in `external/axil-auth`.
- [x] Stage 3: Verification & Update `docs/AUDIT.md`.

## Next Recommended Step
1. Prompt user for task wrap-up flow options.
