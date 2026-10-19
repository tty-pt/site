# Test Suites

Comprehensive testing infrastructure covering unit tests, memory sanitizer matrices, SSR smoke tests, and Playwright end-to-end (E2E) browser tests.

## Test Types & Commands

| Suite | Command | Description |
|-------|---------|-------------|
| Fast Tests | `make test-fast` | Standalone unit tests, ASAN matrix tests, and pages smoke tests |
| Module Unit Tests | `make unit-tests` | Runs `test.sh` for each module under `mods/` |
| Matrix Tests | `make matrix-tests` | C memory/string contract fuzzing and matrix tests under ASAN/Valgrind |
| Page Smoke Tests | `make pages-test` | HTTP smoke test curling all top-level routes against `:8080` |
| End-to-End Tests | `make e2e-tests` | Playwright browser tests executed via Deno (requires server running) |
| Full Test Suite | `make test` | Executes all test suites sequentially |

## Targeted Testing During Development

To keep iteration loops fast, always run targeted tests for the area being modified:

```bash
# Single E2E test file
AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/song-add.test.ts

# Specific module test
make test-mod MOD=song

# Targeted fast suite
make test-fast
```

## Prerequisites

- **Server with `AUTH_SKIP_CONFIRM=1`**: E2E and unit tests require the Axil HTTP daemon running with `AUTH_SKIP_CONFIRM=1` enabled at server startup (`make dev` or `make watch` sets this automatically).
- **Deno**: Required for the Playwright E2E runner (`tests/e2e/*.test.ts`).

## Related Docs

- `docs/TESTING.md` — Detailed test conventions, assertions, and anti-flake guidelines.
- `debug/README.md` — Test log capture and diagnostics.
