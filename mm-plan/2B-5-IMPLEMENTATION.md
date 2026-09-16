# 2B-5 — Implementation detail (mm dialect; store → search → forget → reset)

> Slice 2B-5 of `external/libqmap`. Read these first, in order:
> `mm-plan/CLI-SURFACE-EXAMPLES.md` (§8 normative mm surface after this
> slice), `mm-plan/PHASE-2-CLI.md` (2B deliverables, the `@` roster, D1–D13,
> the 2B-5 slice row), `mm-plan/2B-4-IMPLEMENTATION.md` (the fan-out
> machinery this dialect rides), then `external/libqmap/src/qmap.c`
> (current shape, incl. `gen_put`/`gen_del`/`gen_del_all`,
> `qmap_fanout_store`/`qmap_fanout_unstore`, `qmap_write_ref`).
>
> **D14 update (2026-09-16):** historical record of the mm dialect as
> built. The `stoma="… query=…"` leaf grammar here is still valid qmap
> syntax, but pi-mm now emits the D14 flags form — `-X '(joint=… AND
> stoma="field=text matched=1" AND sepal)' --query=… [--min-sim=…]` —
> structure in `-X`, runtime values on CLI flags (precedence leaf spec >
> CLI > env). See `mm-plan/PHASE-2-CLI.md` D14 carve-out and
> `mm-plan/CLI-SURFACE-EXAMPLES.md` §8/§11.
>
> This file is the lossless record: §1 = the built surface as found
> 2026-09-15, §2 = rehearsal evidence, §3 = findings F1–F6, §4 = the
> normative pi-mm recipes (settle U3), §5 = `test-mm.sh` design, §6 = the
> F4 fix path, §7 = doc updates, §8 = gates, §9 = execution log.
> Status: **DONE 2026-09-15** (TDD red→green; `test-mm.sh` green; F4 fixed
> via `qmap_open` aliasing; docs + CHANGELOG landed).

## 1. The built surface as found (2026-09-15)

All paths `external/libqmap/…`; primary type throughout is `:a:s`
(QM_AINDEX-keyed, string payload).

- **Pass-2 ops** (`src/qmap.c:1951-1970`): `-p` → `gen_put`
  (`src/qmap.c:695-734`); `-d` → `gen_del` (`src/qmap.c:463-485`); `-D` →
  `gen_del_all` (`src/qmap.c:673-693`); `-g .` armed → `qmap_composed_get`,
  unarmed → `gen_get` classic; `-l`, `-m`, `-c`, `-r`, `-g KEY` classic;
  `--list-axes` long-only discovery.
- **Primary open** (`src/qmap.c:872`): `qmap_open(buf, "hd", ktype, vtype,
  qmap_mask_effective(), (flags & QM_FLAGS_MASK) | QM_MIRROR)` — map name
  `"hd"`, mirror flag on. Note for §6: **everything opens `"hd"`**.
- **Roster**: explicit `@csv` in filespec (`mem.db@joint,stoma:a:s`) ∪
  stored `<primary>.roster` sidecar ∪ `-X` names ∪ `QMAP_AXIS_LIBS`;
  inter-pass bind (`qmap_axes_setup` after `gen_open`);
  `rec_axis_open(<primary-dir>/<name>.db)` (the slash matters for stoma —
  see below). First-ever `@` is loud (alongside-heuristic + stoma
  "no primary found" note); stoma emits "rebuilt N docs in X ms" (U4).
- **Axes bound**: joint (`external/libjoint/lib/libjoint.so`) —
  `rec_axis_store` parses **whole** value; `rec_axis_unstore` idempotent;
  `rec_axis_readback`; file-backed alongside `<dir>/joint.db`
  (id-index backfill). stoma (`external/libstoma/lib/libstoma.so`) —
  canonical `text` field of the **whole** value, memory+rebuild (never
  files); `rec_axis_readback` returns the folded text.
- **Ref-operand rule** (`qmap_write_ref`, `src/qmap.c:1218-1237`):
  full-digit string → literal u32; else primary reverse-view name lookup
  (`prim_hd+1`); miss → no ref. In composed mode (`roster_n>0` + `:a:`
  primary) an unresolvable `-d` operand errors
  ("write ref '…': no primary record", exit 1); outside composed mode the
  legacy string-key delete runs (and silently no-ops on numeric operands —
  F3).
- **Rendering**: composed get = `ref[ score] record` (score iff a queried
  axis ranks; best-first, ties asc; pure-filter asc); `-g .` on `:a:s`
  prints bare refs, one per line; empty result → `-1` (sentinel — matters
  for the reset loop). `-t N`/`--top`, `-b F`/`--bottom` whole-expression.
