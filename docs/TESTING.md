# Testing — commands, prereqs, known failures, debug

How to run the test suites, what the server must look like, what fails
pre-existing, and how to write e2e assertions that don't flake.

## Commands

| Command | What |
|---------|------|
| `make unit-tests` | Runs `test.sh` in each module directory. Needs axil on :8080. |
| `make pages-test` | Page-render HTTP smoke test (curls :8080 for DOCTYPE/markup). |
| `make e2e-tests` | Playwright via Deno. Needs server + `AUTH_SKIP_CONFIRM=1`. |
| `deno test --allow-all tests/e2e/<file>.test.ts` | Single e2e file. |
| `make hyle-tests` | cargo test on the `external/hyle` Rust workspace. |
| `make -C external/hyle test` | hyle C unit suite (incl. zig-test step — see below). |

## Server prerequisites

- unit/pages/e2e all hit `http://localhost:8080` — start axil first
  (`docs/BUILD.md`). `AUTH_SKIP_CONFIRM=1` must be set **at server startup**
  (not just at test time) — the auth module reads it on boot.

## Pre-existing failures (not caused by local changes)

- `start.sh` and `scripts/watch.sh` always force `AUTH_SKIP_CONFIRM=1`
  (unconditional export). This is a development convenience, not production
  behavior. See `docs/AUDIT.md` F10.
- `make -C external/hyle test` `zig-test` step fails when `zig` is not
  installed (unrelated to the C suite).
- `tests/pages/20-song-search.sh` expects the "No items" empty state: a
  valid-but-zero FTS result renders `<p class="text-muted">No items</p>` (the
  deliberate zero-row empty state) instead of a `0 of 0 rows` pagination line;
  `row_count` maps that marker to `0 of 0 rows`.
- `make hyle-tests` / `make integration-tests` are not part of `make test` — run manually.

## Debug logging

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
