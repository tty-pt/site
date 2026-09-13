# 2B-6 — Implementation detail (rank convention + `-X` grammar verification)

> Final slice of phase 2B (`external/libqmap`). Read these first, in order:
> `mm-plan/PHASE-2-CLI.md` (2B, decisions D2 + D10, the 2B-6 slice row),
> `mm-plan/CLI-SURFACE-EXAMPLES.md` (§4–§6 grammar + rank note),
> `external/libqmap/docs/RECALL-KERNEL.md` (Rank convention),
> `mm-plan/2B-3-IMPLEMENTATION.md` (§1–§2 the built parser this slice
> verifies), then `external/libqmap/src/qmap.c` (lexer `lx_next` + the
> `qmap_expr_*` recursive descent) and `external/libqmap/src/rec_axis.c`
> (lines 199–211, the kernel rank fallback).
>
> This file is the lossless record: §1 = rank convention as built,
> §2 = grammar verification (doc ↔ prototype), §3 = new gate rows,
> §4 = doc updates, §5 = gates, §6 = execution log.
> Status: **DONE 2026-09-15**. **Phase 2B closed.**

## 1. The rank convention as built (D2)

When a composed query is scored, the winner is the **first rank-capable
axis in query order** — the first axis that has a `rank` fn + `ctx`
supplies *the* score for every surviving ref; the other axes contribute
membership only. Both rankings paths implement exactly this:

- **CLI** (`src/qmap.c`, comment at line 273, eval at 1646-1648): the
  first `E_LEAF` with `axis->rank` seen in **preorder** records
  `ranker_axis`/`ranker_params`; every leaf filter-only ⇒
  `ranker_axis == NULL` ⇒ pure-filter render (no scores, asc ref).
- **Kernel** (`src/rec_axis.c:199-211`, `rec_query_run`): with
  `consumer_score == NULL`, the first axis in `q->axes[]` order with
  `rank` + `ctx` scores the run.

`--score` combining across rankers (sum/max/weighted, riding
`rec_consumer_score` + `rec_axis_score_t[]`) stays **deferred**.

## 2. Grammar verification (doc ↔ prototype)

D10 (`PHASE-2-CLI.md`, as documented in `CLI-SURFACE-EXAMPLES.md` §4)
checked line-by-line against `src/qmap.c`:

- Productions (`qmap_expr_setexpr/orexpr/andexpr/notexpr/primary/leaf`,
  `qmap.c:1499-1584`) match the doc grammar exactly, incl. EXCEPT only
  at the `setexpr` level, `or`/`and` folds, unary-chained `NOT`.
- Tokenizer (`lx_next`, `qmap.c:1360-1479`): word runs, uppercase-only
  keyword match (`lx_is_keyword`), `(`, `)` single-char tokens, optional
  `NAME=VALUE`; quoted VALUE verbatim to close quote (`unterminated
  quote` error); unquoted VALUE consumed to an unquoted `(`/`)` or a
  whitespace-delimited keyword — matches the doc rules incl. silent-
  truncation precedence (`quote values containing operator words`).
- `A NOT B` ⇒ the `use EXCEPT` hint (`qmap.c:1710-1713`); empty parens ⇒
  `expected expression`; trailing garbage ⇒ `unexpected token`. Empty/
  whitespace-only `-X` ⇒ unarmed (`expr_root == NULL`).

Two operational notes (previously doc shorthand, now pinned): (1) the
doc's n-ary `SUB(List)` notation means sequential **left-to-right**
subtraction — the parser builds nested left-assoc `E_SUB` nodes; (2)
the ranker is the first rank-capable leaf in **preorder**.

## 3. New gate rows (TDD)

- `test-cli.sh` — **`except-chain`**: `-X "alpha EXCEPT beta EXCEPT pure"`
  (alpha={1,2,3}, beta={2,3,4}, pure={1,2}) ⇒ expected **empty** stdout.
  Left-assoc `({1})∖{1,2}=∅`; right-assoc would have yielded {1,2} — a
  distinguishing result. Green first run.
- `test-real.sh` — **`stoma-over-sepal`** (floats-direct branch only):
  `-X 'stoma="field=text query=beacon matched=1" AND
  sepal="file=$qvec qdim=$qdim m=2 min_sim=0.5"'` ⇒ expected
  `1 0.125000 2026-09-13T20:00:00:Beacon Harbor lights` +
  `3 0.125000 2026-09-14T12:00:00:Beacon Harbor lights`.
  Preorder first-rankable = stoma (matched/doc-tokens 1/8 both, tie →
  asc ref), NOT sepal cosine (which would print `3 1.000000` /
  `1 0.906867`). Behavior probed against the built binary before the
  row was written; prediction matched exactly.

## 4. Doc updates

- `docs/RECALL-KERNEL.md`: new "Rank convention" subsection after
  "Ranking buffers".
- `CLI-SURFACE-EXAMPLES.md`: legend gains `[2B-6]`; §6 sepal bullet
  names the standing convention (D2) with the `test-real.sh` pin.
- `PHASE-2-CLI.md`: verification note after D10; 2B-6 row → DONE.
- `README.md`: version **v19 (2026-09-15)**; §3 cell 2B DONE; §7
  rank-axis item SETTLED; §12 2B-6 checked + phase closed; resume
  paragraph → phase 3 (`pi-mm`).
- `CHANGELOG.md`: Unreleased 2B-6 entry.

No lib/kernel code changed (rank paths already behaved this way); no
sibling lib changed; no new flags; classic path untouched.

## 5. Gates

- `make -C external/libqmap test` (env unset): all 6 scripts green
  (`test-cli.sh` now 27 rows, `test-real.sh` incl. `stoma-over-sepal`).
- `make` (site root): W06 PASS.
- Stale-path grep over `mm-plan/` + `external/libqmap/`: only the
  intentional migration notes.
- D4: no `/usr` installs; write gates on fresh in-site builds.

## 6. Execution log

- 2026-09-15: plan-mode analysis read the built parser + kernel
  fallback and found the doc grammar accurate; gaps identified
  (chained EXCEPT, two-rankable selection unpinned).
- 2026-09-15: `except-chain` added to `test-cli.sh` — green first run
  (empty ⇒ left-assoc as documented).
- 2026-09-15: two-rank pin probed against the built binary (primed
  roster, real sibling libs) — prediction `1 0.125000` / `3 0.125000`
  matched exactly; `stoma-over-sepal` row added to the floats-direct
  branch — `test-real.sh` all green.
- 2026-09-15: docs landed (RECALL-KERNEL, EXAMPLES, PHASE-2-CLI,
  README v19, CHANGELOG); gates re-run; not committed (never asked).