- **Discovery**: `QMAP_AXIS_PATH` dir-list for `lib<name>.so` (default
  `/usr/lib` — never used in this slice; D4); `test-real.sh:66` now
  defaults `QMAP_AXIS_REAL_LIBS` script-relative to the in-site
  `external/lib{joint,islet,sepal,stoma}/lib` builds (post-migration).
- **Joint query leaf** (verified in `external/libjoint/src/libjoint.c`
  `joint_decode` + rehearsal §2): `a=<date>[ b=<date>]`,
  `sscantime` formats (`YYYY-MM-DD`, `YYYY-MM-DDTHH:MM:SS`, epoch digits);
  half-open `[a,b)` presence. **F2 below: b unset/0 matches nothing.**
- **Stoma query leaf** (verified in `external/libstoma/src/libstoma.c`
  `stoma_axis_decode` + rehearsal): `field=<f> query=<text> [phrase=0|1]
  [matched=N]`; score = `matched / doc-token-count`.
- **Joint store shape** (verified via `joint_date_parse`,
  `src/libjoint.c:834-864`, prefix mode): leading date (`YYYY-MM-DD…`
  acceptable; `YYYY-MM-DDTHH:MM` without `:SS` falls back to the date-only
  prefix, trailing ignored) → open `[date, ∞)` interval; `ref ==
  UINT32_MAX` → `EINVAL`.
- **Stoma sidecar-scan** (`external/libstoma/src/libstoma.c:771-866`):
  when spec contains `/`, scans spec's dir for `*.roster`, opens
  `<base>` as `qmap_open(primary, "hd", QM_HNDL, QM_STR, pmask,
  QM_AINDEX | QM_MIRROR)` (`:840`) and bulk-reindexes. **This second open
  of the CLI's own primary is the F4 root cause.**

## 2. Rehearsal evidence (2026-09-15, /tmp dirs, real joint+stoma)

Seed used throughout:
`-p 1:"2026-09-13:Beacon Harbor lights" -p 2:"2026-09-14:alpha omega" -p 3:"2026-09-15:beacon beacon harbor"`.

| Probe | Result |
|---|---|
| explicit-ref store on `mem.db@joint,stoma:a:s` | refs `1 2 3`; first-run loud-empty note: `stoma: no primary found in '<dir>' (no *.roster sidecar)` |
| `--list-axes` | joint `y n y`, stoma `y y y`; roster sidecar persisted (`v=1 axes=joint,stoma`) |
| `joint="a=2026-09-14 b=2026-09-16"` | `{1,2,3}` full `ref record` lines (presence: all open `[d,∞)` intervals overlap the window) |
| `joint="a=2026-09-14"`, `joint="a=2026-09-14 b=0"`, `joint="b=0"`, `joint="a=2026-09-16"` | **all empty** |
| `joint="a=0 b=2026-09-14"` | `{1}` (refs dated < B) |
| `joint="a=2026-09-16 b=9999-12-31"` / `a=2026-09-14 b=9999-12-31` | `{1,2,3}` |
| `stoma="field=text query=beacon matched=1"` | `1 0.166667 …` + `3 0.166667 …` (1 = matched/tokens = 1/6) |
| composed AND `(joint window ∧ stoma beacon) -t 10` | `1 0.166667 …`, `3 0.166667 …` (stoma scores) |
| `-p "2026-09-14:remember beacon lights"` (no explicit ref) | stored under key **2026** (first-colon split), `joint: store rejected ref 2026` |
| joint-only roster: `-d 2` → reopen | **persists** (`-g .` → `1`) |
| stoma-only roster: `-d 2` → reopen | **no-op** (refs 1,2 remain) — F4 |
| joint+stoma, roster exists only after seed exit: `-d 3` → reopen | **no-op** (refs 1,2,3 remain) — F4 |
| joint+stoma, roster pre-exists (`--list-axes` first): seed → reopen | **primary saved empty** (`-g .` → `-1`) — F4 |
| classic `:a:s` (no roster): `-d 2`, `-D 1`, `-D 2` | all no-ops (legacy string-key delete) |
| `-g .` on empty primary | `-1` (single line) |
| mock-plugin control (`alpha,beta`, fold .so): `-d hello` → `-g .`=`2`; `-d 2` → `-g .`=`-1`, `-l` empty | `-d` machinery itself is sound (test-fanout's green states reproduced) |

## 3. Findings F1–F6

- **F1 — auto-ref ingest impossible for colon payloads.**
  `-p VALUE` splits at the *first* colon (`gen_lookup` `col` split), so
  `<DATE>:<TEXT>` becomes KEY:`VALUE` and the "ref" is garbage
  (observed ref `2026`). pi-mm must supply explicit refs; "next ref" =
  max existing + 1 from `-g .` (bare refs) or pi-mm-side state. Asserted
  by recipe shape, not by a test case (the failure is loud, not silent).
- **F2 — joint open-ended query (`a=D`, b unset/0) matches nothing.**
  Documented "b=0 open-ended" does not hold. Given open `[date,∞)` store
  intervals, the effective joint filter is `date < b`; the `a` bound is
  irrelevant against open ends. Dialect time leaf is therefore always a
  **bounded window** `joint="a=A b=B"` ("entries active in [A,B)",
  i.e. stored before B).
- **F3 — `-d REF`/`-D REF` work only in composed mode.**
  Classic `:a:s` treats the operand as a string key and no-ops. The
  dialect always names the roster file (`mem.db@…`).
- **F4 — `-d` (`-D`) with stoma in the roster does not persist.**
  Rehearsal A/B + joint-only/stoma-only isolation (see §2): del lost /
  seed lost whenever stoma's sidecar-scan mirror-opens the primary.
  Root cause pinned: two `"hd"`-named handles on one file; libqmap's
  exit-save lets the mirror's stale copy win (the repo's known
  "subset-order / double-open save clobber" hazard class). **User decision
  2026-09-15: fix it (not findings-only).** See §6.
