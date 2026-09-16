# 7-AXIS-NAMESPACE-PLAN.md — Per-primary axis-store isolation (fix directory corruption)

Status: **DONE 2026-09-16** — all four phases landed, every gate green
(commit only when asked; site tree left uncommitted). Status lives in
`README.md` §0 row 8 (v26).

> **D14 update (2026-09-16):** historical record of the per-primary store
> naming as built. Unrelated to D14, but note pi-mm's query surface has
> since moved to the flags form — `-X '(stoma="field=text matched=1" AND
> sepal)' --query=… [--min-sim=…]` — structure in `-X`, runtime values on
> CLI flags (leaf spec > CLI > env). See `mm-plan/PHASE-2-CLI.md` D14
> carve-out and `mm-plan/CLI-SURFACE-EXAMPLES.md` §11.

## The current issue

Axis stores are **directory-scoped**, while the primary DB, its roster
sidecar, and the CLI's own handle are **primary-scoped**. Running two
different primary DBs in the same directory corrupts every axis:

### 1. Directory-scoped axis spec (D9) — `qmap.c:964-972`

```c
qmap_axes_spec(const char *name, ...)  →  "<dir>%s/<name>.db"
```

`dirname(qmap_path)` + `<name>.db` means the axis files are
`./joint.db`, `./stoma.db`, `./sepal.db` **shared by every primary in
that directory**. Two DBs (e.g. `mem.db` and `mem2.db`) both write their
ref 1 payloads/vectors into the same `joint.db` / `sepal.db` → refs
overwrite/collide across independent databases.

### 2. stoma rebuild binds the wrong primary — `libstoma.c:771-875`

stoma is memory-only; each `rec_axis_open` rebuilds its text index from
the primary AINDEX. It picks the primary by scanning the directory for
`*.roster` and taking the **first one `readdir()` returns**
(`libstoma.c:810-838`, `break` on first hit). It never knows which
primary the CLI actually opened — its spec is just `./stoma.db`. With
two rosters in the directory it silently rebuilds from the wrong primary:

```
stoma: rebuilt 0 docs in 0.00 ms     ← every query, even against the populated DB
```

Text search returns nothing while `-l` / `-g .` (primary-direct) show the
records. Pure silent corruption — no error path.

### 3. `./` alias mismatch — `qmap.c:872` vs `libstoma.c:840`

CLI opens the primary verbatim (`mem2.db`); stoma re-opens it as
`./mem2.db` (from `dirname("./stoma.db")`). `qmap_open`'s live-handle
alias key is the exact filename string, so the two handles never alias;
stoma loads stale on-disk bytes instead of the invoking process's live
handle.

### 4. Field evidence (2026-09-16 session, `~/site`)

- `mem.db.roster` (15:21) + `mem2.db.roster` (15:39) in one directory.
- `-p 1:… mem2.db@…` → `1`, `-g .` → `1`, `-l` → payload correct.
- `stoma=…` on the same DB → always `rebuilt 0 docs`, zero hits.
- `sepal.db` (3436 B) held vectors from prior runs/DBs → `qmap: 3 refs
  skipped: no primary record`.
- First run of a fresh primary with pre-existing orphan `joint.db`:
  `qmap: axis store found (./joint.db) beside mem2.db but no roster yet`.

Root cause is one: **axis state is keyed by directory, not by primary.**

## Design — per-primary axis-store isolation

Every artifact an axis owns derives from the *primary's own path*, so a
separate DB file is automatically self-contained (the requirement): 

1. **Per-primary axis store names.** `qmap_axes_spec` becomes
   `<dir>/<primary-base>-<name>` (e.g. primary `garden.db` →
   `./garden.db-joint`, `./garden.db-stoma`, `./garden.db-sepal`).
   Matches the roster's existing per-primary shape (`<primary>.roster`).
   Two DBs in one directory can no longer share a bit of axis state.

2. **`QMAP_AXIS_PRIMARY` binding env.** `qmap_axes_setup` (after the
   primary is open, `qmap.c:1941-1945`) sets
   `QMAP_AXIS_PRIMARY=<qmap_path verbatim>`. stoma's rebuild prefers it
   for the roster/primary lookup, solving both the wrong-primary pick
   AND the `./` alias mismatch (verbatim string aliases the CLI handle).

3. **Loud multi-roster guard.** When stoma has no env primary and its
   directory contains more than one `*.roster`, it must NOT silently
   guess — warn loudly and leave the text index empty (exit 0, degraded)
   instead of corrupting recall with the wrong primary's data.

### Phase A — libqmap (`external/libqmap`)

- **A-1** `qmap_axes_spec` (`qmap.c:964-972`): emit
  `<dir>/<basename(primary, .db kept)>-<name>` — e.g. primary
  `data/garden.db` → `data/garden.db-joint` (matches the roster sidecar's
  `<primary>.roster` shape; the `.db` extension stays so the primary
  identity is unambiguous).
