# Phase 1 — ref-type retype (`rec_ref_t = uint32_t`)

> **Layout migration 2026-09-15:** the `~/lib*` sibling checkouts this doc
> names are gone; all four axis libs now live as in-site submodules
> (`external/lib{joint,islet,sepal,stoma}`). `~/lib*` paths below are the
> historical locations (accurate for their 2026-09-13 date); read them as
> `external/lib*`.
>
> Detail doc for `mm-plan/README.md` §5. The plan-level summary lives in
> §5; status, evidence, the execution checklist, and the decision log live
> here. No `.pi/quest/` tracking is used for this plan (decided
> 2026-09-13) — this file is its replacement: if this checklist and §5
> ever disagree, §5 wins and this file gets fixed.

## Status

✅ **DONE 2026-09-13.** libcorm core retyped, green,
installed. **sepal done** (installed, verified). **stoma done**
(group-38 rewrite + `stoma_dest_emit` guard + header docs; `make test`
exit 0 — `stoma_test` 198/198, `stoma_prop_test` 3 seeds green;
installed — header verified identical to the working tree and
`/usr/lib/libstoma.so` byte-identical to the fresh build).
**joint done, islet done** (installed, verified). Fixture 16/16.
Site `make` green + `make test` green (manual user run). Gate (§6
item 1) green — phase 2 unblocked.

## Requirements

- Every `rec_ref_t` crossing an axis fill/rank boundary is a `uint32_t`
  corm ref; no axis-internal key ever leaves its axis (§2 of README, the
  ref-type law — locked, not re-litigated here).
- The two u64-era tests that encode the old width are rewritten for the
  u32 world (sepal `test_opaque_refs`, stoma group 38).
- The new headers + binaries are *propagated* to the live `/usr` stack in
  dependency order (libcorm first), not just edited in working trees —
  otherwise nothing downstream verifies (ABI mismatch, §5.1 of README).
- Gate (§6 item 1): every touched repo's suite green + `fixture_id_join`
  16/16 + site `make`.

## Evidence (research pass 2026-09-13, all read from source)

- `external/libcorm/include/ttypt/rec.h:41` — `typedef uint32_t rec_ref_t;`
  (retyped, working tree, uncommitted); `rec.c` fully generic, no change.
- `external/libcorm/src/rec_test.c` — duplicate-key test fixed to 4-byte
  keys; `src/bench_rec.c` — real 8-byte-read/4-byte-write buffer
  over-read fixed to `corm_reg(sizeof(rec_ref_t))`; `test.sh` +
  `test-cli.sh` green; `lib/libcorm.so` rebuilt.
- `/usr/include/ttypt/rec.h:37` — still `typedef uint64_t rec_ref_t;`
  (installed Sep 11, stale); `/usr/lib/lib{joint,sepal,stoma,islet,corm}.so`
  all built Sep 11 against the u64 header (stale ABI).
- `external/libcorm/tests/Makefile` — fixture links the system-installed
  `libcorm.so`/joint/sepal `.so`s and asserts header≡installed-copy
  byte-identity; it cannot pass until the new stack is installed.
- `~/libsepal/Makefile:8` pins `-I…/external/libcorm/include` ahead of
  `-I/usr/include`; `~/lib{joint,stoma,islet}/Makefile` carry no explicit
  `-I` and resolve `<ttypt/rec.h>` from `/usr/include` — hence the
  libcorm-first install order.
- `~/libsepal/tests/unit/test_vecstore.c:76,82` — `1ULL << 63` narrows to
  `0u` under u32, duplicating ref 0 (`sepal_n` becomes 4, not 5): broken,
  must be rewritten with u32-world refs.
- `~/libstoma/src/stoma_test.c:947` — `big = 5000000000ULL` truncates to
  705032704: the "genuinely 64-bit" proof is void (still round-trips, now
  meaningless): must be rewritten as the u32-boundary proof.