- **F5 — reset has no single invocation.** No clear-all flag exists;
  "-X ''" is unarmed classic (the §8 TODO placeholder was a dead end).
  Reset is the documented loop: enumerate refs via `-g .`, `-d` each,
  skipping the `-1` empty sentinel; idempotent.
- **F6 — dialect = joint+stoma (time+text) only.**
  One payload string must satisfy every axis grammar; `<DATE>:<TEXT>`
  is legible to joint (leading date) and stoma (text) but not to islet
  (`x,y;…` grammar → `EINVAL` loud partial) or sepal-floats. islet/sepal
  payload shapes are a different dialect, out of pi-mm's memory-ingest
  scope. test-real's 4-axis fixture seeds axes around the CLI (via
  `real_seed`), not through it — the mm dialect goes *through* the CLI.

## 4. Normative pi-mm recipes (settle U3; `CLI-SURFACE-EXAMPLES.md` §8 lands these)

```sh
# store — explicit ref; payload <DATE>:<TEXT> (F1, F6)
qmap -p 1:"2026-09-14:Beacon Harbor lights" "mem.db@joint,stoma:a:s"

# search — bounded time window AND text; -t cap, -b floor (F2)
qmap -X '(joint="a=2026-09-14 b=2026-09-16" AND stoma="field=text query=beacon matched=1")' -g . mem.db -t 10

# forget — idempotent; -D identical on axes (F3, F4)
qmap -d 1 "mem.db@joint,stoma:a:s"

# reset — documented loop (F5); skip the "-1" empty sentinel; idempotent
for ref in $(qmap -g . mem.db | grep -E '^[0-9]+$'); do qmap -d "$ref" "mem.db@joint,stoma:a:s"; done
```

Stoma notes to keep in the doc: stoma is derived — a forgotten ref
reappears in text queries if still in the primary (by design, D13), so
forget deliberately deletes primary *and* axes in one op; first-ever `@`
is loud-empty (never silent); reopen shows "rebuilt N docs in X ms".

## 5. `test-mm.sh` design (TDD; write all, run red, then F4 fix → green)

New `external/libqmap/test-mm.sh` (+ `Makefile:62-67` `test:` row),
self-contained shell like the sibling gates (`assert_eq/ok/fails`),
own `$td`, real axes via the `test-real.sh:66` `QMAP_AXIS_REAL_LIBS`
resolver copy, fresh local builds (D4). Case order:

1. Preflight: `libjoint.so` + `libstoma.so` present (same loop as
   test-real preflight, 2 names).
2. Control joint-only: seed 1/2 on `j.db@joint:a:s`; `-d 2` → reopen
   `-g .` = `1` (proves the joint path already; green pre-fix).
3. **RED-A (F4a):** `--list-axes "$td/m.db@joint,stoma:a:s"` first (roster
   pre-exists), seed 1/2/3, reopen: assert `-g .` = `1 2 3` (seed
   persisted).
4. **RED-B (F4b):** joint+stoma seed → `-d 3` → reopen: primary lacks 3
   (`-g .` = `1 2`), stoma `query=beacon` lacks 3, joint window lacks 3;
   also `-d 3` again → exit 0 (idempotent forget).
5. Composed search: `-X '(joint="a=2026-09-13 b=2026-09-15" AND
   stoma="field=text query=beacon matched=1")' -g . -t 10` → exact
   ref-scored lines (`1 0.166667 2026-09-13:Beacon Harbor lights`,
   `3 …`; exact fixture grammar from §2). Pure-filter joint-only case
   `joint="a=0 b=2026-09-14"` → `1 2026-…` (no-score lines).
6. Reset: fresh `m.db@joint,stoma` seeded 1/2/3 → run the §4 loop →
   reopen: `-g .` **single line `-1`**; stoma query empty; joint window
   empty; second loop run exit 0 still `-1` (idempotent).