- **A-2** `qmap_axes_setup` (`qmap.c:1085+`): `setenv("QMAP_AXIS_PRIMARY",
  qmap_path, 1)` before binding slots.
- **A-3** The orphan-store diagnostic (`qmap.c:1103-1115`) uses the new
  spec automatically (it calls `qmap_axes_spec` — no code change,
  verify message shows `garden.db-joint` now).
- Tests: unit test for `qmap_axes_spec` output shape (new name + legacy
  fallback if adopted); integration: two primaries in one dir produce
  distinct axis files; `make test` / `make valgrind` green.

### Phase B — libstoma (`external/libstoma`)

- **B-1** `rec_axis_open` (`libstoma.c:771-875`): if
  `getenv("QMAP_AXIS_PRIMARY")` is set and `<it>.roster` exists, use that
  primary (open with the exact string → aliases the live handle).
- **B-2** Fallback path (env unset, in-process API users): count
  `*.roster` entries in `dirname(spec)`; `0` → warn + empty index
  (existing), `1` → use it (existing), `>1` → **warn loudly + empty
  index** (new; was silent wrong-primary).
- Tests: rebuild-from-env-primary picks the right DB with two rosters;
  multi-roster-without-env leaves a loud warning and empty index; zero
  rosters unchanged. `make test` green.

### Phase C — libjoint / libsepal (`external/libjoint`, `external/libsepal`)

- Both `rec_axis_open` consume the spec string verbatim
  (joint → `joint_init(spec)`; sepal → `sepal_open(spec)`). With the new
  per-primary spec from A-1, **no code change** — verify + extend axis
  open/store tests to cover a per-primary filename round-trip.
- stoma rebuild guard (B-2) is the only behavior change in this phase.

### Phase D — site + docs + pi-mm

- Rebuild: `make -C external/libqmap && make -C external/libstoma &&
  make -C external/libjoint && make -C external/libsepal && make` (site
  W06 PASS).
- pi-mm untouched: `resolveAxisPath` returns *library* dirs; the axis
  *store* files are qmap-internal. Re-run `deno test` / `lint` /
  `check-complexity` / `integration-mm.sh` to prove no regression.
- `integration-mm.sh`: add a two-DB-in-one-dir smoke — two rosters + two
  primaries, store distinct payloads, assert both `stoma=…` queries find
  only their own refs and both `sepal=` scans resolve cleanly.
- Docs: `README.md` §0 row 8 + §6 / resume; this file as the record;
  `RUNNING.md`/SKILL.md note "each `*.db` owns its axis stores
  (`<primary-base>-<name>`); a directory may now host many databases".

## Migration / legacy (decision point)

Existing databases created under the legacy directory-scoped naming
(`./joint.db`, `./sepal.db`) will no longer be found by the new spec.

- **Option 1 (recommended): clean break, no fallback.** Dev-stage
  project, conventions in flux; both databases in the wild session were
  throwaway. Document "rename or re-store legacy DBs". Simplest, zero
  ambiguity, no way to reintroduce shared files.
- **Option 2: graceful legacy fallback.** joint/sepal: open the
  per-primary name; if absent and a legacy `<name>.db` exists beside the
  primary dir, use the legacy file on a one-time basis (read legacy,
  write new). stoma needs no fallback (memory-only, derives from the
  roster). Keeps old single-DB dirs working but retains a foot-gun if two
  legacy dirs exist — the guard in B-2 makes it loud, not silent.

Merge decision when implementing: **Option 1** unless a legacy DB must
survive.

## Gates

- Per-repo `make test` green (libqmap, libstoma, libjoint, libsepal);
  libsepal `make valgrind` unchanged-from-baseline status.
- Two-DB-in-one-dir smoke: separate axis files created, both primaries
  answer `stoma=` + `sepal=` with only their own refs, `rebuilt N docs`
  prints N>0 for each.
- Site `make` W06 PASS; pi-mm gates re-run green; loadable-and-graceful
  at every commit; commit only when asked.

## Verification (manual, after Phase D)

```sh
export QMAP_AXIS_PATH=external/libjoint/lib:external/libstoma/lib:external/libsepal/lib
mkdir -p data
qmap -p 1:"2026-09-14:I was with my grandfather under the cherry tree" "data/garden.db@joint,stoma,sepal:a:s"
qmap -p 1:"2026-09-14:A boat ride with a panda can be deep" "data/park.db@joint,stoma,sepal:a:s"
ls data/                        # garden.db-joint garden.db-stoma garden.db-sepal park.db-*
qmap -X 'stoma="field=text query=grandfather matched=1"' -g . "data/garden.db@joint,stoma,sepal:a:s" -t 10   # → ref 1
qmap -X 'stoma="field=text query=grandfather matched=1"' -g . "data/park.db@joint,stoma,sepal:a:s"  -t 10   # → (nothing)
```

(garden and park share the directory; each returns only its own recall.)