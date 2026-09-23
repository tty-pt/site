# Phase 2 — per-axis write+delete+get (2A) then CLI composition (2B)

> Detail doc for `mm-plan/README.md` phase 2 (`README.md` §11 = 2A
> checklist, §12 = 2B). Phase 2A (per-axis store/unstore/get) is a standalone
> prerequisite; phase 2B (CLI composition) is the architectural phase.
> Deliberately high-level *in 2B*: no concrete flag/argument syntax lives
> here beyond what is locked below. Syntax rots the moment a decision changes
> — an earlier draft of this doc named specific flags and was already wrong
> days later. Capability and constraint are stable; remaining 2B spellings are
> an implementation detail, designed against a working prototype when the
> phase actually starts, not frozen into a planning doc. The deliberate
> exceptions are **2A's signatures, value grammars, and per-axis roles, which
> are locked (decided 2026-09-13; whole-un-split-string `value` confirmed
> 2026-09-14)** — they are the plugin contract, not CLI syntax.
>
> ## Principles (the general directions — do not re-litigate)
>
> 1. **Axes decide put/delete/get.** The CLI passes `(ref, value)` blindly —
>    one opaque string, never split, never interpreted. Each axis parses the
>    whole value string in its own grammar. No per-axis knowledge in corm,
>    ever.
> 2. **Axes own their query language.** Each axis parses its own retrieval
>    grammar — its `NAME=VALUE` leaf in the `-X` set expression (D10); the
>    CLI never interprets it.
> 3. **The CLI conjunctions axis results.** AND/OR/NOT over ref sets (kernel
>    joins), scored through rankers. Set algebra is generic; domain is not.
> 4. **corm stays legacy-close.** Zero-axis invocations byte-identical; every
>    new feature additive (flags, one filespec segment, no changed classic
>    semantics).
>
> ## Needs (kept visible — the checklist behind the checklists)
>
> Per-axis store/unstore/get conventional exports · a single whole-string
> `value` per axis, parsed axis-side (joint leading-date, sepal
> embedding/floats, stoma text, islet point-list) · joint three-form +
> erase + exact-dedup · islet `rev` + `del_value` · stoma `unindex`
> (memory-only) + per-open rebuild · sepal floats + curl-embed strings +
> config setter + offline test seam · `@` roster parse/persist/load ·
> inter-pass load+bind + alongside-defaults · `-g`
> fold + `-Q` retirement + `test-cli.sh` rewrite · fan-out + forget/reset ·
> three-axis gate + mm dialect · rank docs.

## Phase 2A — per-axis store/unstore/get — PLANNED 2026-09-13

Standalone prerequisite phase. Before any CLI surface exists, every
registered axis gains the write/delete/read-back third of the plugin
convention, so a consumer can with one generic call family record a ref in
an axis store, remove it idempotently, and read its entries back — no
per-axis glue. The CLI passes `(ref, value)` — one opaque string, never
split; **what each axis does with the whole string is the axis's own
decision** (principle 1). Contract kernel:
`RECALL-KERNEL.md` "`rec_axis_store` / `rec_axis_unstore` /
`rec_axis_readback` conventional exports". No libcorm/kernel change, no site
change, no `/usr` installs (D4 applies here too — write gates run on fresh
in-site `external/lib*` builds).

> **U2 locked (2026-09-14): the read-back export is `rec_axis_readback`,
> not `rec_axis_get`.** `rec_axis_get(int slot)` is already a landed kernel
> registry lookup (`rec.h:203`, used by corm.c / rec_axis_test.c /
> stoma_test.c), so `rec_axis_get(void*, ref, char**, size_t*)` cannot exist
> in a plugin — the plugin `.so` includes `rec.h` and both would clash.
> Convention name rotates to `rec_axis_readback` — the plugin's "read the
> entries a ref owns" symbol. `store`/`unstore` are collision-free and stay.

### The inverse principle (this is the whole delete design)

