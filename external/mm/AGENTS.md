# AGENTS.md — external/mm

Memory Mipmaps engine: persistent hierarchical memory (L2 summary → L1
condensed → L0 raw) as a C CLI on system libqmap + the site's libstoma.
Provider-free by default; semantic recall is offline (`--vec`/`--like`) with an
optional config-gated `--embed` via curl.

**Read `README.md` first. Read `~/llm/MM.md` §2.2 (provider policy, final) and
§3.1 before changing the engine design.**

## Invariants (do not "fix" these)

- **Multi-axis composition is `rec_query`'s job, not mm's.** mm maps its scan
  flags (`--when`, `--where`, `--q`, `--level`, `--topic`, `--vec`/`--like`)
  onto `rec_query_t` and calls `rec_query_run`. Do **not** hand-roll join
  loops (intersect/subtract/union) or a min-sim+qsort rank loop in mm's
  engine; register a fill/rank axis through libqmap's `rec_axis` registry
  instead (see `external/libqmap/docs/RECALL-KERNEL.md` "Query engine &
  plugin registry"). The offline paths must still work with no plugin loaded.
- **Keys never contain `:`.** libqmap record maps treat any `:` in a key as a
  `struct:field` composite lookup. All keys are `mm_keyify`'d (colons
  stripped); the stored `ts` field keeps the real ISO string. `mm_get`/
  `mm_forget`/`mm_scan` must keyify their input; do not bypass it.
- **One map per file.** libqmap saves/loads multiple maps sharing one file
  positionally; across processes (or on reopen in a different open order) that
  silently skips blocks. Entries live in `<path>`, vectors in `<path>.vec`.
  Never open two maps in the same file.
- **Never `qmap_close` the entry/vec handles.** libqmap has an
  `__attribute__((destructor))` that runs `qmap_save()` after `main`. Closing a
  map first removes it from `file->ids`, so the destructor sees an empty file,
  truncates it to 0 bytes, and data is lost. `mm_close` deliberately does not
  close the qmap handles; free only the engine.
- **FTS rebuild condition is `fts_dirty || !fts_built`** — on a fresh open the
  store is populated but the index is empty, so `fts_dirty` alone would skip the
  rebuild and the first query would miss everything.
- **Semantic scan is a routing aid, never the only index.** `mm_semantic_scan`
  always starts from the same topic/prefix/level/FTS candidate set; entries
  without a stored vector, or with a mismatched dimension, are skipped (never
  compared). Keep the offline path (`--vec`/`--like`) working with no provider.
- **`--embed` is config-only convenience.** It shells out to system `curl`
  (`$MM_EMBED_URL`, optional `$MM_EMBED_KEY`/`$MM_EMBED_MODEL`) and parses the
  first JSON float array. It must fail loudly with a pointer to `--vec`/`--like`
  and must never be required for any other operation.
- **Candidate keys are owned; free the ones you drop.** `mm_semantic_scan`
  collects `strdup`'d keys into `cand`; any candidate killed by the
  FTS/level filter must be `free`d when nulled, not just skipped in compaction.
  Verified leak-free under ASan (engine side). Note: ASan still flags a
  heap-use-after-free inside libqmap's own exit destructor — that is library
  teardown, not engine code; do not "fix" it here.

## Build & test

```sh
make mm                       # site root => external/mm build
make -C external/mm test      # 120 unit checks + CLI suite
make standalone-unit-tests    # runs mm suite inside the site gate
```

## Working here

- Keep `engine.{h,c}` provider-free (pure data engine). All network lives in
  `mm.c` (`embed()`).
- Adding a CLI flag: parse both `--name=value` and `--name value` via `opti()`.
- Tests: extend `engine_test.c` (uses `mkstemp`) and `test.sh` (CLI e2e; it
  stubs `curl` for `--embed`). Run `make test` before finishing.
- Don't grow the driver-facing surface without updating `README.md` and this
  file's invariants.