7. `-1` sentinel guard: loop over an empty db issues zero `-d` calls
   (the `grep -E` gate covers it; assert no `write ref` noise on stderr).
8. Classic zero-plugin regression (fresh `:a:s`, no roster): `-p 5:q`,
   `-g .` = `5`, `-g .` on empty = `-1`.

## 6. F4 fix path (root cause §1/`libstoma.c:840`)

Candidate fixes, smallest first:

- **(a) libqmap dedup of identical (file, dbid) opens [preferred].**
  A second `qmap_open` of the same path+`"hd"` (same types) aliases the
  live map handle instead of creating a second copy. Eliminates the
  whole double-open-clobber class. Only stoma's mirror re-opens a
  primary (joint/islet/sepal open distinct `<name>.db` files; roster
  sidecar uses dbid `@roster`), so blast radius is minimal. Watch:
  `qmap_close` interplay, mask/shape mismatches (stoma uses D11-derived
  pmask; must match the CLI's effective mask — document the requirement).
- **(b) stoma-side guard [fallback].** `rec_axis_open` detects (or is told
  via spec) that the primary is already open in-process and skips its own
  open, rebuilding from… the same PRIM handle it cannot reach across the
  dlopen boundary without a new channel. Weak — likely forces (a) anyway.
- **Not in scope:** new CLI flags, readback surface, islet/sepal payloads.

Decision recorded by the red tests: implement (a) only if the full
libqmap suite stays green; otherwise (b)-style or re-scope as finding.

## 7. Doc updates

- `CLI-SURFACE-EXAMPLES.md`: §8 rewritten to §4 recipes + F1/F2/F5 notes;
  §11 unchanged (no new flags); keep the `[2B-5]` legend tag.
- `PHASE-2-CLI.md`: 2B-5 row → **DONE** (date); final-gate bullet cites
  `test-mm.sh`; nothing else re-opens.
- `mm-plan/README.md`: §3 status (2B-5 DONE), U3 **SETTLED**, §12 tick.
- `external/libqmap/CHANGELOG.md`: `## [Unreleased] — 2B-5 mm dialect`
  entry (recipes, test-mm.sh, F4 fix record).
- Sibling (`external/libstoma` or libqmap core) CHANGELOG only if touched.

## 8. Gates

- New red cases fail first (record exact output in §9), then green after fix.
- `make -C external/libqmap test` green (6 scripts) with
  `QMAP_AXIS_REAL_LIBS` **and** `QMAP_MASK` unset.
- Touched sibling suite(s) green (`external/libstoma`, and `libjoint` as
  insurance); root `make` green (W06).
- `grep -rn '~/lib\|/home/quirinpa/lib' mm-plan/ external/libqmap/`
  shows only intentional migration-note hits.
- D4: no `/usr` installs; commit only when asked.

## 9. Execution log

- 2026-09-15: doc created (§1–§8) from read-only rehearsal + plan-mode
  analysis; **user decision: fix F4**. Implementation begins (§5 TDD).
- 2026-09-15: `test-mm.sh` written (all §5 cases) + wired into
  `Makefile` `test:` — first run **RED exactly as predicted**:
  `seed-persisted` (expected `1|2|3`, actual `-1`),
  `composed-and`/`joint-before`/`forget-stoma` (empty),
  `forget-primary` (expected `1|2`, actual `-1`),
  `reset-empty`/`reset-again-empty` (expected `-1`, actual `1|2|3`),
  `reset-stoma-empty` (still finds 1,3).
- 2026-09-15: F4 fix landed in `external/libqmap/src/libqmap.c`
  (`qmap_open` aliases the live handle on identical (file, map, shape)
  re-open; registry + file-ids membership + record/types/mask match).
  Rebuilt clean (zero warnings), re-ran: all green except
  `forget-joint` — a **test bug**, not a code bug (expected `""` for
  `joint="a=2026-09-15 b=2026-09-16"` after forgetting 3, but presence
  semantics correctly return refs 1+2 whose open intervals still overlap;
  corrected to the two `ref record` lines — F2 working as documented).
  `test-mm.sh: all green`.
- 2026-09-15: gates — `make -C external/libqmap test` all 6 scripts
  green (env unset); `external/lib{stoma,joint,islet,sepal}` suites
  green; root `make` W06 PASS; stale-path grep clean (intentional hits
  only); D4 (no installs).
- 2026-09-15: docs landed — `CLI-SURFACE-EXAMPLES.md` §8 + legend (U3
  settled), `PHASE-2-CLI.md` 2B-5 DONE + final gate, `README.md`
  §3/U3/§12, `external/libqmap/CHANGELOG.md` Unreleased 2B-5 entry.
  No sibling lib code touched (fix is libqmap-core) → no sibling
  CHANGELOG. Not committed (never asked).