- `~/libstoma/src/libstoma.c:238-251` (`stoma_dest_emit`) — `strtoull` →
  `(rec_ref_t)` narrows silently; non-decimal row_ids already abort the
  fill via `d->err` (`stoma.h:94-106` documents the strict-decimal
  contract): the `> UINT32_MAX` guard extends that same rule.
- `~/libsepal/include/ttypt/sepal.h:10`,
  `~/libjoint/include/ttypt/joint.h:19-28`,
  `~/libislet/include/ttypt/islet.h:89-99` — stale "u64" `rec_ref_t`
  wording in the ID-uniformity notes.
- `~/libjoint/src/libjoint.c:380-382` — `qm_id` already
  `corm_reg(sizeof(uint32_t))`; `:896-906` widen is identity: no code
  change. `~/libislet` — `uint32_t` cell values, u64 morton keys internal,
  fill widen is identity: no code change.
- Site impact: `rg rec_ref_t` over `external/{hyle,axil,libxylem}` and
  `external/libcorm/rust-bindings` returns zero hits — the site build is
  source-invisible to the retype.
- `external/libstoma/src/stoma_test.c` has no group-38/big-ref content —
  the site submodule needs no edits (scope stays `~/lib*`).
- **stoma execution (2026-09-13, TDD red→green):** new group-38 test run
  *before* the guard: 197/198, exactly the new
  `fill aborts on >UINT32_MAX row_id` check failed (fill returned 0 with
  the truncated alias instead of -1) — proves the test has teeth. Guard
  added → `make clean && make test` exit 0: `stoma_test` 198/198,
  `stoma_prop_test` seeds 1/42/1337 (300 rows × 1500 queries each) OK,
  zero warnings under `-Wall -Wextra -Wpedantic`. Deliberate
  non-changes: the two `snprintf("%llu", (unsigned long long)ref)` sites
  (`stoma_index_ref`, `stoma_rank`) are functionally correct under u32
  (varargs promote; buffer `char rid[24]` fits 10-digit max) — left alone,
  only the header doc's stale `%llu` quote was de-littered. Build wiring
  confirmed: mk emits `-I…/libstoma/include` ahead of `-I/usr/include`,
  `-L…/libstoma/lib -L/usr/lib`, tests run with `LD_LIBRARY_PATH=lib`
  (local fresh `libstoma.so` + installed u32 `libcorm.so` — consistent).
  `/usr/include/stoma/stoma.h` is one feature behind the working tree
  (lacks `stoma_index_ref`, installed Sep 11) — the pending
  `sudo make install` fixes that too; verify header≡working-tree and
  `.so` byte-identity after.
- **Stale-binary ABI hazard (hit 2026-09-13, sepal):** after the libcorm
  install, sepal's `test_search`/`test_axis` failed with pointer-valued
  refs and denormal scores — looked like a real width bug, but the test
  binaries were dated Sep 11 (built against u64 headers) while
  `libsepal.so` was fresh u32. Two compounding causes: (a) top-level
  `make clean` (mk's rule) never descended into `tests/` (which has its
  own `clean` nobody called); (b) the tests build rule
  `$(UNIT_DIR)/%: $(UNIT_DIR)/%.c test_common.h` carries no header deps,
  so unchanged `.c` files never rebuild. Fixed in `~/libsepal/Makefile`
  (`clean: clean-tests` → `$(MAKE) -C tests clean`); full `make clean &&
  make test` then went green (exit 0), confirming **zero functional
  sepal changes**. Standing rule for stoma/joint/islet: always
  `make clean && make test`, and if any of them shows the same symptoms,
  apply the same one-line recursion before suspecting the code.
- **Same hazard, second and third sightings (2026-09-13, joint + islet,
  during the docs pass):** joint `./test.sh` failed 3 Category-9 checks
  (`rec_ref_present` in fill/decode/open tests) — `bin/test` dated Sep 11
  running against the Sep-13 u32 `/usr/lib/libcorm.so`; `make clean && make
  all && ./test.sh` → all green (core expects-diff + extended). Islet
  `make test` failed 2 fill checks (`fill_mv_cell`: expected 11, got
  94489280523 = `0x16_0000000B`, high-bits leakage — the classic width
  signature) — `tests/unit/` binaries dated Sep 11, and top-level
  `make clean` does NOT descend into `tests/` (own `clean` target,
  uncalled); `make -C tests clean && make test` → exit 0, 27 suites, zero
  failures. Neither repo needed functional changes — both failures were
  the environment, both times. Extension to the standing rule: joint/islet
  still need the manual `make -C tests clean` until someone adds sepal's
  one-line clean-recursion there too.

