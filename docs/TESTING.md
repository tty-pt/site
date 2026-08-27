# Testing — commands, prereqs, known failures, debug

How to run the test suites, what the server must look like, what fails
pre-existing, and how to write e2e assertions that don't flake.

## Iteration rule: targeted tests first

While iterating on a change, run **only the tests that cover it** — never the
full suite mid-task (it takes ~2–3 minutes and floods output):

```bash
# one e2e file
AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/song-type.test.ts
# a few related files
deno test --allow-all tests/e2e/picker-omni.test.ts tests/e2e/gig-zoom.test.ts
```

Pick files by feature area (picker → `picker-*`, `song-type`, `gig-*`;
list/search → `song-search*`, `song-ssr-filter`; hydration → `bud-hydration`).
Run the **full** `make test` once, as the final quality gate before declaring a
task complete.

## Commands

| Command | What |
|---------|------|
| `make unit-tests` | Runs `test.sh` in each module directory + standalone C unit tests (`run-dsv-legacy.sh`). Needs axil on :8080. |
| `make matrix-tests` | Runs C cross-library memory/matrix tests under ASAN. |
| `make pages-test` | Page-render HTTP smoke test (curls :8080 for DOCTYPE/markup). |
| `make e2e-tests` | Playwright via Deno. Needs server + `AUTH_SKIP_CONFIRM=1`. |
| `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<file>.test.ts` | Single e2e file execution. |
| `DEBUG=1 AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<file>.test.ts` | Single e2e file with WASM/DOM lookahead and browser console diagnostics. |
| `make hyle-tests` | cargo test on the `external/hyle` Rust workspace. |
| `make -C external/hyle test` | hyle C unit suite (incl. zig-test step — see below). |

## Server prerequisites

- unit/pages/e2e all hit `http://localhost:8080` — start axil first
  (`docs/BUILD.md`). `AUTH_SKIP_CONFIRM=1` must be set **at server startup**
  (not just at test time) — the auth module reads it on boot.

## Pre-existing failures (not caused by local changes)

- `start.sh` and `scripts/watch.sh` enable `AUTH_SKIP_CONFIRM=1` in dev
  mode. This is a development convenience, not production behavior.
- `make -C external/hyle test` `zig-test` step fails when `zig` is not
  installed (unrelated to the C suite).
- `tests/pages/20-song-search.sh` expects the "No items" empty state: a
  valid-but-zero FTS result renders `<p class="text-muted">No items</p>` (the
  deliberate zero-row empty state) instead of a `0 of 0 rows` pagination line;
  `row_count` maps that marker to `0 of 0 rows`.
- `make hyle-tests` / `make integration-tests` are not part of `make test` — run manually.

## Debug logging & WASM/DOM Diagnostics

All logs captured to `debug/`:

```
debug/builds/    make output with timestamps
debug/runtime/   axil.log (via make watch)
debug/tests/     e2e output with timestamps
```

```bash
make debug-logs            # recent logs tail
make build-capture         # rebuild + save log
make test-capture          # run e2e + save
make test-single-capture TEST=foo.ts
fprintf(stderr, "DEBUG: %s\n", val);   # C debug — check debug/runtime/axil.log
```

### Diagnosing WASM vs DOM Hydration Mismatches

When Playwright E2E tests fail during hydration or WASM state initialization:

1. **Isolate the single failing test**:
   `DEBUG=1 AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<file>.test.ts`
2. **Check for WASM LinkError imports**:
   Look for `LinkError: WebAssembly.instantiate(): Import ... function import requires a callable`. This indicates native C symbols (like `json-c`) leaked into WASM builds because of missing `#ifndef __wasm__` preprocessor guards in UX/component files.
3. **Inspect WASM vs DOM node count diffs**:
   Compare the WASM lookahead tree vs DOM lookahead tree in the `[error] WASM/SSR mismatch` log output. For example, if WASM renders `<select>` or `<div class="hyle-picker-empty">` while DOM renders 50 `<label class="hyle-picker-option">` elements, check:
   - Was `bud-state` JSON serialized correctly on the native SSR side?
   - Did `hyle_bud_picker_state_from_json` or WASM state deserializer fail due to struct field memory alignment mismatches (`pick_view_t` vs `hyle_bud_picker_view_t`)?
   - Is `__thread` used inside UX code compiled for WASM? (Remove `__thread` from WASM build paths as WebAssembly does not support thread-local storage).
4. **Stale Server Process Check**:
   If C shared libraries (`.so`) are recompiled but the server process was already running, `dlopen` will continue executing stale code in memory. Kill running `axil` processes (`ps aux | grep axil`, `kill -9 <pid>`) and restart (`make watch` or `bg_run` with name `"axil dev server"`).

## Writing e2e assertions (anti-flake rules)

- **Don't use `page.waitForFunction` with `document`/`window`.** Deno's test
  type-checker (TS2584) has no DOM lib and rejects it. Use a locator-based
  polling helper (`waitFor(fn, ms, msg)`) instead.
- **URL order is DOM order.** A GET form submits checkboxes in document order
  (`?type=comunhao&type=natal`), so wait for the params **order-independently**
  (e.g. `new URL(page.url()).searchParams.getAll("type")` in the `waitFor`
  callback), not a `waitForURL(/type=natal&type=comunhao/)` regex.
- Wait for text via locator `textContent()` polling, not raw CSS selectors,
  when the value is patch-updated by wasm.

## Related docs

- `docs/BUILD.md` — build/restart steps the server prereqs depend on.
- `docs/ARCHITECTURE.md` — runtime model.
