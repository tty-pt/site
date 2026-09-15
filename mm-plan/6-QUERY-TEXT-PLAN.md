# 6-QUERY-TEXT-PLAN.md — Phase 6: sepal query-time `query=` leaf

Status: **DONE 2026-09-16** — implemented as planned (libsepal `1ade85a`;
site Phase B landed). Tracked here; status lives in `README.md` §0 row 7.
Leaf name settled by user choice: **`query=`**
(parallels stoma's `query=` key).

## Goal

Let the caller pass a plain string — `sepal="query='harbor lights' m=10
min_sim=0.2"` — and have **libsepal embed it itself** at query time through
its already-configured embedder. This deletes the entire pi-mm curl→tempfile→
`file=` bridge (`src/embed.ts`, `ToolEnv.exec`, the struct.pack dance in
`RUNNING.md`); pi-mm just emits a text leaf.

## Verified foundations (do not re-open)

- **Seam**: `sepal_axis_decode` (`external/libsepal/src/libsepal.c:1043`)
  builds a heap-owned `rec_sepal_params{q,qdim,m,min_sim}`; `fill`/`rank`
  only need `p->q` set and already null-check it (`:1014`, `:1024`).
  Eager-embed-inside-decode fits with **zero** fill/rank/kernel/CLI changes.
- **Quoting**: stoma's established convention is single-quoted values with
  spaces (`query='black star'`, `libstoma.c:681-744`, lenient on
  unterminated quote). The `-X` lexer passes double-quoted whole values
  through with inner single quotes literal (`qmap.c:1433-1445`), so
  `sepal="query='harbor lights' m=10"` arrives at decode intact. Mirror
  stoma's tokenizer verbatim.
- **Failure contract**: decode NULL → fill −1 → `qmap_expr_error` →
  `exit(EXIT_FAILURE)` (`qmap.c:1643-1644, :1350-1359`). Same as unreadable
  `file=` today: loud query error, never silent-empty. pi-mm's client-side
  `sepalConfigured` gate + soft fallback stays as-is.
- **Test seam**: `sepal_embed_fetch` is `__attribute__((weak))`;
  `test_axis_store.c:20-44` already overrides it with a canned-vector
  strong stub. The new path is unit-testable offline the same way.
  (`test_axis_store_live.c` covers real curl, env-gated.)
- **Repo boundary**: `external/libsepal` is a submodule (`tty-pt/libsepal`,
  at `663c5fa` when planned). The C change lands there; the site picks it
  up via pointer bump.
- **Math**: query dim = embed full dim (e.g. 768); `sepal_search` already
  caps the exact stage at `SEPAL_EXACT_DIM` and skips candidates with
  `h.dim > qdim` (`:911`, `:922`) — no change needed.

## Phase A — libsepal repo (`tty-pt/libsepal`)

### A-1 Decode grammar (`src/libsepal.c`, `sepal_axis_decode` only)

- Tokenize with the stoma single-quote-aware splitter (copy the pattern).
- New key `query`; existing `file/qdim/m/min_sim` parsing unchanged.
- Precedence: non-empty `query` → embed path; else `file=` path (unchanged).
- Embed path: unconfigured (`!sepal_embed_cfg.url`) → free + NULL;
  `sepal_embed_fetch(query, &v, &n)` rc≠0 → NULL; `n==0 || n>SEPAL_VEC_MAX`
  → free + NULL; else `p->q=v, p->qdim=n`. `m`/`min_sim` parsed as today.
- New leaf: `sepal="query='harbor lights' m=10 min_sim=0.2"`.

### A-2 Header docs (`include/ttypt/sepal.h`)

- Extend the embedder-config comment (`:87-110`, embedder now serves store
  *and* query-text).
- Add a query-grammar block (near `sepal_search` `:175-184`): `file=` vs
  `query=` forms, precedence, NULL conditions.
- Update the decode comment (`libsepal.c:1033-1042`).

### A-3 Tests (auto-discovered `tests/unit/test_*.c`)

- New `tests/unit/test_axis_decode_query.c`: strong-stub
  `sepal_embed_fetch` (canned 3-float vector, records received text):
  quoted multi-word `query='hello world'` → canned vector, qdim=3, text
  arrives verbatim; unquoted single token; `query`+`file` → query wins;
  empty query + no file → NULL; config cleared via
  `sepal_configure_embeddings(NULL,NULL,NULL)` → NULL *without* calling
  fetch (offline-safe).
- End-to-end: `sepal_put` direct floats → decode `query=…` → registered-axis
  fill+rank → ref found with expected cosine.
- Gates: `make test` green, `make valgrind` clean for the new binary, zero
  warnings. (TESTING.md: build site `external/libqmap` kernel first.)

## Phase B — site + pi-mm simplification

### B-1 Submodule bump

Commit in libsepal, bump the pointer in site, `make -C external/libsepal`,
site `make` green.

### B-2 pi-mm (net deletion)

- Delete `src/embed.ts` + `tests/embed.test.ts`.
- `qmap.ts`: `sepalLeafFor(vecFile,qdim)` → `sepalLeafForText(topic)`
  emitting ``sepal="query='<topic>' m=10 min_sim=0.2"``. Keep
  `SEPAL_M/MIN_SIM`, `embedEnv`, `filespecFor(_,embed)`, `sepalConfigured`
  (store still embeds; env passthrough still required).
- `tools/index.ts`: drop `exec` from `ToolEnv` (curl's only consumer is gone).
- `scan.ts`: `embed=true` + configured → text leaf; no curl, no temp file,
  no `finally` cleanup. Unconfigured → same soft `unconfigured` fallback.
  Behavior change to document: unreachable endpoint now surfaces as the
  standard loud-but-graceful `mm scan failed (exit 1)` diagnostic instead
  of `no-vector` (consistent with `file=` today).
- `scanTopRef` stays text-only.

### B-3 pi-mm tests

- `sepalLeafForText` exact-string tests (incl. quoting); scan emits text
  leaf + sepal filespec + embed env with zero exec calls; unconfigured
  fallback; update `env()` helper (no `exec`); delete curl/tempfile tests.

### B-4 Integration smoke

- Store step unchanged (python one-shot mock still feeds store-time embed).
- Query step becomes `sepal="query='embedded lighthouse beacon' m=10
  min_sim=0.4"` — with the canned mock every string embeds to
  `[0.1,0.2,0.3]`, so it self-matches at 1.0. Keep one `file=`
  dissimilar check (preserves `file=` coverage live).

### B-5 Docs

- SKILL.md (new leaf semantics + unreachable-endpoint behavior); pi-mm
  README/AGENTS.md (layout minus `embed.ts`, no `ToolEnv.exec`);
  `RUNNING.md` §4 collapses to a one-liner query; this doc stays as the
  Phase 6 record (`5-EMBED-PLAN.md` stays as Phase 5's landed record).

## Gates

- A: libsepal `make test` + valgrind clean, zero warnings; no new exported
  symbols (`nm -D` unchanged — decode is static, no ABI change); additive
  grammar only, no rank-convention impact.
- B: pi-mm `deno test` / `lint` / `check-complexity` / `integration-mm.sh`
  green; root `make` W06 PASS; loadable-and-graceful at every commit;
  commit only when asked.

## Known limitations (documented, not solved)

- A literal `'` in the topic truncates at the first quote — stoma `query=`
  parity, documented in SKILL.md.
- Store side still embeds the whole `<DATE>:<TEXT>` payload (Phase 5
  settled); only the query bridge moves into C.
- One synchronous embed POST per query — same cost profile as store-time
  embed; no threading changes.
- `make valgrind` in libsepal is red at baseline (`test_axis` leaks the
  documented never-free decode params; verified via stash at `663c5fa`,
  exit 1 with 2 loss records) and stays red the same way: the new binary
  loses only the 4 by-design decode param blocks (32 B struct + stub
  vector each), with zero invalid reads/writes. The green gate is
  `make test` (exit 0, 20 PASSED lines incl. 56 new assertions).