`rec_axis_unstore(ctx, ref)` deletes exactly the entries the axis's own
store created for that ref, by walking backwards the same knowledge the
store used to put them there (`rec_axis_readback` reads them back the same way).
Delete cost ∝ entries the ref owns, never O(store), never a scan. An axis
whose store is already ref-indexed (sepal), or keeps a ref-derived inverse
(joint's `id` index, stoma's doc side-table), reuses that structure with a
public op + tests — **zero new stored state**. Only where the store key is
not the ref and no inverse exists (today only libislet's cell grid) does an
axis add the one canonical manifest (`ref → entries`) — the same *kind* of
inverse the others already happen to keep.

| Axis | The existing inverse | New footprint |
|---|---|---|
| libsepal | store keyed **by ref** (same-ref put is replace-in-place, verified) | `rec_axis_store`/`unstore`/`readback` adapters only (`unstore` normalizes `sepal_del`'s absent −1 → 0) — **DONE 2A-1** |
| libjoint | `id` index (id → ti keys, kept by `corm_assoc`, backfilled from the file-backed `ti` map on open) | public `joint_erase(jd, id)` + adapters (ordered-attempt + three-form grammar; id-index exact-match guard; read-back = NUL-joined intervals in the store grammar) + tests; no stored state — **DONE 2A-3** |
| stoma | doc side-table (`field\trow` → folded text) — re-tokenize → the exact posting keys (the table also serves phrase verification + rank lengths, so it is query structure, not duplication) | public `stoma_unindex`/`_ref` + adapters (`text` field; read-back = folded doc, one entry — locked 2026-09-14) + tests; no new state; memory-only — **DONE 2A-2** |
| libislet | none (cell-keyed, value = ref) | **only structural addition**: `rev` manifest (own single-map `<fname>.ridx`: u32 ref → `;`-joined canonical point string, replace-in-place per ref) + `islet_del_value_N` family (`1..4` + `2_32`); `store` replace-in-place, shared cells legal, `unstore` O(cells-of-ref) |

### Contract (uniform across all four axes, locked)

- `int rec_axis_store(void *ctx, const char *spec, rec_ref_t ref, const char *value)`
  — records `ref` and derives its index from the **whole** `value` string, as
  given (axis parses it entirely in its own grammar; corm never splits or
  interprets). 0 ok; −1 error (`errno`: `EINVAL` bad grammar/args, `ERANGE`
  out of domain); `ctx == NULL` → −1. The primary accepts any string; an
  axis is never forced to understand a format outside its grammar (it alone
  rejects). `spec` reserved (NULL — never credentials).
- Additive typed path (D12, pre-2B-4 — keeps shipped `*.so`s valid):
  `int rec_axis_store_typed(void *ctx, const char *spec, rec_ref_t ref, const void *blob, size_t len, uint32_t qtype)`
  — same, but for binary primaries (any `corm_reg` type). Axis implements
  both; CLI prefers the typed symbol when `vtype != CM_STR` (forwarding
  `(ptr,len,qtype)` from `corm_get`+`corm_type_len`), else the string one.
  Missing typed export ⇒ text-only axis. `readback` already binary-capable
  (`blob/n`).
- `int rec_axis_unstore(void *ctx, rec_ref_t ref)`
  — removes every entry the ref owns. **Idempotent**: absent ref → 0
  (this is the documented compensation primitive for a partial write-
  fanout); `ctx == NULL` → −1.
- `int rec_axis_readback(void *ctx, rec_ref_t ref, char **blob_out, size_t *n_out)`
  — **locked (U2, 2026-09-14)**: reads back the entries the ref owns as one
  malloc'd buffer of NUL-joined entry display strings; the consumer renders
  first/all/count from it. Named `readback` because `rec_axis_get` is already
  the kernel registry lookup (`rec.h:203`).
- Optional exports, dlsym'd like `rec_axis_open`; an axis missing
  store/unstore is **read-only** (missing get: no read-back). Not kernel
  API, not in the `rec_axis_t` vtable.
- Surface rules (uniform, consumer-enforced): axis ops require an `a`-type
  primary (refs must exist — the ref-type law, enforced); axis values come
  from `s`/`u`-origin strings (decimals rendered canonically). Notation
  validation is axis-side and loud.
- The same ref (u32, §2) crosses the boundary; internal keys never leave
  the axis. Single-writer; no-close invariant (consumer processes never
  close axis stores — note `stoma_close` does not save, so calling it loses
  data); libcorm's exit-time destructor save persists everything still open.
  File integrity checked after every mutating op (crash-consistency).
- **Library clients are unaffected.** A client calling `corm_put`/`corm_del`
  directly never touches `rec_axis_store`/`unstore`/`get` — axis stores are
  a CLI-orchestrated consumer-side concern, not part of the libcorm API
  contract. The axis convention is the *plugin* contract (what each axis
  exports for the CLI to discover via dlsym), not a mandate on libcorm's
  own callers.

### Per-axis roles (axis-decided; the CLI passes the whole string blindly)

- **libsepal** — dual grammar, floats first: `"f1,f2,…"` comma-floats
  (dim = token count, `1..SEPAL_VEC_MAX`) go direct to `sepal_put`;
  otherwise the string is embedded **iff** an embedder is configured, else
  `EINVAL`. Embedder binds via an explicit config setter (e.g.
  `sepal_configure_embeddings(url, model, key)` — prototype names);
  libcurl is `dlopen`d lazily on first embed (no build-time or offline
  dependency; absent curl → `EINVAL`). Unconfigured + string → loud.
  Model dim must satisfy the dim policy (≤2048, else `ERANGE`).
- **libjoint** — parses the **whole** value string: (1) explicit `"A,B"`
  (both dates, B>A) → atomic interval; (2) leading date prefix then anything
  else (e.g. the mm `"<DATE>:<STRING>"` shape) → open interval starting at
  that date, trailing content ignored; (3) three-form (`"T1"` open,
  `"T1,T2"` atomic, `",T2"` close-or-backfill); (4) else `EINVAL`. Nothing
  is split by the CLI — joint reads the date out of the string itself.
  Native `1` (already-present / back-filled) absorbs to 0. Exact-duplicate
  restates are no-ops via an `id`-index exact-match guard in the adapter (a
  repeated native `stop` would back-fill a duplicate `[-inf,T2)`); a restate
  with a *different* start legitimately adds a second interval (multi-interval
  ownership is real — replace is explicit via erase). Epoch or ISO-8601
  (`sscantime`); `ref != UINT32_MAX` (reserved sentinel).
- **stoma** — the **whole** value string's text under a **canonical `text`
  field** (single-facet at the store boundary — symmetric with the other
  axes' single-value stores); multi-facet indexing stays raw native
  (`stoma_index`/`stoma_unindex` per field). Memory-only; each open
  bulk-rebuilds from primary strings (`s` raw + `u` decimal — exactly what
  the store path accepts, so rebuild and incremental never diverge).
- **libislet** — the **whole** value string is a point list `"x,y[,z];x2,y2"`
  (`;`-separated points, `,`-separated int16 coords; dim = first point's
  coord count, 1..4; int16 lanes only — the int32 dim-2 config is
  indistinguishable by grammar, so the adapters stay on `islet_put_1..4`).
  Strict token grammar (`[-+]?[0-9]+`, `INT16_MIN..INT16_MAX`); duplicate
  points within one call deduped; over `ISLET_AXIS_MAX_POINTS` (1024) →
  `ERANGE`; anything else → `EINVAL` (never forced to read an unexpected
  format). `store` is **replace-in-place** (locked): unstore the ref's
  previous cells, then `islet_put_N` each point — a ref owns one spatial
  footprint, re-store is idempotent. `rev` records the cells (own
  `<fname>.ridx` single map, u32 ref → `;`-joined canonical point string,
  replace-in-place per ref, atomic); different refs may share a cell (the
  grid is multi-value; `islet_del_value_N` removes only the target).

Retrieval vs record language (inherited, not invented): every axis's query
`VALUE` (the `NAME=VALUE` leaf in `-X EXPR`) stays space-separated `k=v`
(the de-facto uniform retrieval language); store grammars stay compact
positional. Sepal's file-vs-inline
asymmetry (query vector from a file, store vector inline or embedded) is
the axes' own habit.

### Persistence classes

File-backed (reopened across invocations): primary, joint `ti` (when
persisted), islet grid+`rev` (when persisted), sepal blobs, `@roster` map.
Derived / memory+rebuild (zero or transient disk, never stale): `stoma`
always, `joint`/`islet` optionally — `rec_axis_open(spec)` decides per
deployment (D13, `QDBE_MASK` `4095` as initial hint, auto-grow — D11;
env `CORM_MASK` overrides). Re-derived from primary strings/typed blobs
at each open (O(corpus) startup; measured in 2B-2 — over budget triggers a
persist-postings proposal, UNCLEAR U4).

### Slices (TDD red→green each; gate = that repo's suite + `nm -D`
shows the new symbols; clean-rebuild rule per repo)

- **2A-0** Contract doc (this section + `RECALL-KERNEL.md` convention +
  `README.md` §10 store/unstore/get columns): whole-string `value` signature,
  per-axis roles, surface rules, embedder + roster conventions, `get`
  signature lock. Plan v18. Content-only.
- **2A-mock** The first testable step — proof-of-contract slice, done in the
  libcorm repo before any real axis is touched. Extend the existing test
  plugin `external/libcorm/src/librec_axis_mock.c` with
  `rec_axis_store`/`rec_axis_unstore`/`rec_axis_readback` (in-memory
  per-axis ref→value store: store = append/replace-in-place, unstore =
  swap-remove, absent → 0 idempotent, readback = malloc'd read-back, ctx
  NULL → −1). Add a pure C test `rec_axis_store_test.c` (dlopen + dlsym
  against the mock .so, **no CLI involved**) that runs the full round-trip:
  store → readback → unstore → readback zero, plus idempotent-unstore,
  absent-readback, replace-in-place, NULL-ctx, per-axis isolation. Gate:
  libcorm `make test` green with the new binary wired into `test.sh`.
  Rationale for mock-first: the store/unstore/readback convention is a
  *plugin* contract — the mock proves the shape, dlsym-ability, and
  semantics in isolation before the real axis work in 2A-1; the CLI fan-out
  that calls these symbols lands later in 2B-4.
  **DONE 2026-09-14.** `src/librec_axis_mock.c`: per-slot in-memory
  ref→value store (`store_append` replace-in-place, `store_remove`
  swap-remove, absent→0; +124 lines), `rec_axis_store`/`unstore`/`readback`
  exports. `src/rec_axis_store_test.c`: dlopen+dlsym round-trip suite
  (store→readback→unstore→zero, idempotent-unstore, absent-readback,
  replace, NULL-ctx, per-axis isolation) wired as `bin/rec_axis_store_test`
  into `Makefile` (`-lcorm -lqsys`, qsys_dlopen + memcpy dlsym dance per
  corm.c convention) and `test.sh`. Gate: libcorm `make test` exit 0
  (test.sh + test-cli.sh).
- **2A-1** libsepal adapters: `store` = floats direct (comma-floats,
  dim = token count, `1..SEPAL_VEC_MAX`) / else curl-embedder path /
  else `EINVAL`; `unstore` = `sepal_del` absent → 0; `get` = stored vector
  rendered back as comma-floats. New API: `sepal_configure_embeddings(url,
  model, key)` config setter (all-NULL clears; errors deferred to first
  use) + lazy `dlopen("libcurl.so")` on first embed with only the needed
  curl symbols dlsym'd (no build-time or offline curl dependency; absent
  curl → `EINVAL`; model dim over `SEPAL_VEC_MAX` → `ERANGE`; embed via
  `sepal_put`, so the flat index stays consistent). To keep the suite
  offline-green, the curl call goes through a file-static function-pointer
  seam that tests override with a canned-vector stub; one env-gated live
  test (`SEPAL_LIVE_EMBED=1`) exercises the real curl path, skipped by
  default. Tests (`tests/unit/test_axis_store.c`): store→`sepal_get`
  cross-check→unstore→get 0; absent-unstore → 0; over-cap → `ERANGE`;
  unconfigured-string → `EINVAL`; configured-string → embedded vector via
  the stub seam; replace-in-place; NULL ctx.
  **DONE 2026-09-14.** In `external/libsepal` (`src/libsepal.c`,
  `include/ttypt/sepal.h`, +~480 lines): `sepal_configure_embeddings(url,
  model, key)` setter (url+model required together, key optional, all-NULL
  clears); `rec_axis_store` (floats-first via `parse_comma_floats`, over-cap
  → `ERANGE` before any embed attempt; non-floats → embed iff configured
  else `EINVAL`); `rec_axis_unstore` (`sepal_del` absent −1 → 0, verified
  `-1`-only-on-absent in source); `rec_axis_readback` (stored vector rendered
  as `%.9g` comma-floats — exact float32 round-trip — `n_out` = display
  chars, absent → NULL/0/0, NULL args → −1/`EINVAL`). Embedder: **real lazy
  curl** (`dlopen("libcurl.so.4"|"libcurl.so")`, only 7 symbols dlsym'd via
  the memcpy dance, `CURLoption`/`CURLINFO` ABI numbers hard-defined, JSON
  escape + OpenAI-style `{"model","input"}` body + Bearer header +
  `{"data":[{"embedding":[…]}]}` parse, non-2xx → error, embed-dim over cap
  → `ERANGE`, goes through `sepal_put` so the flat index stays consistent).
  Seam = **weak `sepal_embed_fetch`** (strong exe override wins); offline
  test (`tests/unit/test_axis_store.c`, auto-globbed, 70 assertions, stub
  returns `[0.5,-0.25,0.75]`) + separate env-gated live test
  (`tests/unit/test_axis_store_live.c`, no override, skips unless
  `SEPAL_LIVE_EMBED=1` + `SEPAL_LIVE_EMBED_URL`). `nm -D` shows all five
  symbols (`store`/`unstore`/`readback` = T, `embed_fetch` = W). Gates:
  libsepal `make test` exit 0 (unit+integration+property+stress), valgrind
  clean on the new test. **U2 read-back name = `rec_axis_readback`** (locked:
  `rec_axis_get(int)` is the landed registry lookup, plugin could never
  define the same name — see contract header note).
- **2A-2** stoma: native `stoma_unindex(db, field, row_id)` +
  `stoma_unindex_ref` (differential: index → unindex → zero residual
  postings/doc), then adapters (canonical `text` field; absent → 0;
  read-back = whole doc, one entry).
  Memory-only confirmed (no file support added).
  **DONE 2026-09-14.** In `external/libstoma` (`src/libstoma.c`,
  `include/stoma/stoma.h`): `stoma_unindex` walks the doc side-table
  backwards — re-tokenizes the folded `(field,row)` text to recover the
  exact `field\t<tok>\t<row>` posting keys, `corm_del`s each, then dels
  the doc entry (absent → idempotent 0; O(tokens-of-row), never O(store);
  zero new stored state) + `stoma_unindex_ref` (decimal). Adapters on
  `STOMA_AXIS_TEXT_FIELD` (`"text"`): `store` = unindex-then-index
  (replace-in-place, so a ref owns exactly one doc; NULL/empty value →
  `EINVAL`); `unstore` = idempotent 0; `readback` = the stored folded
  text as one NUL-joined entry (`n_out` = display chars, libsepal
  convention; absent → NULL/0/0). **Locked decision: read-back returns
  the folded (lowercased, accent-preserving) text** — the store's sole
  copy; verbatim-casing round-trip would need a second side-table
  (new stored state, out of scope). Tests: group 39 in `src/stoma_test.c`
  (per-token residual sweep — querying every original token proves zero
  residual postings; `stoma_rank` −1 after unindex proves the doc entry
  is gone; per-field isolation; dup-token docs; NULL contracts) + new
  `src/stoma_axis_store_test.c` (whole-string store, folded read-back,
  replace, unstore, per-ref isolation, canonical-field symmetry,
  `EINVAL` matrix), wired into `Makefile` (`all` + `test:`). Gates:
  `make test` exit 0 (stoma_test 227/227, prop seeds pass, adapter
  34/34), new adapter test valgrind-clean, `nm -D` shows all five
  symbols (`stoma_unindex`, `stoma_unindex_ref`, `store`/`unstore`/
  `readback` = T), zero warnings (`-Wall -Wextra -Wpedantic`).
- **2A-3** libjoint: native `joint_erase(jd, id)` (id-index
  `corm_get_multi` → del each ti key; `corm_assoc` cleans `max`/`id`),
  then adapters (ordered-attempt + three-form; absent → 0; neighbors
  unaffected; exact-duplicate restates are no-ops).
  **DONE 2026-09-14.** In `external/libjoint` (`src/libjoint.c`,
  `include/ttypt/joint.h`): `joint_erase` walks the `id` index with
  `corm_get_multi`, collects each `struct ti` (copies — the assoc mutates
  the index while its cursor is live, the `ti_finish_last` pattern), dels
  each from the primary `ti` map (ripples to `max`+`id` via `corm_assoc`;
  empirically verified: after a native double-backfill the single primary
  del clears both `max` dups), then `corm_del_all(id)` mops residual
  duplicate id-index entries the replace path leaves behind
  (O(intervals-of-id); zero new stored state; absent → idempotent 0;
  `UINT32_MAX` → `EINVAL`). Guards NULL-valued
  replace-stub entries on every id-index walk (native identical-replace
  leaves them; the adapter never creates them). Adapters on the
  `(unsigned)(uintptr_t)` jd cast: `store` = ordered-attempt over the whole
  string — no comma → open (exact date, else leading-date prefix for the
  `"<DATE>:<STRING>"` shape) → `joint_start`; `",B"` → close-or-backfill
  → `joint_stop`; `"A,B"` both clean dates with `B>A` validated before
  mutating → `joint_start` then `joint_stop`; else `EINVAL` (NULL/empty
  value, `ref == UINT32_MAX`, NULL ctx → `EINVAL`; native `1`s absorbed).
  Each form gated by an id-index exact-match guard (exact `[mn,mx]`
  already owned → no-op — stops a repeated `stop` back-filling a duplicate
  `[-inf,B)`); disjoint multi-interval ownership stays real (replace is
  explicit via erase). Dates via an internal non-aborting
  `joint_date_parse` (same formats/timezone as `sscantime`, returns −1 —
  the public `sscantime` CBUG-aborts on bad input, unusable for a loud-
  `EINVAL` adapter). `readback` = one NUL-joined buffer of entries in the
  store grammar (closed `"A,B"`, open `"A"`, backfill `",B"`; `n_out` =
  display chars; absent → NULL/0/0) — round-trippable through `store`.
  Known corner (documented): a `jd` of 0 widens to a NULL ctx, which the
  locked contract rejects — handle-0 stores are unreachable via adapters.
  Tests: new self-checking `src/joint_axis_store_test.c` (116 assertions —
  native differential incl. double-backfill mop + neighbors + restart,
  four grammar forms, exact-dup + disjoint no-op/add semantics, `B<=A` /
  garbage / comma-shape / NULL-empty-units `EINVAL` matrix, readback
  NUL-join + round-trip, fill integration), wired into `Makefile` (`all` +
  `LDLIBS`) and `test.sh` (after the diff); `test.c`/`expects.txt`
  untouched. Gates: `make clean && make test` exit 0 (existing diff intact
  + `test_extended` + 116/116), `nm -D` shows all four symbols
  (`joint_erase`, `store`/`unstore`/`readback` = T), zero warnings
  (`-Wall -Wextra -Wpedantic`). Valgrind on the new binary shows only the
  library's pre-existing process-lifetime `joint_iter` arena allocations
  (same class as `bin/test`'s own 33 pre-existing records); no record
  traces to the new erase/adapter/parse/guard paths.
- **2A-4** libislet: `rev` manifest + `islet_del_value_N` family (rev in
  its own single-map `<fname>.ridx` file — never a second map in the grid
  file; u32 ref → `;`-joined canonical point string, replace-in-place per
  ref; `rec_axis_open` registers a file-backed rev for non-empty `fname`,
  in-memory rev otherwise; raw `islet_open` handles get a lazy in-memory
  rev, documented) + adapters (`store` replace-in-place: parse-first, then
  unstore-then-put, in-call dedup, over `ISLET_AXIS_MAX_POINTS` → `ERANGE`;
  `unstore` idempotent O(cells-of-ref); `readback` = NUL-joined canonical
  points, round-trippable). Shared cells legal; `islet_del_value_N` family
  shipped for `1..4` + `2_32` (native primitive, rev-agnostic; the `2_32`
  config stays out of the adapters), `unstore` O(cells-of-ref).
  **DONE 2026-09-14.** In `external/libislet` (`src/libislet.c`,
  `include/ttypt/islet.h`, +~490 lines): strict whole-string point-list
  parser (`;`-points, `,`-int16-coords, dim from first point 1..4, uniform
  dims, in-call dedup, 1024-point cap → `ERANGE`); rev registry
  (grid→rev, spec buffer kept process-lifetime — the old `free(buf)` was a
  latent exit-time UAF, also fixed); `rec_axis_open` opens the `.ridx`
  sidecar + registers the pair; `islet_del_value_1..4`/`_2_32`
  (collect → `corm_del_all` → re-put survivors, returns count removed);
  the three adapters (NULL ctx → −1/`EINVAL`, `ref == UINT32_MAX` →
  `EINVAL`). Tests: new self-checking `tests/unit/test_axis_store.c`
  (15 tests, 193 assertions — round-trip, replace, per-ref + shared-cell
  isolation, dims 1/2/4, 22-string `EINVAL` matrix, `ERANGE` cap + exact-cap
  boundary, native `del_value` order-preservation, file-backed `.ridx`
  sidecar via `corm_save()`, readback resubmit, raw-handle lazy rev),
  auto-globbed by `tests/Makefile` (+ `.ridx` in `clean`). Gates:
  `make` zero warnings (`-Wall -Wextra -Wpedantic`), `make test` exit 0
  (all suites incl. the new one, fresh local build via `LD_LIBRARY_PATH`,
  D4), `nm -D` shows all nine symbols (`store`/`unstore`/`readback`/
  `open` + `del_value_1/2/3/4/2_32` = T), new binary valgrind-clean.
- **2A-5** Round-trips per persistence class **DONE 2026-09-14:** the
  2A-1..2A-4 suites proved the adapters in-process only; this slice proves
  each class round-trips through its documented lifecycle across **real
  process boundaries** — file-backed axes `store → reopen → query finds
  ref → unstore → reopen → absent`; stoma `store → query finds →
  unindex → query misses → rebuild → finds` (+ complementary half: a ref
  dropped from the primary corpus stays gone after rebuild, proving
  rebuild converges to primary truth, not just re-adds everything; plus
  rebuild-vs-incremental parity).
  - **Cross-process mechanics (shared, all three file-backed repos):**
    one self-exec'ing C binary per repo with `seed` / `verify1` /
    `unstore` / `verify2` modes + a no-arg orchestrator that `fork`+`exec`s
    itself one phase at a time and checks child exit codes (decided
    2026-09-14: self-exec harness over fork-only/shell-driver — one file
    per repo, fits the auto-glob wiring, true process separation).
    Every phase is a fresh process, so "reopen" really reads what the
    previous phase's explicit `corm_save()` (plus libcorm's exit
    destructor) flushed to disk; nothing is ever `corm_close`'d
    (no-close invariant); harness temp files self-unlink and are named to
    match the existing `clean` globs.
  - **libsepal** `tests/integration/test_axis_roundtrip.c` (auto-globbed):
    seed refs 1=`0.5,-0.25,0.75`, 2=`1.0,2.0,3.0` via `rec_axis_open(DB)`;
    verify1 reopen → `sepal_search` with the ref-1 vector returns ref 1
    (score ≥ 0.99), `sepal_get(2)` rehydrated; unstore(1); verify2 reopen
    → search no longer yields ref 1, ref 2 still found (isolation),
    `readback(1)` NULL.
  - **libislet** `tests/integration/test_axis_roundtrip.c` (auto-globbed):
    spec `<path>.db::1023`, `islet_init()` in every mode; seed
    11=`1,2;4,5`, 12=`7,8`; verify1 (fresh process) `readback(11)` =
    `1,2␀4,5` (**`.ridx` rehydration proof — untested until now**) +
    `rec_axis_fill_bbox_2` full box `{0,0}×{10,10}` = {11,12}, tight box
    = {11} only; unstore(11); verify2 reopen → `readback(11)` NULL, full
    box = {12}.
  - **libjoint** `src/joint_axis_roundtrip_test.c` (+ `Makefile` `all :=` /
    `LDLIBS` row + `test.sh` row): every worker burns handle 0 first
    (`(void)joint_init(NULL)` — the documented jd-0↔NULL collision,
    `joint_axis_store_test.c:60-68`), then `rec_axis_open(path)`; seed
    13=`2026-01-15:hello world` (open), 14=`2026-03-01,2026-06-01`
    (atomic); verify1 (fresh process) `joint_iter` at 2026-02-01 has 13
    not 14, at 2026-04-01 has 14 (**file-backed `id`-index backfill proof
    — untested until now**); unstore(13); verify2 reopen → 13 gone, 14
    intact, `readback(13)` NULL.
  - **libstoma** `src/stoma_axis_roundtrip_test.c` (+ `Makefile` rows;
    single process, zero disk): corpus = the "primary authority"
    {(1,"Beacon Harbor lights"),(2,"Beacon AND Pão"),(3,"alpha omega")};
    store all → `stoma_query("beacon")` = {1,2} → unstore(1) →
    `harbor` misses, `beacon` = {2} → rebuild (fresh `rec_axis_open("")`,
    re-index corpus **minus primary-dropped 3**) → `beacon` = {1,2} again
    (unstored-but-still-in-primary refs return — the never-stale
    property), `omega` stays gone, rebuild-vs-incremental parity.
  - **TDD:** tests first; lib code changes only where a cross-process
    reopen step reveals a real bug (candidates: islet `.ridx`
    rehydration, joint file-backed erase+backfill persistence).
  - **Gates per repo:** clean rebuild, zero warnings, `make test`
    (joint: `./test.sh`) exit 0 incl. the new binary, new binary
    valgrind-clean (fork+exec children included), `nm -D` spot-check (no
    new symbols — the 2A-1..2A-4 exports stay `T`), fresh local builds
    via `LD_LIBRARY_PATH` (D4, no installs).
  - **DONE 2026-09-14.** Four harnesses: libsepal
    `tests/integration/test_axis_roundtrip.c` (5 symbols T, `sepal_search`
    finds ref 1 reopen, isolation ok, valgrind 0, `make -C tests test`
    1857/20064/87465/10609 assertions green), libislet
    `tests/integration/test_axis_roundtrip.c` (9 symbols T, `.ridx`
    rehydration + `fill_bbox_2` proofs, valgrind 0, `make -C tests test`
    green incl. new 2-phase harness), libjoint
    `src/joint_axis_roundtrip_test.c` (5 symbols T, burn-handle-0 then
    `joint_iter` at 2026-02-01/2026-04-01 per-ref isolation, file-backed
    `id` backfill proof, valgrind 0 on new binary — pre-existing
    `joint_iter` arena leaks remain in the old store test only,
    `./test.sh` green), libstoma `src/stoma_axis_roundtrip_test.c`
    (single-process, 43/43, valgrind 0: corpus `{(1,"Beacon Harbor
    lights"),(2,"Beacon AND Pão"),(3,"alpha omega")}` store → beacon 2,
    unstore 1 → harbor miss, rebuild minus primary-dropped 3 → beacon
    {1,2}+harbor {1} again, omega gone, rebuild-vs-incremental parity).
    No lib code changed — harnesses were green first try (islet `.ridx`
    and joint `id` backfill already correct cross-process); TDD was the
    proof. Temp files under `/tmp/test_*` self-unlink, matching existing
    `clean` globs.
- **2A-6** Close-out **DONE 2026-09-14:** `make` green (W06 check PASS,
  boundary-checks pass, all mods build; site is source-invisible to 2A —
  only sibling libs changed) + per-repo suites green above + `nm -D`
  spot-checks above; no installs. Phase 2A closed; §12 (2B) unlocked.

### 2A gates

Every touched repo's suite green (`make test`; libjoint `./test.sh`);
new symbols exported; round-trips green; site `make` green as insurance
(site is source-invisible). Commit only when explicitly asked.

---

## Phase 2B — general corm CLI composition

## 2B — what this phase must deliver

- A corm CLI capability to compose **multiple** registered axis libraries
  in one query: intersect/union/subtract their results (kernel joins over
  ref sets), then rank — triggered by the existing `-g` op, not a parallel
  mode.
- **General-purpose, not mm-specific.** The same capability must let, say,
  the space axis (libislet) compose with the time axis (libjoint) and the
  text axis (libstoma), with zero mm concepts involved. mm is one consumer
  of this capability, built on top as a documented set of invocations —
  never a separate code path.
- **Axis plugins are discovered by name**, not by an explicit file path
  the caller supplies per query, and not by a manually-numbered slot.
  (Superseded: an earlier iteration of this plan required the caller to
  name the plugin's file path and bind a slot number explicitly at every
  call site — decided against. Axes register themselves; the CLI resolves
  a name to an already-registered axis.) The `@` roster in the filespec
  declares the load set once, at creation; the file knows it afterwards.
- **A write-fanout mechanism**: one logical write lands correctly in the
  primary map and in every axis store that should index it — each axis
  parsing the same whole `value` string in its own grammar. This
  orchestration lives outside libcorm's core — a thin consumer-side layer,
  never a put/del hook inside the library.
- **Result materialization**: the winning set of refs from a composed
  query resolves back to the real record(s) in the primary map, not just
  bare numeric refs — rendered through the existing print machinery.
- **Both composition mechanisms stay.** The existing exact relational
  chain and the newer scored/ranked composition mode are both kept; the
  relational chain is untouched, the scored mode is triggered by `-g`.
  Neither replaces the other.
- **Legacy-close**: zero-axis invocations byte-identical; classic `-g`
  (keyed and `.`) unchanged when no effective query is armed.

## 2B — decided (do not re-open)

- Fan-out orchestration lives outside libcorm core.
- Capabilities (fill/rank) are query-time only — no put/del hooks, no
  per-map engine state inside libcorm.
- Both composition mechanisms (exact relational + scored) stay.
- Axis plugins are named and auto-discovered — never wired by an explicit
  file path plus a manually-numbered slot at the call site.
- All refs crossing this surface are the `uint32_t` corm refs described in
  `README.md` §2 — never an axis's own internal key.
- **D1 — write-fanout shape:** per-axis **conventional** store/unstore/get
  exports (same optional status as `rec_axis_open`, discovered via
  `qsys_dlsym`, **NOT** added to `rec.h`/libcorm core), with the single
  whole-string `(ref, value)` signature and axis-side roles. General for any
  future axis; the CLI stays generic (passes the whole string blindly +
  uniform surface
  rules). Implemented + unit-tested per axis in **phase 2A** (above); 2B
  only wires the dlsym lookup into the CLI. Fan-out attempts every target,
  reports every failure, exits nonzero naming the rejectors; idempotent
  compensation = forget. Single-writer; no cross-store transaction.
- **D2 — rank convention:** document "first rank-capable axis in query
  order wins" (kernel fallback, `rec_axis.c:199-211`) as the standing
  convention now; a `--score` sum/max/weighted combining flag is deferred
  (would ride the existing `rec_consumer_score` + `rec_axis_score_t[]` hook).
- **D3 — "relational aux chain extended to the same typed addressing":**
  means the new surface addresses stores by registered *name* (including
  the `@` roster in the filespec). The legacy `-q`/`-a` chain stays
  untouched; existing flat consumers demonstrably unaffected (D4's gate on
  `-g` with zero plugins).
- **D4 — installs:** none this phase. Read gates run on the phase-1
  `/usr` stack (`CORM_AXIS_PATH=/usr/lib`); write gates run on fresh
  in-site `external/lib*` builds via `CORM_AXIS_PATH`/`LD_LIBRARY_PATH`
  (the `~/lib*` sibling checkouts are gone since the 2026-09-15
  submodule migration).
- **D5 — axis autonomy:** what an axis does on put/delete/get, and its
  query language, are the axis's own decisions. The CLI never interprets
  axis grammars; it resolves names, threads refs, renders output.
- **D6 — query membership (reprased for the `-X` surface, 2026-09-14):** an
  axis joins the effective query iff its NAME appears in the `-X` set
  expression; a bare NAME (no `=VALUE`) is the explicit opt-in with empty
  params. Axes not named are write-target-only. Membership is determined by
  the expression alone — no pending-join or `--combine` state survives
  (both are retired with the fold).
- **D7 — roster create-only:** the `@` roster persists write-if-absent
  (creation or missing roster + `@` present); an existing roster + `@` =
  override-this-invocation-only, never persisted. Migration = classic
  dump/replay (`corm -k -l old` → replay `-p` into new).
- **D8 — sepal embedder config is env-only (locked 2026-09-14):**
  `CORM_SEPAL_EMBED_URL` + `CORM_SEPAL_EMBED_MODEL` (both required
  together — mirrors the `sepal_configure_embeddings` setter rule) and
  optional `CORM_SEPAL_EMBED_KEY`. Configured iff url AND model are both
  set; otherwise sepal stays unconfigured → string-store `EINVAL`, read-only
  (the offline default — identical behavior to today). Set exactly once in
  the inter-pass bind (2B-1), same stage as stoma bulk-reindex, so a
  write-then-query `-p` in the same invocation already has the embedder.
  Credentials never touch filespec, `spec` (reserved — 2A), roster, or disk
  (they exist only in the invoking env; honors the "spec never credentials"
  lock). No separate master switch; no config-file format invented (D4).
  The 2B-2 offline gate uses sepal **floats-direct**; the string→embed→ANN
  path through the CLI is proven by an env-gated second case (skipped
  unless the vars are set).
- **D11 — hash mask is a hint, not a cap (locked 2026-09-14 pre-2B-4):**
  `QDBE_MASK` (`external/libcorm/src/corm.c:163`) shrinks `32767 → 4095`
  (`2^12-1`, 4k buckets; was `2^15-1`, 32k — ~8× over-large for small
  corpora, mmap bloat). `corm_open(...,mask,flags)`
  (`external/libcorm/include/ttypt/corm.h:172`) auto-grows on overflow
  unless `CM_NOGROW` is set, so the mask is only an initial hint.
  `libstoma`'s sidecar-scan rebuild (`libstoma/src/libstoma.c:839`)
  mirrors the same default. Per-store override via env `CORM_MASK` for
  benches; no new CLI flag (keeps D9).
- **D12 — typed primary + additive `rec_axis_store_typed` (locked
  2026-09-14 pre-2B-4):** libcorm is already typed (`corm_reg`,
  `external/libcorm/include/ttypt/corm.h:662`); the axis store boundary
  gains additive
  `rec_axis_store_typed(ctx,spec,ref, const void *blob,size_t len,uint32_t qtype)`
  alongside the string `rec_axis_store` (keeps shipped `*.so`s valid).
  The CLI forwards `(ptr,len,qtype)` from `corm_get`+`corm_type_len(qtype)`
  and prefers the typed symbol when `vtype != CM_STR`, else the string one;
  `readback` already returns `blob/n`. Missing typed export ⇒ text-only axis.
- **D13 — derived (rebuild) vs persisted axis is deployment freedom, not
  per-axis hard-wired (locked 2026-09-14 pre-2B-4):** `rec_axis_open(spec)`
  decides persistence (file vs memory/rebuild) — the roster (`@joint`) names
  the *logical* axis only. `stoma` stays derived (memory+rebuild, zero files);
  `joint`/`islet` can be built either way (`<dir>/<name>.db` file if
  persisted, otherwise rebuild from the primary); `sepal` stays persisted.
  2B-4 fanout writes to whatever `ctx` the bound axis represents; a derived
  axis's write can be a no-op or an in-memory index that rebuild restores.
- **D9 — no per-call load/open flags (locked 2026-09-14):** there is no
  `--dl`/`--open`-style flag at any call site, and the old `-Q` surface
  carries neither a file path nor a manually-numbered slot. Axis discovery
  is **by name only**: the `@` roster (+ stored roster + names in the `-X`
  expression + `CORM_AXIS_LIBS`) names the load set; the CLI dlopens
  `lib<name>.so` by name through `$CORM_AXIS_PATH` (dir list, default
  `/usr/lib`); the ctx is bound by calling the plugin's `rec_axis_open`
  with an alongside-default spec derived from the primary store
  (`<primary-dir>/<name>.db`, or the axis's own memory/empty-field
  defaults); sepal embed config is env-only (D8). `-Q`, `--dl`, `--open`,
  `--axis` (any form), `--params`, and the `rq_*` helpers are retired and
  removed with the `-g` fold (2B-3). **Plugin-contributed `--<name>=VALUE`
  configuration flags are permitted** (D14): additive per-axis config
  defaults forwarded by corm to every bound axis that declares them, via the
  optional `rec_axis_cli_options()` / `rec_axis_config_arg()` convention —
  `-X` remains the only query *verb* (see the D14 carve-out below).
- **D10 — the query is ONE set-expression flag (locked 2026-09-14):**
  `-X EXPR` is the only query mechanism. `EXPR` is a set algebra over axis
  leaves — `( ) AND OR EXCEPT NOT`, `NAME` / `NAME=VALUE` (whole-string
  `VALUE`, optional, quote-or-keyword-delimited); any precedence is
  expressible (`(A OR B) AND C`, `A OR (B AND C)`, 3+ groups; relative
  setminus is the binary keyword `EXCEPT` — `A EXCEPT B` = `A − B`;
  `NOT` is **unary-only** — root/prefix `NOT` = complement against the
  primary ref universe, and `A NOT B` is a parse error with a `use EXCEPT`
  hint; `NOT A EXCEPT B` = `(complement A) − B`). The CLI parses only the
  set structure; axis grammars stay opaque (D5). Two result knobs:
  `-t N`/`--top N` (cap, default 0 = all) and `-b F`/`--bottom F` (score
  floor). Armed iff `-X` present and nonempty; unarmed = classic `-g`.
  Retired with the fold: sticky `--and/--or/--not`, `--combine`, per-axis
  `--params`. `AND/OR/EXCEPT/NOT/( )` are reserved (uppercase-only; axis
  names are lowercase slugs, so shadowing is effectively unreachable —
  `-X except` referencing an axis literally named `except` stays legal).
  This is the surface `mm-plan/CLI-SURFACE-EXAMPLES.md` documents;
  grammar/`-X` details live there and win.

> **D14 carve-out — `-X` remains the only query verb.** `--axis`/`--params`/
> sticky joins stay retired. **Plugin-contributed `--<name>=VALUE`
> configuration flags** are now permitted: additive per-axis config defaults
> forwarded by corm (an armed `-X` present) to every bound axis that declares
> them, via the optional `rec_axis_cli_options()` / `rec_axis_config_arg()`
> convention. Precedence: leaf spec > CLI arg > env. Inline `--name=value`
> only; bare `--name` is an error; credentials stay env-only.

> **Verified against the built 2B-3 parser (2026-09-15, 2B-6):** the
> grammar above is exactly what `external/libcorm/src/corm.c` (lexer +
> recursive descent, `corm_expr_*`) implements — token kinds, word rules,
> quote + whole-string `VALUE` semantics (operator/paren-terminated,
> silent-truncation precedence), unary-only `NOT` with the `use EXCEPT`
> hint, uppercase reserved keywords. Two operational notes from the
> prototype: (1) `EXCEPT` chains **left-to-right** (the n-ary `SUB(List)`
> notation means sequential subtraction) — pinned by the new
> `except-chain` row in `test-cli.sh`; (2) the ranker is the **first
> rank-capable leaf in preorder** — D2 standing convention, pinned by the
> new two-rankable row in `test-real.sh`.

## 2B — the `-g` fold (the composed get; `-Q` retired)

`-Q` is a get — get-against-a-composed-predicate — so it folds into the
get the CLI already had, sharing subroutines (`gen_lookup` ref resolution,
`corme_print` rendering, `assoc` tail, the two-pass loop, exit codes), with
an armed `-X` itself running the query (explicit `-g .` at its argv
position when present; once after all ops when absent). The classic `-g`
keeps its exact meaning everywhere else. Structural parallel (already the
CLI's own pattern): `-X EXPR : composed -g` :: `-q/-a chain : classic -g` —
predicate-building in pass 1, evaluation in pass 2 + end of main. The full
surface is documented in `mm-plan/CLI-SURFACE-EXAMPLES.md` (normative; D10).

- *Pass 1 (setup):* axis plugin loads/discovery (`CORM_AXIS_PATH`,
  by-name dlopen of `lib<name>.so`, name→slot resolution among loaded
  axes — no `--dl`, no `--open`), the `@`/stored roster load set (∪ names
  appearing in `-X EXPR`), the single query flag `-X EXPR` and its two
  knobs `-t N`/`--top N`, `-b F`/`--bottom F`.
  Inter-pass (after primary open, before pass 2): bind stores, stoma
  bulk-reindex. Opens must happen before pass 2 because pass 2 may not open.
- *Pass 2 (ops):* `-g .` with a nonempty effective query runs the composed
  query at its argv position, interleavable with `-p`/`-d`/`-g KEY`
  (write-then-query in one invocation — correct by 2A per-op durability);
  with no `-g .` present, an armed `-X` runs once after all ops (so `-g .`
  is no longer required — it now only pins the query's position).
  `-g KEY` is always the classic keyed get; the predicate never touches it.
  `-g .` with no `-X` (or an empty `-X`) is classic all-records (so
  write-only invocations can never surprise).
- *Ref-operand rule:* literal u32, else the primary reverse-view name
  lookup (the same iteration `-g` performs today — `corm -g NAME
  file:a:s` already prints the id, no chain involved); miss → named error.
  The whole `-p`/`-d` argument is the value; corm never splits it — each
  axis parses the whole string itself (2A roles).
- *Reuse:* rendering goes through `corme_print` with the existing
  `-k`/`.`/aux-chain conventions — never a parallel print path.
- *Rendering (locked):* ref-led always — `ref[ score] record` per line
  (score iff ranked; best-first, ties asc ref; pure-filter asc). File-less
  degrades to `ref[ score]` (today's `-Q` bytes — mock tests survive).
  Dangling refs (no primary record): skipped with a stderr count, exit 0
  (reads degrade; writes fail). `-k`/`-r` ignored under armed `-g .`.
- *Compat:* flat CLI behavior stays byte-for-byte for existing consumers
  and for invocations with no effective query. `-Q` (guard,
  `corm_recall_query`, `--dl`, `--open`, `--axis`, `--params`,
  sticky joins, `--combine`, `rq_*` helpers) is deleted atomically in the
  fold slice; `test-cli.sh` is rewritten the same slice (`-X EXPR` on
  every query case — incl. `-X`-armed full `ref score record` lines and
  the `-t`/`-b` knobs — primary-seeded asserted output, `--list-axes` as
  its own pass-2 op needing no file).
- *Failure semantics:* exit code threaded through pass-2 ops (today `main`
  returns nothing); every query axis pre-validated (slot valid + `ctx`
  non-NULL) with a named error **before running the composed query** — this
  closes the recall hole the kernel's silent fill-skip would otherwise
  open, with no kernel change. Mid-run fill failures keep kernel skip
  semantics, documented. Note: D10's parens/precedence/complement cannot
  be expressed in the linear `rec_query_t`, so the CLI evaluates the
  parsed tree directly on the kernel's own `rec_set_intersect/union/
  subtract` + `rec_rank_*` — still kernel API, no shell scripts; the CLI
  just never calls `rec_query_run` (it remains a library API for linear
  consumers).
- *Constraints to respect:* pass-1-only opens (two-pass ordering
  guarantee); ops run in argv order; long-only options must not collide
  with the short `optstr`; type/print resolution completes before pass 2.

## 2B — the `@` roster (load set + fan-out; `file@a,b:k:v`)

`data.db@stoma,joint:a:s`: from the first put, the CLI knows what axes to
load. `@` declares the load set and the write fan-out roster — nothing
else (never query membership: `@` can't carry `NAME=VALUE`, so D6/D10
exclude it by construction).

- *Parse:* first `@` splits the roster (csv of axis names, order kept for
  determinism) from the file path; the `:k:v` types still describe the
  primary map.
- *Inter-pass load:* load set = `@` ∪ stored roster ∪ names appearing in
  `-X EXPR` ∪ `CORM_AXIS_LIBS`, deduped by name (D9). Each named axis
  dlopen'd **by name** (from
  `CORM_AXIS_PATH`, dir list, default `/usr/lib`) unless already loaded;
  stores bound via **alongside-defaults** — the CLI calls each axis's
  `rec_axis_open` with a spec derived from the primary store's location
  (`<primary-dir>/<name>.db`; islet relies on empty-field defaults;
  stoma memory-opens + bulk-reindexes; sepal embed config is env-only,
  D8). No `--dl`, no `--open`, no per-call spec flag. Unknown name →
  named error.
- *Write set:* each `-p`/`-d` writes `{primary} ∪ {@ roster} ∪ {current
  target}`, each store exactly once (union deduped by slot). The whole
  `-p` string is the value; it fans out as `(ref, value)` to every target
  and each axis parses the entire string in its own grammar (2A roles).
  Fan-out attempts every target, reports every failure, exits nonzero
  (loud partials; compensation = idempotent forget — D1). `-d`/`-D` both
  mean unstore-all on axis targets (documented collapse).
- *Persistence:* the roster persists write-if-absent in map `"@roster"`
  (keys `v`=`1`, `axes`=csv) in a **sidecar** file — same dir as the
  primary, named `<primary>.roster` (e.g. `data.db` → `data.db.roster`);
  one-map-per-file invariant. Written fresh (write-if-absent) when an
  explicit `@` first names it; read back by every later invocation; opened
  only when present (no `@` + no sidecar = classic path, nothing extra). Explicit
  `@` on an existing roster overrides this invocation only (override-once,
  never written). Old-binary bare opens read fine (prefix-subset) and can
  only clobber the roster block — recoverable by re-specifying `@`, axis
  data never at risk (separate files). Heuristic: the CLI stats
  alongside-default paths and notes when axis stores exist but no roster
  does (loud hint, exit 0; implemented 2B-1).
- *Prototype-time resolutions (locked 2026-09-14 by the 2B-1 build):*
  sidecar filename `<primary>.roster`; alongside-default spec is the
  literal `<primary-dir>/<name>.db` (D9); `--list-axes` added long-only in
  2B-1 (flat `getopt_long`, renders `slot name fill rank ctx` after load);
  `@`-in-filename needs quoting (same class of footgun as `:`); the sepal
  embed config setter (D8) and stoma memory-open/reindex are real-lib
  responsibilities deferred to the 2B-2 wiring — the CLI itself stays pure
  load+bind (`rec_axis_open(spec)` only, never axis-specific).
- *Overhead when unused:* exactly zero — no extra opens, parses, or
  blocks on the classic path. Per-use cost is inherent (N grammars need N
  parses; N stores need N writes). Same-dir separate files is the 2B
  layout (works today); same-file co-location stays deferred (subset-order
  opens load empty maps and the exit-save clobbers — needs a libcorm
  roster guard, i.e. kernel change, out of 2B scope).

## 2B — the shape in one command (the whole intent, end to end)

```sh
corm -p "<SOME-DATE>:<SOME STRING>" "demo.db@stoma,sepal,joint:a:s"
```

This one line is the entire contract in miniature. What happens, in order:

1. **Primary put.** `demo.db` is `:a:s` — key type `a` (auto-index), value
   type `s` (string). corm auto-assigns a fresh ref and stores the **whole**
   argument `<SOME-DATE>:<SOME STRING>` as the value. **The value format is
   not strict** — the primary stores whatever string it is given. Nothing is
   split, no key is extracted, nothing is validated.
2. **Fan-out.** The CLI passes `(ref, value)` to **each** roster axis —
   `stoma`, `sepal`, `joint` — the same ref, the **same entire string**,
   verbatim. corm has zero per-axis knowledge: it never splits the string,
   never pulls out a date, never decides what a "key" is.
3. **Each axis parses the whole string itself, in its own grammar.**
   - **sepal** reads the string's content and **generates embeddings for it**
     (using whatever embedder it is configured with) → a semantic vector
     store rowed by ref → later `sepal=…` (ANN) queries find nearby refs.
   - **joint** reads the leading date out of the string → that ref exists
     from that time (open interval) → interval membership queries.
   - **stoma** FTS-indexes the string under the canonical `text` field →
     lexical/token queries find the ref.
   Axes are **strict but never forced**: an axis that can't understand a
   string's format rejects it (loud, `EINVAL`) while the other axes — and
   the primary — are unaffected.
4. **Query with the `-X EXPR` set expression.** Each axis is asked through
   its own `NAME=VALUE` in the expression; leaves join over ref sets
   (intersect/union/subtract), ranked by
   the rank-capable axes, rendered ref-led by the folded `-g` (D10).

The division of labour is the whole point: **corm stores and fans out; the
axis parses.** The ref (the auto-index) is the "value stored" by every axis;
the **string is the payload the axis turns into something queryable** — a
semantic index, a timeline, a token index.

## 2B — execution plan (decided 2026-09-13; reserialized v18)

Slices (TDD; gate per slice = its own suite + `./test.sh &&
./test-cli.sh` + touched sibling suites; commit only when asked):

- **2B-0** CLI-surface doc correction **DONE 2026-09-14** (README
  width-lie fix + `-Q`/`--dl`/`--open` retirement documented):
  `external/libcorm/README.md:125-126,138-139` (previously "uniform
  64-bit refs", `libit`/`libgeo`) → u32 + the four real axis names;
  `docs/RECALL-KERNEL.md` `rec_axis_open` convention + Status rewritten
  to the by-name surface; CHANGELOG `libit`→`libjoint`; `external/libsepal`
  header doc sibling names updated. Content-only.
- **2B-1** Discovery + roster + load **DONE 2026-09-14** (CLI-only, kernel
  untouched): `@roster` parse in `gen_open` (first `@` before the
  `:`-parse); sidecar `<primary>.roster` write-if-absent / override-once /
  stored-read; inter-pass load+bind (name→slot among loaded axes, else
  lazy by-name dlopen of `lib<name>.so` from `$CORM_AXIS_PATH`,
  `rec_axis_open` on alongside-defaults `<dir>/<name>.db` — no
  `--dl`/`--open`, D9); `--list-axes` added long-only;
  stoma/sepal specifics deferred to 2B-2 real-lib wiring (CLI stays pure
  load+bind);
  numeric `--axis SLOT` kept only as `-Q`-legacy until its deletion
  (2B-3); `CORM_AXIS_LIBS` stays. Single-axis stub plugins
  `libstub.so`/`libzed.so` built via the standalone-rule pattern (avoids
  the multi-LIB Makefile aggregation bug). TDD: `test-roster.sh` in
  `make test` — all green.
- **2B-2** Non-mm space∩time∩text gate over real files (DONE 2026-09-14):
  fitness of the built surface: `test-real.sh` seeds the same refs 1/2/3
  into real primary (`:a:s`) + joint/islet/sepal stores, seeder
  `tests/real_seed.c` writes the sepal qvec/qdim; stoma rebuilt from the
  primary at each open via the **sidecar-scan in libstoma** (scans
  `<dir>/*.roster` → `<dir>/<base>` as
  `corm_open(…,"hd",CM_HNDL,CM_STR,32767,CM_AINDEX|CM_MIRROR)` + bulk
  re-index; budget note on stderr on every bound open, well under budget
  at 3 docs: ~0.02–0.03 ms, U4 measured and recorded — no persist-postings
  proposal at this scale); sepal two columns: offline floats-direct +
  env-gated embed (string store + ANN, `rec_axis_env_config` hook per
  plugin; D8); loud first-@ alongside + stoma notes prove the empty-index
  path is never silent. Asserts (with the real grammars in
  `CLI-SURFACE-EXAMPLES.md` §6 and the exact surface spellings,
  `field=text query=… matched=1`, `dim=2 s=9,1 l=1,1`,
  `a=2026-09-14 b=2026-09-15`): `--list-axes` all four `ctx=y` + budget +
  conjunctive winner `3 0.125000 2026-09-14T…Beacon Harbor lights` +
  sepal floats `3 1.000… / 1 0.906…` (+ embed-string column structural
  when the vars are set) + plain `corm -g` on a fresh primary. Bring-up:
   libjoint burns handle 0 once per process in `rec_axis_open` (the
   jd-0↔NULL collision) + §6 grammars corrected. **Runs AFTER 2B-3** (the
   fold lands the surface once). Wired into `make test` (gates per slice +
   `./test.sh && ./test-cli.sh && ./test-roster.sh && ./test-fanout.sh &&
   ./test-real.sh` + sibling suites; clean-rebuild rule). **DONE
   2B-4 2026-09-15 — next: 2B-5.**
- **2B-3** The `-g` fold + the `-X` surface: one `getopt_long` loop over
  the extended optstr; `-X EXPR` parsed as a set algebra (D10 — leaves
  `NAME`/`NAME=VALUE`, `( ) AND OR EXCEPT NOT`, precedence; the CLI parses set
  structure only, leaf grammars stay opaque); an armed `-X` runs the
  effective query — at an explicit `-g .`'s argv position, else once after
  all ops (D6) — and renders ref-led lines; result knobs `-t N`/
  `--top N` (default 0 = all) + `-b F`/`--bottom F`; load set extends to
  names in `EXPR` (D9); `-Q`, its guard, `--dl`, `--open`, `--axis`,
  `--params`, sticky joins, `--combine`, and the `rq_*` helpers deleted;
  `test-cli.sh` rewritten against `CLI-SURFACE-EXAMPLES.md` (every query
  case via `-X`, primary-seeded full `ref score record` lines, `-t`/`-b`
  cases, `--list-axes` standalone); slot+ctx pre-validation with named
  errors; exit code threaded through pass-2 ops; dangling-ref
  warn-and-skip. Each surface step starts with a small integration spike
  and stays red until the folded shape passes.
  **DONE 2026-09-14** — implemented per `2B-3-IMPLEMENTATION.md` (§2–§4:
  lexer + recursive-descent parser + tree eval on `rec_set_*`, composed
  get with rank/pure-filter render); `test-cli.sh` 25/25, full `make test`
  green, stale-term grep clean. Bring-up fixes: fold-plugin `rec_axis_open`
  uses database `"hd"` + CLI mask (corm namespaces records by dbid);
  `rec_rank_free(NULL)` guard; classic rows pinned to verified pristine
  behavior.
- **2B-4** Write fanout + forget/reset (D1 — with D11..D13, pre-2B-4):
  CLI wiring only — union write-sets for `-p`/`-d`
  (`{primary} ∪ {@} ∪ {target}`, each store once; whole `-p` *payload* fans
  out as `(ref, blob,len,qtype)` to every target, typed when `vtype !=
  CM_STR` via additive `rec_axis_store_typed` (D12) else the string
  `rec_axis_store`, with a loud skip for binary-payload-on-text-only
  axes); dlsym `store(_typed)`/`unstore`/`readback` (missing
  store ⇒ read-only); loud partials (attempt all, report all, nonzero) with
  idempotent forget as compensation; `-d`/`-D` collapse on axes;
  `QDBE_MASK` `4095` (D11, `CORM_MASK` env override) and
  derived-vs-persisted freedom per `rec_axis_open` (D13). **Targeted
  `-g`/`-m`/`-c` readback via `rec_axis_readback` deferred 2026-09-15:**
  `readback` is dlsym'd (capability) but has no CLI surface — query +
  write cover the flow; `-g`/`-m`/`-c` stay byte-identical. Mock
  store/unstore/readback round-trip tests done in 2A-mock.
  Single-writer; no cross-store transaction.
  **DONE 2026-09-15** — `src/corm.c` fan-out + capability table,
  write-capable fold plugin + string-only plain plugin, `test-fanout.sh`
  green, full `make test` + sibling suites + site `make` green.
  Detail: `2B-4-IMPLEMENTATION.md`.
- **2B-5** mm dialect as *documented invocations* on the same surface
  (store → composed search → forget → reset; recipes are exactly what
  pi-mm will issue in phase 3; exact spellings written from the built
  surface — U3 **SETTLED 2026-09-15**).
  **DONE 2026-09-15** — `external/libcorm/test-mm.sh` (real joint+stoma,
  store/search/forget/reset loop, `-1` sentinel, classic regression) +
  `CLI-SURFACE-EXAMPLES.md` §8 rewritten to the proven recipes +
  **F4 fix**: `corm_open` aliases the live handle when the same
  (file, map) is opened twice with the same shape (libstoma's sidecar
  mirror-open no longer orphans the CLI's primary, so seeds and `-d`
  forgets persist). Detail: `2B-5-IMPLEMENTATION.md`.
- **2B-6** Rank convention documented (D2 — **first rank-capable axis in
  query order wins**; kernel fallback `rec_axis.c:199-211`; `--score`
  combining deferred); `-X` grammar verified against the built 2B-3
  parser (chained-`EXCEPT` left-assoc pinned in `test-cli.sh`,
  two-rankable stoma-over-sepal pinned in `test-real.sh`); plan bumped.
  **DONE 2026-09-15.** Detail: `2B-6-IMPLEMENTATION.md`. **Phase 2B closed.**

**Final gate:** three-axis (`test-real.sh`) + mm-dialect (`test-mm.sh`)
green; `./test.sh && ./test-cli.sh` green; sibling suites green; site
`make` green; no new installs.

## 2B — gate

A non-mm composition (e.g. space ∩ time ∩ text, three different axis
libraries, zero mm involvement) works end to end over real files; the mm
dialect (store → search → forget/reset) works end to end over real files
using the exact same underlying capability; a query with no effective
axes still answers a plain corm lookup; existing flat-map consumers are
unaffected.

## 2B — deferred (decided, not unclear)

- `-m`/`-c` over composed result sets.
- `--score` combining across rankers.
- Same-file co-location (needs a libcorm roster guard — kernel change).
- Plugin-ABI embedder (revisit iff a second embedder shape appears).
