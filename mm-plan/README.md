# MM Plan — Memory Mipmaps + general qmap composition

> Single home for this plan. Whole plan lives in this one doc; phase detail
> docs (only where a phase genuinely needs one) live beside it in this same
> directory. Nowhere else — not `.pi/quest/`, not scattered root markdowns.
> Canonical kernel API reference (library-owned, unchanged in location):
> `external/libqmap/docs/RECALL-KERNEL.md`.
>
> Working rules: TDD every increment; execution tracking lives in this
> plan itself (per-phase status + checklists — no `.pi/quest/` usage, by
> explicit decision 2026-09-13); **commit only when explicitly asked**;
> `make` + full suites green as a gate.

Version: v19 (2026-09-15) — phase 1 DONE (all five repos, fixture
16/16, site `make` + `make test` green). Phase 2 stores **the whole
`-p` string as one un-split value** (see §10, §11). Phase 2 re-scoped
around four principles (detail `PHASE-2-CLI.md` header)**: axes decide
put/delete/get** (CLI passes `(ref, value)` blindly — one opaque string,
never split; each axis parses the whole string in its own grammar);
**axes own their query language**;
**the CLI conjunctions axis results** (AND/OR/NOT over ref sets); **qmap
stays legacy-close** (zero-axis invocations byte-identical, all new surface
additive). **2A — per-axis store/unstore/readback** (DONE, §11) and
**2B — general qmap CLI composition** (DONE 2026-09-15, §12; the
`-Q` mode folded into a composed `-g`, the `@` filespec roster declares the
load set once at creation; 2B-0..2B-6 all landed; §12 checklist all green).
§10 catalogs every
operation kind the four axes answer, store/unstore/get included
(source-verified; each repo's own README carries its matching catalog).
Supersedes v17/v16/v15/v14; still supersedes `MM-PI-MM.md` (deleted) and
`external/libqmap/docs/QMAP-AXIS-CLI.md` (deleted, was superseded/never
implemented).

---

## 0. Status at a glance

| # | Phase | What it is | Status |
|---|---|---|---|
| 0 | Recall kernel substrate | `rec.h`/`rec.c` in libqmap; a working `qmap -Q` scored-query mode (dlopen'd axis plugins, boolean AND/OR/NOT combine, top-k/min-score ranking) — tested, green; 4 axis libs registered via `rec_axis_register` + `rec_axis_open(spec)` | ✅ DONE — `-Q` retired in 2B (replaced by the by-name composed `-g`) |
| 1 | Ref-type law: `rec_ref_t = uint32_t` (qmap ref, not an axis's internal key) | ✅ **DONE 2026-09-13** — all five repos retyped + installed + verified; fixture 16/16; site `make` + `make test` green. Detail: `PHASE-1-RETYPE.md` | ✅ DONE — gate (§6 item 1) green |
| 2 | 2A — per-axis store/unstore/readback | Conventional `rec_axis_store(ctx, spec, ref, value)` / `rec_axis_unstore(ctx, ref)` / `rec_axis_readback(ctx, ref, blob_out, n_out)` exports per axis (`readback`, not `get` — `rec_axis_get(int)` is the landed registry lookup; U2 settled 2026-09-14): the CLI passes the **whole** `-p` string as `(ref, value)` — never split — and each axis parses it entirely in its own grammar (joint leading-date, sepal floats-or-configured-embed, stoma memory-only text, islet point lists); unstore walks the axis's existing inverse — only libislet adds new stored state (`rev`, ref→cells); uniform idempotent contract + surface rules; missing symbols ⇒ read-only axis; no kernel/libqmap change. Detail: `PHASE-2-CLI.md` 2A; contract: `RECALL-KERNEL.md` | ✅ **DONE 2026-09-14** — mock proof-of-contract (`rec_axis_store_test`, libqmap `make test` green) + libsepal adapters with real lazy-curl embedder (`test_axis_store` 70 assertions green, valgrind clean) + libstoma `stoma_unindex`/`_ref` + `text`-field adapters (`stoma_test` 227/227, `stoma_axis_store_test` 34/34, 5 symbols) + libjoint `joint_erase` + ordered-grammar adapters (`joint_axis_store_test` 116/116, 4 symbols) + libislet `rev` + `islet_del_value_N` + point-list adapters (`test_axis_store` 15 tests/193 assertions, 9 symbols) + **2A-5 cross-process round-trips** (sepal `sepal_search` + isolation, islet `.ridx` rehydration + `fill_bbox_2`, joint `joint_iter` + file-backed `id` backfill — each via self-exec harness `seed/verify1/unstore/verify2`; stoma corpus `{(1,"Beacon Harbor lights"),(2,"Beacon AND Pão"),(3,"alpha omega")}` store→beacon 2→unstore 1→harbor miss→rebuild minus dropped 3 → beacon {1,2}+harbor {1} again, omega gone, parity 43/43) + **2A-6 close-out** (`make` W06 PASS, all suites green, `nm -D` spot-checks, no installs). Next: 2B |
| 3 | 2B — general qmap CLI composition | The `-Q` mode folds into a composed `-g` (same trigger shape as the `-q` chain vs classic `-g`); axis plugins auto-discovered by name; `@` filespec roster declares the load set once at creation (persisted in a <primary>.roster sidecar, fan-out for `-p`/`-d`); string-unified writes over the phase-2A store surface; mm dialect as invocations of this same surface — **not mm-only**, must also compose e.g. islet-with-others. Detail: `PHASE-2-CLI.md` (2B) | ✅ **DONE 2026-09-15** — 2B-0 + 2B-1 (by-name discovery/load + `@roster` sidecar + `--list-axes`, `test-roster.sh` green 2026-09-14); decided options (D1-D10 + D11-D13); **D10 = the `-X EXPR` set-expression query** (`CLI-SURFACE-EXAMPLES.md`); 2B-3 (fold, `test-cli.sh` 25/25, `2B-3-IMPLEMENTATION.md`); 2B-2 (real-file gate, `test-real.sh` green, `CLI-SURFACE-EXAMPLES.md` §6); 2B-4 (write fan-out + forget, `test-fanout.sh` green, `2B-4-IMPLEMENTATION.md`); 2B-5 (mm dialect, `test-mm.sh` green, F4 `qmap_open` aliasing, `2B-5-IMPLEMENTATION.md`); **2B-6 (rank convention D2 — first rank-capable axis in query order wins, `--score` deferred — pinned in `test-cli.sh`/`test-real.sh`; `-X` grammar verified against the built parser). Phase 2B closed — next: phase 3 (`pi-mm`)** |
| 4 | `pi-mm` extension | Tools/hooks/skill/config wrapping phase 2 (`memory_scan/think/store/forget/reset`) | 🔴 NOT STARTED |
| 5 | Time/space in the extension | `memory_store --until/--near` via joint/islet through the phase-2 surface | 🔴 NOT STARTED |

---

## ▶ Resume here first

Phase 1 is **DONE** (gate §6 item 1 green 2026-09-13). **Phase 2A is DONE
2026-09-14** (all six slices: 2A-mock + 2A-1 libsepal + 2A-2 stoma + 2A-3
joint + 2A-4 islet + 2A-5 cross-process round-trips + 2A-6 close-out —
`PHASE-2-CLI.md` 2A, §11 checklist all green, site `make` W06 PASS,
per-repo suites green, `nm -D` spot-checks, no installs). **2B started:
2B-0 (CLI-surface docs corrected) + 2B-1 (roster + discovery + load)
DONE 2026-09-14** — `@roster` sidecar `<primary>.roster`, by-name dlopen
(`lib<name>.so` from `$QMAP_AXIS_PATH`), alongside-default bind, flat
`--list-axes`; `test-roster.sh` green. **2B-3 DONE 2026-09-14** — the `-g`
fold + the `-X` set-expression surface landed first (EXCEPT-setminus,
unary-only NOT; `-t`/`-b` knobs; `-Q`/`rq_*` deleted; `test-cli.sh`
25/25 green against the `librec_axis_fold` plugin; `make test` fully
green, stale-term grep clean; detail: `2B-3-IMPLEMENTATION.md` §9).
**2B-2 DONE 2026-09-14** — non-mm space∩time∩text gate over the same refs
(joint+islet+sepal+stoma: the conjunctive winner as real `ref score record`
lines; sepal floats + env-gated embed; stoma sidecar-scan rebuild, budget
measured; §12 checklist; bring-ups: joint jd-0 burn + §6 grammar).
**2B-4 DONE 2026-09-15** — write fan-out + forget landed (union
`-p`/`-d`/`-D` over `{primary} ∪ {@} ∪ {target}`; string/typed dispatch
D12 with binary+text-only loud-skip; loud partials + idempotent forget;
`QDBE_MASK` 4095 + `QMAP_MASK` D11; readback has no CLI surface —
deferred; `test-fanout.sh` 40/40 green; detail:
`2B-4-IMPLEMENTATION.md`).
**2B-5 DONE 2026-09-15** — mm dialect landed: pi-mm recipes
(store explicit-ref, bounded-window joint ∧ stoma search, idempotent
forget, enumerate+forget reset loop) proven over real joint+stoma files
by `test-mm.sh` green; **F4 fix**: `qmap_open` aliases the live handle on
identical (file, map) re-opens (stoma sidecar mirror no longer clobbers
seeds/forgets); detail: `2B-5-IMPLEMENTATION.md`. **2B-6 DONE 2026-09-15**
— rank convention landed (D2: first rank-capable axis in query order
wins; `--score` deferred) + `-X` grammar verified against the built
parser (chained-`EXCEPT` + two-rank pins in `test-cli.sh`/`test-real.sh`);
detail: `2B-6-IMPLEMENTATION.md`. **Phase 2B closed — next: phase 3
(`pi-mm`).** TDD per slice, commit only when asked.

---

## 1. Vision

Memory Mipmaps give a coding-agent session **persistent, hierarchical,
low-cost recall** across compaction and sessions, without holding raw
transcripts in context and without requiring a model, a daemon, or a
network connection:

| Level | Resolution | Key form | Example |
|---|---|---|---|
| 2 | summary | `topic@month` | `mirror@2025-05` |
| 1 | condensed | `topic@timestamp` | `mirror@2025-05-14T1705` |
| 0 | raw | `@timestamp` | `@2025-05-14T1705` |

The mipmap dialect becomes **agent tools** in a pi extension (`pi-mm`):

| Memory Mipmaps command | `pi-mm` tool |
|---|---|
| `memory:scan "topic" level 2` | `memory_scan(topic, level=2)` |
| `memory:think extract:intention` | `memory_think(key, extract)` |
| `memory:store timestamp […]` | `memory_store(...)` |
| `memory:forget topic\|tag` | `memory_forget(key)` |
| `memory:reset` | `memory_reset()` |

Boundary rule with `pi-quest`: **quest = current work state; MM = episodic
history.** Findings/decisions live in the quest; what happened-when-and-why
lives in memory. Don't duplicate.

**Non-goals:** no `~/llm` dependency (not `llmd`, not llama.cpp, no
local-embedding requirement); no remote memory storage (memory is local
data — a `qmap` file, never leaves the machine).

**A second, equally important goal:** the qmap CLI composition surface
built for phase 2 is **general-purpose**, not an mm-only mechanism. It must
let any registered axis libraries compose with each other — e.g. libislet
(space) in conjunction with libjoint (time) and libstoma (text), with no mm
concepts involved — with mm as one documented consumer on top.

---

## 2. The data model & the ref-type law (locked)

A memory (or any record) is a **value V** (a string, a vector, whatever)
referenced by an **id** that lives in a **primary qmap map** (its own file,
`QM_AINDEX`-opened — the id authority). Each id is looked up in key-lookup
sets built by axis libraries:

- **Set I** — interval membership (time): libjoint inserts times.
- **Set S** — semantic/spatial/lexical membership: libsepal (ANN over
  embeddings), libislet (bbox/space), libstoma (lexical/FTS).
- Query = **boolean composition** (AND/OR/NOT, ∅-tolerant) of these sets →
  winning ids → **materialize the real record** from the primary map.

**The law, stated precisely (this is what "axis id-uniformity" actually
means — get this right and everything else follows):**

1. **A qmap ref is `uint32_t`.** It is whatever `qmap_put()` under
   `QM_AINDEX` handed back for the primary record. This is the one
   reference type that crosses every boundary: axis fill functions, `rec_set_t`,
   `rec_rank_t`, `rec_query_run`'s output arrays.
2. **An axis's own internal keys are a completely different thing and are
   never confused with the ref.** libislet's morton keys are `uint64_t` —
   fine, that's islet's own spatial index, entirely internal. libjoint's
   internal secondary index, libstoma's inverted index, libsepal's sketch
   table — all internal, all axis-owned, any width they like.
3. **An axis search returns qmap refs** (`uint32_t`), stamped alongside
   whatever internal key got it there. Never the internal key itself.
4. **Set operations (AND/OR/NOT) run over qmap refs.** This is the kernel's
   entire job (`rec_set_t`, `rec.h`) — sorted merge-join, no hashing, no
   axis-specific knowledge.
5. **The real record is fetched last**, by ref, from the primary map (or
   whichever map owns that content) — never before the composition is done.

Consequence for `rec_ref_t` (the kernel's carrier type): it is `uint32_t`,
matching qmap's own native id width, not the wider width that would be
needed to embed axis-internal keys (those never leave their axis). Phase 1
is the work of making every axis speak this at its fill/rank boundary,
independent of what it stores internally.

---

## 3. Architecture (what phase 2 builds on)

- **One primary map** (its own qmap file, auto-indexed) is the id authority
  and (for mm specifically) the only place content lives. Writes are
  explicit multi-call sequences: put primary → same ref into each axis
  store.
- **Each axis keeps its own qmap-backed store** (own file — "their own
  qmaps if need be"). No shared physical file is required — only the ref
  value is shared.
- **Types are the language:** typed-file slots accept registered type
  names (a time store, an embedding store, ...) exactly like today's
  built-in type shorthand (`u`/`s`/`a`, see `external/libqmap/README.md`).
  Representative types live in core (`name`/`parse`/`print` + optional
  **query-time-only** `fill`/`rank` on the type vtable); capabilities live
  outside core. **No put/del hooks, no per-map engine state in libqmap** —
  derived indexes rebuild on query, plugin-side. The live data plane is
  untouched.
- **Composition, both mechanisms kept:** the existing exact relational
  chain, extended to the same typed addressing; and the scored/ranked
  set-composition mode (`rec_query_run`). Neither replaces the other.
  `rec.h`/kernel unchanged in shape (only the ref width changes).
- **No bespoke binary:** the mm dialect is documented qmap invocations
  issued from `pi-mm`'s `mm/client.ts`, not a separate engine. Old
  `external/mm` stays deleted once phase 2/3 land.

---

## 4. Preserved contracts (do not re-litigate)

- **Mipmap keys never contain `:`** — libqmap record maps treat any `:` in
  a key as a `struct:field` composite lookup. All keys are `mm_keyify`'d
  (colons stripped); the stored `ts` field keeps the real ISO string.
- **One qmap map per file.** libqmap saves/loads multiple maps sharing one
  file *positionally* — across processes (or a different open order) that
  silently skips blocks. Every store this plan touches (entries, ids,
  vectors) gets its **own file**.
- **Never `qmap_close` the long-lived handles.** libqmap's
  `__attribute__((destructor))` runs `qmap_save()` after `main`; closing a
  map first removes it from `file->ids`, so the destructor sees an empty
  file and truncates it to 0 bytes. Single-writer, no locking; forget
  deletes everywhere it was written.
- **Dim policy for vectors:** dimension-tagged per record, `0 < dim ≤
  MM_VEC_MAX` (2048, matches `libsepal`'s own cap); **mismatched dims are
  skipped, never forced**; over-cap is an error, never silent truncation.
- **Degradation, never error.** Semantic search is a routing aid, never the
  sole index — time/topic/text recall always work with no provider and no
  plugin loaded. Missing/absent provider only degrades its own dependent
  feature.
- **Provider policy (final — do not re-open):**
  - *Inference* (summaries, `think`): defaults to the agent's own model;
    optional override, any OpenAI-compatible `POST <url>/v1/chat/completions`.
  - *Embeddings* (optional semantic layer): default **off**; when wanted,
    any OpenAI-compatible `POST <url>/v1/embeddings`.
  - *Storage is never a provider* — the engine + its files are always
    local.
- **Accent-sensitive text search** — same fold as site search
  (`pão` ≠ `pao`), via `stoma_fold`, no locale/`iconv` dependency.

---

## 5. Phase 1 — ref-type retype ✅ DONE 2026-09-13

Started 2026-09-13, closed same day. All five repos retyped +
installed + verified; fixture 16/16; site `make` + `make test` green.
Research pass 2026-09-13 source-read all four axis libs and the
build/install wiring; the remaining scope is smaller and more mechanical
than first assumed — but nothing downstream of libqmap can be *verified*
until the new headers and binaries are propagated, not just edited.
Status, evidence, execution checklist, and decision log:
`PHASE-1-RETYPE.md`.

Done so far (`external/libqmap` working tree only, uncommitted):
- `rec.h`: `rec_ref_t` retyped `uint64_t` → `uint32_t`; doc comments
  updated (refs are qmap refs, not axis-internal keys;
  `rec_set_fill_qmap_iter` now accepts keys ≤ 4 bytes, was ≤ 8).
- `rec.c`: no changes needed — fully generic over `rec_ref_t`.
- `src/rec_test.c`: one test (`fill_qmap_iter duplicates`) was registering
  8-byte qmap keys — fixed to 4 bytes (now matches the ref width it feeds).
- `src/bench_rec.c`: was registering an 8-byte qmap key type while reading
  4-byte `rec_ref_t` array elements by pointer — a real buffer over-read
  bug this retype exposed. Fixed to `qmap_reg(sizeof(rec_ref_t))`.
- Verified: `test.sh` and `test-cli.sh` green after both fixes.

### 5.1 The propagation gap (the actual blocker)

The retype exists only in the submodule working tree. The live system the
axis libs build and link against is still u64:

- `/usr/include/ttypt/rec.h` still reads
  `typedef uint64_t rec_ref_t;` (installed Sep 11); `/usr/lib/libqmap.so`
  and `/usr/lib/lib{joint,sepal,stoma,islet}.so` were all built against it.
- The id-join fixture (`external/libqmap/tests/Makefile`) links the
  *system-installed* `libqmap.so` + joint/sepal `.so`s and asserts
  header≡installed-copy byte-identity — it cannot pass until the new stack
  is installed, whatever the sources say. Rebuilding an axis lib now would
  link a u64-ABI `libqmap.so` under u32 headers (real breakage in
  `rec_set_push`/`rec_rank_push`/`rec_query_run`, not cosmetic).
- Rebuild order is forced: **install libqmap first.** `libsepal` resolves
  `rec.h` from `external/libqmap/include` (explicit `-I` ahead of
  `-I/usr/include` in its Makefile); `libjoint`/`libstoma`/`libislet` have
  no explicit `-I` and resolve `<ttypt/rec.h>` from `/usr/include`. Only
  after libqmap's install do the axis repos see u32 `rec.h`.
- Installs land in root-owned `/usr` (same as the Sep 11 installs): exact
  `sudo make install` commands are handed to the user to run; the
  assistant does everything else. (Decided 2026-09-13.)

### 5.2 Per-axis findings (source-read, 2026-09-13)

Every axis lib was written parametric over `rec_ref_t`, so the retype
changes essentially **zero functional lines**. What remains per repo:

- **libsepal** (`external/libsepal`): `sepal_hit_t.ref`,
  `sepal_put/del/get/dim/full_dim/rank`, `idx_ref[]`, and
   `sepal_kt = qmap_reg(sizeof(rec_ref_t))` (now 4 bytes, was 8) all
   self-adapt; the u64 slot map (`ref_hash`, `hkey`) stays correct for
   zero-extended u32 refs. Changes: (a) header wording
   `sepal.h:10` "`caller-opaque rec_ref_t (u64)`" → u32; (b) **rewrite
   `tests/unit/test_vecstore.c::test_opaque_refs`** — `1ULL << 63` now
   narrows to `0u`, duplicating ref 0 (`sepal_n` becomes 4, not 5); switch
   to u32-world refs `{ 0, 1, 0xFFFF, 0x7FFFFFFF, UINT32_MAX }`, fix the
   matching `sepal_get` call and the "opaque u64" print; (c) **`Makefile`:
   `clean: clean-tests` recursion** (top-level `make clean` never
   descended into `tests/`, leaving Sep-11 u64-ABI test binaries that
   failed spuriously against the fresh u32 lib — see `PHASE-1-RETYPE.md`).
   ✅ DONE 2026-09-13 — `make test` exit 0, installed, verified.
- **libstoma** (`external/libstoma`): storage is decimal-string keyed, so it is
  width-independent; `stoma_index_ref`/`stoma_rank`/
  `rec_axis_fill_tokens` params self-adapt. Changes: (a) **rewrite
  `src/stoma_test.c` group 38** — `big = 5000000000ULL` truncates to
  705032704, so the "genuinely 64-bit" proof is now a lie that still
  round-trips; replace with the u32-boundary proof (`UINT32_MAX`
  round-trip index→fill→rank, plus guard regression: `"5000000000"`
  storable via string path, fill aborts `-1`, raw `stoma_query` still
  serves it) and reword the comment; (b) **a
  1-line strictness guard in `stoma_dest_emit`** (`src/libstoma.c`): after
  `strtoull`, `if (v > UINT32_MAX) { d->err = 1; return; }`, documented in
  `stoma.h`'s `rec_axis_fill_tokens` comment (+ de-littered the stale
  `%llu` quote in the `stoma_index_ref` doc; the two `%llu` impl sites
  stay — correct under u32 via varargs promotion). Why: `stoma_index`
  accepts any opaque decimal row_id string, so out-of-contract values are
  reachable through the raw API (mm's own AINDEX ids can never exceed u32,
  so this is pure hardening); a truncated id would alias a *different*
  row — a wrong hit, not an error — and the fill already aborts on
  non-decimal strings for exactly this "can't be a ref" reason.
  ✅ DONE 2026-09-13 — TDD red (197/198 pre-guard, exactly the new
  abort check failed) → green (`make test` exit 0: `stoma_test` 198/198,
  `stoma_prop_test` seeds 1/42/1337 green, zero warnings); installed,
  verified (header identical, `.so` byte-identical).
- **libjoint** (`external/libjoint`): internally `unsigned` ids,
  `qm_id = qmap_reg(sizeof(uint32_t))` already 4-byte
  (`src/libjoint.c`); `rec_axis_fill_interval`'s widen is now an identity
  cast. **No code change.** Doc-only: the ID-uniformity note in `joint.h`
  (~lines 19–28) still says `rec_ref_t (uint64_t…)` → u32.
- **libislet** (`external/libislet`): per-cell values `uint32_t` (with
  `ISLET_MISS = UINT32_MAX` already matching), u64 morton keys stay
  internal; `rec_axis_fill_bbox_N`'s widen is now identity. **No code
  change.** Doc-only: the ID-uniformity note in `islet.h` (~lines 89–99)
  `uint64_t` → u32.
- **Scope (locked 2026-09-13, locations migrated 2026-09-15):** edits
  lived in the `~/lib*` sibling checkouts only — those checkouts are now
  gone; all four axis libs live as in-site submodules
  (`external/lib{joint,islet,sepal,stoma}`). The historical note below
  about the site's `external/libstoma` copy is superseded: there is one
  copy now, the submodule.
- **Fixture** (`external/libqmap/tests/fixture_id_join.c`): no source
  change (casts at `:110`/`:154` are no-ops). Rebuild + rerun
  `make clean all test` → 16/16 once the installed stack matches.
- **Site gate:** `rec_*` appears nowhere in `external/hyle`,
  `external/axil`, `external/libxylem`, or the libqmap rust bindings — the
  site build is source-invisible to the retype. Gate stays "site `make`"
  (+ full `make test` as cheap insurance).

### 5.3 Execution checklist (in order)

1. ✅ DONE — `external/libqmap`: re-ran `./test.sh && ./test-cli.sh`;
   user ran `sudo make install`; verified `/usr/include/ttypt/rec.h`
   reads u32.
2. ✅ DONE — `external/libsepal`: header wording + `test_opaque_refs` rewrite +
   `Makefile` clean-recursion → `make test` green (exit 0) → installed;
   installed header/`.so` verified identical to the working tree.
3. ✅ DONE — `external/libstoma`: group-38 rewrite + `stoma_dest_emit` guard +
   header docs → `make test` green 2026-09-13 (exit 0: `stoma_test`
   198/198, `stoma_prop_test` all seeds; TDD red 197/198 pre-guard) →
   installed; header≡tree + `.so` byte-identity verified.
4. ✅ DONE — `external/libjoint`: ID-uniformity note u32 + Operations
   catalog/docs corrections → `make clean && make all && ./test.sh`
   green 2026-09-13 (core expects-diff + extended; first run red was the
   stale-binary hazard) → installed; header≡tree + `.so` byte-identity
   verified.
5. ✅ DONE — `external/libislet`: ID-uniformity note u32 + `islet_search`
   sentence fix → `make -C tests clean && make test` green 2026-09-13
   (exit 0, 27 suites; first run red was the stale-binary hazard) →
   installed; header≡tree + `.so` byte-identity verified.
6. ✅ DONE — Fixture: `make -C external/libqmap/tests clean all test`
   → 16/16, 2026-09-13.
7. ✅ DONE — Site: `make` green 2026-09-13 (incl. `W06 check PASS: no
   native imports in WASM`) + `make test` green (manual run by the user,
   2026-09-13).
8. ✅ DONE 2026-09-13 — this section marked DONE (v14). **Commit only
   when explicitly asked.**

- Old quest journal for this phase (`.pi/quest/future/AXIS-ID-UNIFORM.md`)
  removed — it claimed "COMPLETE," which is no longer true now that the
  ref-type law changed underneath it. Per the 2026-09-13 decision, no
  `.pi/quest/` tracking is used for this plan at all: this section is the
  surviving, accurate record and the single home for status.

---

## 6. Sequencing

1. **Phase 1 — DONE 2026-09-13** (§5, checklist §5.3 all green).
   Gate met: every touched repo's suite green + `fixture_id_join`
   16/16 + site `make` + site `make test`.
2. **Phase 2A — per-axis store/unstore/readback.** **DONE
    2026-09-14** — conventional exports with axis-decided grammars
    (`store(ref, value)` with the **whole string** — never split,
    idempotent `unstore`, `readback`), uniform surface
    rules, persistence classes, slices 2A-mock..2A-6 in `PHASE-2-CLI.md` (2A);
    §11 is the checklist.
3. **Phase 2B — general qmap CLI composition.** **IN PROGRESS 2026-09-14** —
    2B-0 + 2B-1 landed (docs + `@roster` sidecar + by-name discovery/load,
    `test-roster.sh` green). Composed `-g` (fold of `-Q`),
    `@` roster + fan-out, reserialized slices 2B-0..2B-6, decided options
    D1-D10 in `PHASE-2-CLI.md`; §12 is the checklist.
4. **Phase 3 — `pi-mm` scaffold.** Tools stub qmap in unit tests. Gate: a
   Pi session recalls prior gist from disk with no provider; `deno test`
   green.
5. **Phase 4 — time/space in the extension.** `memory_store --until` /
   `--near` tool surface over joint/islet through the phase-2 surface. Gate:
   validity-window + geotag round-trips through the extension tools.

---

## 7. Open questions — UNCLEAR (stated as unclear; settled at the slice noted)

- **U1** Embedder env names, curl sonames, timeout values → 2A-1 prototype.
  (Direction locked: curl in libsepal, lazy-`dlopen`, explicit config
  setter, CLI maps env; floats always direct.)
  **PARTIALLY SETTLED 2026-09-14 (2A-1 DONE):** curl impl landed —
  `dlopen("libcurl.so.4"|"libcurl.so")`, config setter
  `sepal_configure_embeddings(url, model, key)` in `external/libsepal`,
  OpenAI-style body/response, no timeouts yet (left for live-fire tuning).
  Still open: CLI env-name mapping (lands with 2B-4 fan-out).
- **U2** ~~`rec_axis_get` exact signature → locked in 2A-0~~ **SETTLED
  2026-09-14:** the read-back export is **`rec_axis_readback`** —
  `rec_axis_get(int)` is the landed registry lookup and a plugin could never
  define the same name (see `PHASE-2-CLI.md` 2A header note). 2A-mock + 2A-1
  built against the locked name.
- **U3** mm-dialect exact recipes → **SETTLED 2026-09-15 (2B-5 DONE):**
  store = explicit-ref `-p REF:"<DATE>:<TEXT>"` (auto-ref cannot carry
  colon payloads); search = bounded-window `joint="a=A b=B"` ∧
  `stoma="field=text query=Q matched=1"`; forget = roster-backed `-d`;
  reset = enumerate refs via `-g .` + `-d` each (no single-flag reset;
  `-1` sentinel skipped). Proven by `test-mm.sh` over real joint+stoma
  (`CLI-SURFACE-EXAMPLES.md` §8, `2B-5-IMPLEMENTATION.md`).
- **U4** Stoma rebuild budget → **measured in 2B-2, 2026-09-14** — gate
  `test-real.sh` rebuilds 3 docs in **~0.02–0.03 ms** (3-record corpus, per-
  open bulk-reindex via the sidecar-scan in `libstoma:rec_axis_open`; budget
  note on stderr on every bound open). Well under budget at this scale; no
  persist-postings proposal needed now (deferred, never silently absorbed —
  over budget would add a proposal, not silently change the design).
- **U6** `QDBE_MASK` sizing → **decided 2026-09-14 pre-2B-4**: default CLI mask
  shrinks `32767 → 4095` (4k buckets, `2^12-1`; was 32k, `2^15-1` —
  `external/libqmap/src/qmap.c:163` + `libstoma` sidecar-scan). Over-large
  by ~8× for small corpora (mmap bloat) — `qmap_open(...,mask,flags)`
  auto-grows (`external/libqmap/include/ttypt/qmap.h:172`) so the mask is
  only a hint, not a cap; per-store env `QMAP_MASK` overrides for benches.
- **U7** Binary primary + typed fanout → **decided 2026-09-14 pre-2B-4**:
  libqmap is already typed (`qmap_reg`); the axis store boundary gains
  additive `rec_axis_store_typed(ctx,spec,ref, blob,len,qtype)` alongside
  the string `rec_axis_store` (keeps shipped `*.so`s valid); CLI prefers
  the typed symbol when `vtype != QM_STR`, else the string one.
- **U8** Derived (rebuild) vs persisted axis → **decided 2026-09-14 pre-2B-4**:
  freedom per deployment, not per-axis hard-wired. `rec_axis_open(spec)`
  decides persistence (file vs memory/rebuild) — the roster names the
  *logical* axis, not the file. `stoma` stays derived; `joint`/`islet`
  can be built either way; `sepal` stays persisted; 2B-4 fanout writes to
  whatever `ctx` the bound axis represents.
- **U5** libcurl build-header availability → discovered in 2A-1 (lazy
  `dlopen` needs only call-shape decls).
- Write-fanout shape (orchestrator design, atomicity/error semantics
  across stores) — decided D1 (attempt all, report all, nonzero;
  idempotent forget compensates; no cross-store transactions).
- **Rank-axis ordering** for combined text+vector queries →
  **SETTLED 2026-09-15 (2B-6):** standing convention = **first rank-capable
  axis in query order wins** (D2 — kernel fallback `rec_axis.c:199-211`,
  the CLI tree eval picks the first rank-capable leaf in preorder);
  `--score` combining across rankers deferred.
- Capture-heuristic thresholds for `turn_end` significant-turn detection
  in `pi-mm` — validate empirically once the extension runs.

---

## 8. Risks

| Risk | Mitigation |
|---|---|
| Ref-type retype left half-done breaks a downstream consumer silently | gate every phase-1 step on that repo's own suite; don't call phase 1 done until all 5 repos + the fixture are green |
| Vectors regress to meaningless (wrong model/pooling change again) | keep the water/fire ordering as a standing fixture/test |
| `qmap_close` truncation bug reintroduced | keep the no-close invariant explicit; test file integrity after every mutating op |
| Single-writer invariant violated across the primary + axis stores | document single-writer; round-trip test (`put` → `get` → `del` everywhere it was written) in phase 2 |

---

## 9. References

- Kernel API (canonical, unchanged location): `external/libqmap/docs/RECALL-KERNEL.md`
- Kernel header: `external/libqmap/include/ttypt/rec.h`
- qmap public API: `external/libqmap/include/ttypt/qmap.h`
- Axis libs: `external/libjoint`, `external/libislet`, `external/libsepal`, `external/libstoma` (in-site
  submodules since 2026-09-15; formerly `~/lib*` sibling checkouts, now removed)
- id-join proof fixture: `external/libqmap/tests/fixture_id_join.c`

---

## 10. Axis operations catalog (what each axis stores and answers)

Source-verified 2026-09-13 against the four axis checkouts (headers +
implementations + tests; `~/lib*` siblings then, `external/lib*`
submodules since the 2026-09-15 migration). Every axis owns its domain store and answers
through three surfaces: **native ops** (its own API), **kernel adapters**
(`rec_axis_fill_*` → candidate set, optional ranker → score), and —
planned, phase 2A — the conventional **store/unstore/get adapters**
(`rec_axis_store(ctx, spec, ref, value)` / `rec_axis_unstore(ctx, ref)`
/ `rec_axis_get`, contract `RECALL-KERNEL.md`). The CLI passes the
**whole** `-p` string as `(ref, value)` — never split — and each axis
parses it entirely in its own grammar; interpretation is axis-side
(principle 1). The primary value format is not strict; an axis never has
to understand an unexpected format (it alone rejects).
Only the fill/rank adapters and the ref crossing them follow the ref boundary
(§2); internal keys never leave the axis.
**Mask:** default hash mask `4095` (`2^12-1`, 4k buckets; was `32767`) —
`qmap_open(...,mask,flags)` auto-grows on overflow (`qmap.h:172`), so the
mask is only an initial hint. Override per-store via env `QMAP_MASK` for
benches.

**Typed store:** alongside `rec_axis_store(ctx,spec,ref, const char *value)`
(string whole-value) there is additive
`rec_axis_store_typed(ctx,spec,ref, const void *blob,size_t len,uint32_t qtype)`
for binary primaries (any `qmap_reg` type); the CLI forwards `(ptr,len,qtype)`
from `qmap_get`+`qmap_type_len(qtype)` and prefers the typed symbol when
`vtype != QM_STR`. `readback` already returns `blob/n`. Shipped `*.so`s stay
valid — missing typed export ⇒ text-only axis. See `PHASE-2-CLI.md` D12 /
`RECALL-KERNEL.md` convention.

**Derived vs persisted:** `rec_axis_open(spec)` decides — the roster (`@joint`)
names the *logical* axis only. A derived axis rebuilds from the primary at
each open (like `stoma`); a persisted one uses the alongside file (`<dir>/
<name>.db`). Freedom per deployment, not hard-wired per axis.

Each repo's own README carries its matching per-repo catalog.

**stoma — text** (`external/libstoma`, `include/stoma/stoma.h`). Store: inverted
index `(field, token) → row_id` (decimal strings) + folded doc text per
`(field, row)`. Accent-sensitive fold; query tokens prefix-match and AND.
- Writes: `stoma_open/close/clear`, `stoma_index(db, field, row_id, value)`,
  `stoma_index_ref` (same, `rec_ref_t` caller). Phase-2A store/unstore/get:
  the **whole** value string is indexed under a canonical `text` field;
  delete via new
  `stoma_unindex(db, field, row_id)` — reads the `field\trow` doc,
  re-tokenizes it into the exact posting keys, deletes those + the doc;
  read-back is the whole doc text, one entry. **No new stored state** (the
  doc side-table is the inverse — and serves phrase verification + rank
  lengths, so it is query structure, not duplication). **Memory-only**,
  rebuilt from primary strings at each open (zero disk, never stale).
- Native queries: `stoma_query` (token-prefix AND), `stoma_query_phrase`
  (contiguous in-order subsequence; single token = plain query). Utilities:
  `stoma_fold`, `stoma_tokenize`, `stoma_list_normalize/_contains/_append`.
- Kernel: `rec_axis_fill_tokens` (exact) + `stoma_rank` (matched /
  doc-tokens; shorter docs win ties). **Has a ranker.**
- Answers natively: "which rows mention these words / this phrase in
  field F". No point-fetch op (row→text is rank-internal only).

**sepal — meaning** (`external/libsepal`, `include/ttypt/sepal.h`). Store:
dim-tagged vectors per ref (matryoshka prefix `min(full_dim, 256)` +
sign-bit sketch over the full dim), qmap-persisted blobs (VEC1).
- Writes/reads: `sepal_open/close`, `sepal_put/del`,
  `sepal_get` (never truncates; 0 = missing or undersized buffer),
  `sepal_dim/full_dim/n`, `sepal_index_validate`. Phase-2A store/unstore/get:
  comma-floats go direct to `sepal_put` (same-ref put is replace-in-place,
  verified); otherwise the **whole** string is embedded iff configured (its
  grammar decides what content is meaningful), else `EINVAL` loud
  (libcurl `dlopen`d lazily; explicit config setter; CLI maps env);
  `unstore` = `sepal_del` absent → 0; read-back is the stored vector.
  **No new stored state** (the store is already keyed by ref).
- Native queries: `sepal_search` (two-stage ANN — Hamming prefilter
  top-`m`, exact-cosine rerank top-`k` above `min_sim`; `m==0` → 10×k),
  `sepal_cosine`, `sepal_sketch`, `sepal_blob_len/_hdr/_put`.
- Kernel: `sepal_fill_approx` (**declares `REC_SET_APPROX`**, owed bound
  `m/n`) + `sepal_rank` (exact cosine). **Has a ranker.**
- Answers natively: "top-k refs by meaning-similarity to this vector"
  and point fetch ("what vector is stored for ref R").

**islet — space** (`external/libislet`, `include/ttypt/islet.h`). Store:
`uint32` values per grid cell, keyed by internal `uint64` Morton codes
(1–4D int16 lanes + 2D int32 family); multi-value cells.
- Writes/reads: `islet_init/open`, `islet_put/get/set/del/del_all/
  cell_count_N`, `islet_get_multi_N` + `islet_cell_next`,
  `islet_iter_N` + `islet_next/next32` (box → collected points).
  Phase-2A store/unstore/get: point-list grammar into `islet_put_N`
  (the whole value string is `x,y[,z];…` — `;`-separated points, `,`-
  separated int16 coords, dim = first point's coord count 1..4; strict
  tokens `[-+]?[0-9]+`, in-call dups deduped, over
  `ISLET_AXIS_MAX_POINTS` (1024) → `ERANGE`, else `EINVAL`). `store` is
  **replace-in-place** (parse-first, then unstore-then-put; a ref owns one
  spatial footprint, re-store idempotent; different refs may share a cell
  — the grid is multi-value). **The only new stored state in 2A** — a
  `rev` manifest (own single-map `<fname>.ridx`: u32 ref → `;`-joined
  canonical point string, replace-in-place per ref; wired into
  `rec_axis_open`, lazy in-memory for raw handles) + `islet_del_value_N`
  family (`1..4` + `2_32`), so unstore is O(cells-of-ref), never a scan;
  read-back is one NUL-joined `x,y[,z]` per cell, round-trippable. The
  cell grid is keyed by cell, so the ref isn't discoverable backwards
  without it.
- Codec/utils: `morton_set/get_N`, `islet_ops[]` runtime-dim table,
  SIMD `morton_set/get_bulk[4]`, `islet_last_scan_count`.
- Kernel: `rec_axis_fill_bbox_N` (exact; volume capped at
  `ISLET_FILL_MAX_VOL`). **No ranker (filter-only).**
- Answers natively: point fetch ("value at cell P"), point presence
  (`get != ISLET_MISS`, `cell_count > 0`), box membership ("all values
  in `[s, s+l]`").

**joint — time** (`external/libjoint`, `include/ttypt/joint.h`). Store: per-entity
presence intervals (`[min, max)`, open end = +inf), primary map + `max`
and `id` secondary indexes.
- Writes: `joint_init/close`, `joint_start` (presence on),
  `joint_stop` (presence off; stop-without-start back-fills from −inf).
  Phase-2A store/unstore/get: joint parses the **whole** value string into
  an interval — explicit `A,B` (atomic) first, then leading-date prefix
  (e.g. the mm `"<DATE>:<STRING>"` shape, open interval — trailing content
  ignored), then three-form (`T1` open, `T1,T2` atomic, `,T2`
  close-or-backfill); native `1`s absorb to 0; exact-duplicate restates
  are no-ops via an `id`-index guard. Delete via new
  `joint_erase(jd, id)` over the existing `id` secondary index; read-back
  is one `start,end` per interval. **No new stored state** (the `id`
  index, kept by `qmap_assoc` and backfilled from the file-backed `ti`
  map on open, is the inverse).
  Utils: `sscantime` / `printtime` (ISO-8601 ↔ `time_t`).
- Native query: `joint_iter(jd, a, b)` + `joint_next` — decomposes the
  window into maximal constant-presence segments, each carrying the
  **full present set** (`min/max/count` + `count` ids).
- Kernel: `rec_axis_fill_interval` (exact: every entity present in
  `[a,b)`). **No ranker (filter-only).**
- Answers natively: window→entities presence profile. Derived client-side
  from the segments: point presence (`joint_iter(jd, t, t+1)` + id match),
  co-presence ("when are 3 and 4 together": segments whose set ⊇ {3,4}),
  concurrency (`count` per segment), gaps (empty stretches).
- **Not native:** per-entity interval fetch ("all intervals of entity X";
  the `id` index exists internally only). **Caveat (flagged follow-up,
  needs its own TDD fix, out of scope here):** coincident same-entity
  endpoints (`[A,t)` + `[t,C)`) misreport the entity absent on the
  leading segment — puts sort before dels at the shared endpoint.

Composition reminder: kernel joins (AND/OR/NOT over refs) combine axes;
rankers exist only on stoma + sepal, so scored multi-axis queries score
through those two (or a consumer score fn).

---

## 11. Phase 2A — per-axis store/unstore/readback (2A-mock + 2A-1 + 2A-2 + 2A-3 + 2A-4 + 2A-5 + 2A-6 DONE 2026-09-14)

Standalone prerequisite phase: every registered axis gains the
conventional `rec_axis_store`/`rec_axis_unstore`/`rec_axis_readback`
exports (`readback`, not `get` — U2 settled: `rec_axis_get(int)` is the
landed registry lookup) so a consumer can record a ref in an axis store,
remove it idempotently, and read its entries back — with axis-decided
grammars, no per-axis glue.
Detail (inverse principle, uniform contract + surface rules, per-axis
roles, persistence classes, slices): `PHASE-2-CLI.md` 2A; contract kernel:
`external/libqmap/docs/RECALL-KERNEL.md` (store/unstore/readback conventional
exports). No libqmap/kernel change, no site change, no `/usr` installs
(D4 — write gates run on fresh in-site `external/lib*` builds). TDD every slice;
per-repo gate = that repo's suite + `nm -D` shows the new symbols;
clean-rebuild rule. Commit only when asked.

**Axis autonomy** (principle 1): the consumer passes the whole `-p` string
as `(ref, value)` — never split; each axis parses it entirely — and
interpretation is axis-side. **The inverse principle** (the
delete design): `unstore` deletes exactly the entries the axis's own store
  created for a ref, by walking the axis's existing inverse mapping — cost ∝
  entries-of-ref, never O(store), never a scan (`readback` reads them back
  the same way). sepal (store keyed by ref), joint (`id` index) and stoma (doc
side-table) need **zero new stored state**; only libislet (cell-keyed, a
ref isn't discoverable backwards) adds the canonical `rev` manifest
(ref → cells, its own file) + `islet_del_value_N`.

- [ ] **2A-0** Contract doc: `RECALL-KERNEL.md` convention + this §10
  columns + `PHASE-2-CLI.md` 2A section (whole-string `value` signature, roles,
  surface rules, embedder + roster conventions, `get` signature lock).
  Plan v18. Content-only.
- [x] **2A-mock** Proof-of-contract slice (DONE 2026-09-14): mock plugin
  `src/librec_axis_mock.c` + `src/rec_axis_store_test.c` dlsym round-trip
  suite; `Makefile` + `test.sh` wiring; libqmap `make test` green.
- [x] **2A-1** libsepal adapters (DONE 2026-09-14): store = floats direct /
  curl-embed iff configured / else `EINVAL`; `unstore` = `sepal_del`
  absent → 0; `readback` = stored vector as `%.9g` comma-floats.
  `sepal_configure_embeddings(url, model, key)` + lazy
  `dlopen("libcurl.so.4"|"libcurl.so")` (7 symbols, ABI numbers, no
  build-time curl dep) + weak `sepal_embed_fetch` seam.
  `tests/unit/test_axis_store.c` (70 assertions, canned-vector stub) +
  `tests/unit/test_axis_store_live.c` (env-gated, skipped by default).
  Gates: libsepal `make test` green (unit+integration+property+stress),
  valgrind clean, `nm -D` shows all five symbols.
- [x] **2A-2** libstoma (DONE 2026-09-14): native `stoma_unindex(db,
  field, row_id)` + `stoma_unindex_ref` (doc side-table walked backwards
  → exact posting keys; absent → idempotent 0; zero new state) + adapters
  on canonical `text` field (store = unindex-then-index replace-in-place,
  NULL/empty → `EINVAL`; unstore = 0; read-back = folded doc, one entry —
  folded-text locked, verbatim casing would need new stored state).
  Tests: group 39 in `stoma_test.c` (per-token residual sweep, rank-−1
  proves doc entry gone, per-field isolation, dup tokens, NULL contracts)
  + new `src/stoma_axis_store_test.c` (34 assertions). Gates: `make test`
  exit 0 (227/227 + prop seeds + 34/34), new test valgrind-clean, `nm -D`
  shows all five symbols, zero warnings. Memory-only confirmed.
- [x] **2A-3** libjoint (DONE 2026-09-14): native `joint_erase(jd,
  id)` (id-index `qmap_get_multi` → collect → del each ti key; `qmap_assoc`
  cleans `max`/`id`; `qmap_del_all(id)` mops replace-path residual dups —
  empirically verified; absent → 0; `UINT32_MAX` → `EINVAL`; zero new
  state) + adapters (ordered-attempt whole-string grammar: open / `,B`
  close-or-backfill / `A,B` atomic with `B>A` pre-checked; native `1`s
  absorb; id-index exact-match guard makes exact restates no-ops; NULL-
  valued replace-stub guards on every id walk; dates via internal
  non-aborting `joint_date_parse` since public `sscantime` CBUG-aborts;
  read-back = NUL-joined store-grammar entries, round-trippable).
  Tests: new self-checking `src/joint_axis_store_test.c` (116 assertions;
  `test.c`/`expects.txt` untouched). Gates: `make clean && make test`
  exit 0, `nm -D` 4 symbols = T, zero warnings, valgrind shows only the
  library's pre-existing `joint_iter` process-lifetime arenas (same class
  as `bin/test`'s own records).
- [x] **2A-4** libislet (DONE 2026-09-14): `rev` manifest (own
  single-map `<fname>.ridx` file — never a second map in the grid file;
  u32 ref → `;`-joined canonical point string, replace-in-place per ref;
  wired into `rec_axis_open`, lazy in-memory for raw `islet_open` handles;
  spec buffer kept process-lifetime — fixed a latent exit-time UAF) +
  `islet_del_value_N` family (`1..4` + `2_32`), then adapters
  (point-list grammar `x,y[,z];…`; `store` replace-in-place parse-first,
  in-call dedup, over `ISLET_AXIS_MAX_POINTS` (1024) → `ERANGE`; `unstore`
  idempotent O(cells-of-ref); read-back = NUL-joined canonical points,
  round-trippable; shared cells legal). Tests: new
  `tests/unit/test_axis_store.c` (15 tests, 193 assertions). Gates:
  `make` zero warnings, `make test` exit 0 (all suites, fresh local build
  via `LD_LIBRARY_PATH`, D4), `nm -D` 9 symbols = T, new binary
  valgrind-clean. Next: 2A-5 round-trips, 2A-6 close-out.
- [x] **2A-5** Round-trips per persistence class (DONE 2026-09-14,
  detail `PHASE-2-CLI.md` 2A-5): 2A-1..2A-4 proved adapters in-process
  only — this slice crossed real process boundaries. File-backed
  (sepal/islet/joint): self-exec'ing harness per repo
  (`seed`/`verify1`/`unstore`/`verify2` modes + no-arg orchestrator that
  fork+exec's itself per phase; explicit `qmap_save()` + exit destructor;
  never `qmap_close`; self-unlinking `/tmp/test_*` files): store → reopen
  → query finds ref (`sepal_search` with ref-1 vector score ≥ 0.99;
  islet `readback` = `1,2␀4,5` + `fill_bbox_2` full={11,12} tight={11} —
  proved `.ridx` rehydration; joint `joint_iter` at 2026-02-01 has 13 not
  14, at 2026-04-01 has 14 — proved file-backed `id` backfill) → unstore
  → reopen → absent (+ per-ref isolation). Joint workers burn handle 0
  first (jd-0↔NULL collision). stoma (memory-only, single process):
  corpus `{(1,"Beacon Harbor lights"),(2,"Beacon AND Pão"),(3,"alpha
  omega")}` store → `beacon`=2 → unstore 1 → `harbor` miss → rebuild
  (fresh open, re-index corpus minus primary-dropped 3) → `beacon`={1,2},
  `harbor`={1} again (never-stale), `omega` gone, rebuild-vs-incremental
  parity 43/43. No lib code changed — harnesses green first try. Gates:
  clean rebuild, zero warnings, per-repo suites green (sepal 1857/20064/
  87465/10609, islet 15 suites, joint `./test.sh` + 116/116 + round-trip,
  stoma 227+prop seeds+34+43), new binaries valgrind-clean, `nm -D`
  unchanged (sepal 5 T, joint 5 T, stoma 6 T, islet 9 T), `LD_LIBRARY_PATH`
  local builds (D4).
- [x] **2A-6** Close-out (DONE 2026-09-14): `make` green (W06 check PASS,
  boundary-checks pass, all mods build; site is source-invisible to 2A) +
  per-repo suites green above + `nm -D` spot-checks; no installs. Phase
  2A closed; §12 (2B) unlocked.

**2A gate:** every touched repo's suite green; new symbols exported
(`nm -D`); round-trips green; site `make` green (source-invisible).

---

## 12. Phase 2B — general qmap CLI composition (PLANNED, after 2A)

Execution checklist (detail: `PHASE-2-CLI.md` — principles, D1-D10, `-g`
fold, `@` roster, reserialized slices). Built on the phase-2A adapters.
TDD every slice; each slice ends green on its own gate + `./test.sh &&
./test-cli.sh` + the sibling suites it touches (standing clean-rebuild
rule). No `/usr` installs (D4): read gates run on the phase-1 `/usr`
stack, write gates on fresh in-site `external/lib*` builds via
`QMAP_AXIS_PATH`/`LD_LIBRARY_PATH`. Commit only when explicitly asked.
Legacy-close throughout: zero-axis invocations byte-identical.

- [x] **2B-0** CLI-surface doc correction (DONE 2026-09-14): README
  width-lie fix — `external/libqmap/README.md:125-126,138-139`
  (previously "uniform 64-bit refs", `libit`/`libgeo`) → u32 + the four
  real axis names; plus `docs/RECALL-KERNEL.md` `rec_axis_open`/Status
  rewritten to the by-name surface (`-Q`/`--dl`/`--open` retired) and
  CHANGELOG `libit`→`libjoint`; `external/libsepal` header doc sibling-names
  updated. Gate: `./test.sh && ./test-cli.sh` green (content-only).
- [x] **2B-1** Discovery + roster + load (CLI-only, kernel untouched):
  `@roster` parse/persist/load (write-if-absent; override-once;
  alongside-heuristic note); by-name inter-pass load+bind (name→slot,
  lazy dlopen of `lib<name>.so` from `$QMAP_AXIS_PATH`, ctx via
  `rec_axis_open` on alongside-defaults — no `--dl`/`--open`, D9;
  stoma memory-open + reindex + sepal env config D8 deferred to
  real-lib wiring in 2B-2 — the CLI plugs each axis through the same
  by-name load/bind surface);
  numeric `--axis SLOT` kept only as `-Q`-legacy until its deletion (2B-3);
  `QMAP_AXIS_LIBS` stays. Single-axis stub plugins `libstub.so`/`libzed.so`
  built via the standalone-rule pattern (avoids the multi-LIB Makefile
  aggregation bug). `test-roster.sh` TDD (DONE 2026-09-14).
- [x] **2B-2** Non-mm space∩time∩text gate over real files (DONE 2026-09-14):
  `test-real.sh` green — fixture seeds the same refs 1/2/3 into real
  primary (`:a:s`) + joint/islet/sepal stores (islet `q.vec`/`q.dim` side
  files); stoma rebuilt from the primary at each open via the sidecar-scan
  (`rec_axis_open` scans `<dir>/*.roster`, opens `"<dir>/<base>"` as
  `qmap_open(…,"hd",QM_HNDL,QM_STR,32767,QM_AINDEX|QM_MIRROR)` and
  re-indexes; budget note `rebuilt N docs in X ms` on stderr — 3 docs
  ~0.02–0.03 ms in the gate, U4: well under budget, no persist-postings
  proposal needed at this scale, deferred and never silently absorbed);
  loud first-@ alongside + stoma notes prove the empty-index path is never
  silent (second open: budget note). Asserts: `--list-axes` all four
  `ctx=y` + `"(joint=\"a=2026-09-14 b=2026-09-15\" AND
  islet=\"dim=2 s=9,1 l=1,1\") AND stoma=\"field=text query=beacon
  matched=1\" -g . -t 100"` → `3 0.125000 2026-09-14T12:00:00:Beacon Harbor
  lights` (stoma first rank, `1/8` tokens) + sepal
  `sepal="file=q.vec qdim=3 m=2 min_sim=0.5"` → `3 1.0 …` / `1 0.906…` +
  classic zero-plugin regression on a fresh no-roster primary + offline
  floats-direct + env-gated `QMAP_SEPAL_EMBED_*` ANN column
  (`rec_axis_env_config` hook, `sepal_embed_fetch` for qvec+strings, skipped
  unless both vars set). Bring-ups: libjoint burns handle 0 once per
  process in `rec_axis_open` (jd-0↔NULL) + `CLI-SURFACE-EXAMPLES.md` §6
  grammars corrected. Wired into `make test`.
- [x] **2B-3** The `-g` fold + the `-X` surface: one `getopt_long` loop;
  `-X EXPR` set algebra (D10; leaves `NAME[=VALUE]`, `( ) AND OR EXCEPT NOT`,
  precedence) with `-t N`/`--top N` + `-b F`/`--bottom F` knobs;
  `-g .` armed runs the effective query (D6) and renders ref-led lines;
  `-Q`, its guard, `--dl`, `--open`, `--axis`, `--params`, sticky joins,
  `--combine`, and the `rq_*` helpers deleted;
  `test-cli.sh` rewritten against `CLI-SURFACE-EXAMPLES.md` (every query
  case via `-X`, primary-seeded full-line assertions, `-t`/`-b` cases,
  `--list-axes` standalone); slot+ctx pre-validation
  with named errors; exit code threaded through pass-2 ops; dangling-ref
  warn-and-skip. **DONE 2026-09-14** — `test-cli.sh` 25/25, full `make test`
  green, stale-term grep over `src/`+`include/` clean; two bring-up fixes
  recorded in `2B-3-IMPLEMENTATION.md` §9 (fold-plugin dbid `"hd"` + CLI
  mask; `rec_rank_free(NULL)` guard; classic-row expectations pinned to
  verified pristine behavior).
- [x] **2B-4** Write fanout + forget/reset (D1 — with D11..D13):
  union write-sets for `-p`/`-d` (`{primary} ∪ {@} ∪ {target}`, each store
  once; whole `-p` *payload* fans out as `(ref, blob,len,qtype)` to every
  target — string `rec_axis_store` for `:a:s` primaries, additive
  `rec_axis_store_typed` when `vtype != QM_STR` (D12) else a loud
  text-only skip); dlsym store(_typed)/unstore/readback (missing store ⇒
  read-only); loud partials (attempt all, report all, nonzero) with
  idempotent forget as compensation; `-d`/`-D` collapse on axes;
  `QDBE_MASK` `4095` + `QMAP_MASK` env (D11); readback has **no** CLI
  surface (deferred 2026-09-15 — query + write cover the flow; `-g`/`-m`/
  `-c` stay byte-identical). ✅ **DONE 2026-09-15** — `test-fanout.sh`
  green (verbatim store, typed dispatch, text-only skip, named/numeric
  forget, `-D` collapse, partials, mask); fold plugin write-capable +
  plain string-only axis; full `make test` + sibling suites + site `make`
  green. Detail: `2B-4-IMPLEMENTATION.md`.
- [x] **2B-5** mm dialect as *documented invocations* on the same surface
  **DONE 2026-09-15** (store → composed search → forget → reset: the exact
  recipes pi-mm issues in phase 3; U3 settled — `CLI-SURFACE-EXAMPLES.md`
  §8 rewritten to the proven spellings; `test-mm.sh` green over real
  joint+stoma files; F4 double-open clobber fixed in `qmap_open`).
  Detail: `2B-5-IMPLEMENTATION.md`.
- [x] **2B-6** Rank convention documented (D2; `--score` deferred),
  `-X` grammar verified against the built prototype, plan bumped to v19.
  **DONE 2026-09-15** — standing rule = **first rank-capable axis in query
  order wins** (CLI preorder leaf selection in `qmap.c`, kernel fallback
  `rec_axis.c:199-211`); `except-chain` row in `test-cli.sh`,
  `stoma-over-sepal` row in `test-real.sh`; no lib/kernel changes. Detail:
  `2B-6-IMPLEMENTATION.md`. **Phase 2B closed — next: phase 3 (`pi-mm`).**

**Final gate:** three-axis (`test-real.sh`) + mm-dialect (`test-mm.sh`)
green; `./test.sh && ./test-cli.sh` green; sibling suites green; site
`make` green (all additive — site source-invisible to the CLI changes).

**Risks:** (1) stoma O(corpus) rebuild cost at scale — measured in 2B-2
(U4), persist-postings deferred, never silently absorbed. (2) roster
clobber by old-binary bare opens — recoverable (re-specify `@`), axis
data never at risk, alongside-heuristic notes it. (3) sibling stale-binary
hazard on tests (clean-rebuild first, as always).