## Execution checklist

- [x] Research all four axis libs + build/install wiring (read-only).
- [x] Record findings + refined checklist in the plan (README §5, this file).
- [x] `external/libcorm`: re-ran `./test.sh && ./test-cli.sh`; user ran
  `sudo make install`; verified `/usr/include/ttypt/rec.h` reads u32.
- [x] `~/libsepal`: header wording + `test_opaque_refs` rewrite +
  `Makefile` clean-recursion fix → `make test` green (exit 0, 13 suites,
  e.g. search 4528 + vecstore 2253 assertions) → installed; verified
  `/usr/include/ttypt/sepal.h` identical to working tree and
  `/usr/lib/libsepal.so` byte-identical to the fresh build.
- [x] `~/libstoma`: group-38 rewrite + `stoma_dest_emit` guard + header
  docs → `make test` (`bin/stoma_test` 198/198, `bin/stoma_prop_test`
  3 seeds) green 2026-09-13 → installed; verified
  `/usr/include/stoma/stoma.h` identical to working tree and
  `/usr/lib/libstoma.so` byte-identical to the fresh build.
- [x] `~/libjoint`: ID-uniformity note u32 + Operations catalog/docs
  corrections → `make clean && make all && ./test.sh`   green 2026-09-13
  (core expects-diff + extended; first run red was the stale-binary
  hazard, see above) → installed; verified
  `/usr/include/ttypt/joint.h` identical to working tree and
  `/usr/lib/libjoint.so` byte-identical to the fresh build.
- [x] `~/libislet`: ID-uniformity note u32 + `islet_search` sentence fix →
  `make -C tests clean && make test` green 2026-09-13   (exit 0, 27 suites;
  first run red was the stale-binary hazard, see above) → installed;
  verified `/usr/include/ttypt/islet.h` identical to working tree and
  `/usr/lib/libislet.so` byte-identical to the fresh build.
- [x] Fixture: `make -C external/libcorm/tests clean all test` → 16/16
  (2026-09-13).
- [x] Site: `make` green 2026-09-13 (incl. `W06 check PASS: no native
  imports in WASM`) + `make test` green (manual run by the user,
  2026-09-13).
- [x] Post-gate cleanup 2026-09-13 (comment/doc-only; suites re-greened
  after): `~/libislet/README.md` width-lie fix ("promoted to `rec_ref_t`
  (u64)" → u32); sepal ref printing `%llu` → `%u`
  (`examples/basic.c`, `twostage.c` ×2, `kernel_form.c`, README Quick
  Start) + examples rebuilt warning-free; `make test` exit 0 sepal +
  islet after.
- [x] Marked DONE 2026-09-13 (README v14). **Commit only when explicitly asked.**

## Decisions

- 2026-09-13: installs propagate via `sudo make install`, run by the user
  (root-owned `/usr`, same as the Sep 11 installs); the assistant hands
  the exact commands and does everything else.
- 2026-09-13: add the `stoma_dest_emit` `> UINT32_MAX` abort (pure
  hardening; mm's own AINDEX ids can never exceed u32, but the raw
  `stoma_index` API accepts any decimal string and truncation would alias
  a different row).
- 2026-09-13: scope is `~/lib*` sibling checkouts only (now
  `external/lib*` submodules — see the migration note at top); the site's
  `external/libstoma` submodule is untouched (rebuild consistency only).
- 2026-09-13: no `.pi/quest/` usage for this plan; `mm-plan/` is the
  single home. (Supersedes the deleted
  `.pi/quest/future/REC-REF-U32-PHASE1.md` tracking stub, whose content is
  preserved here.)
