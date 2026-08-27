# Active Task: ghost-repertoire

Status: **in_progress**

## Goal
Fix the gig addition metadata bug ("ghost gigs" where `grp` link was stored in filesystem without standard `source_update_item` flow) and establish a pure derivation model for group repertoire:
1. Repertoire rows are strictly partitioned into:
   - **Derived rows**: dynamically tallied/computed from the group's gigs (majority transpose across gigs, format, first-seen tie-breaker).
   - **Pinned rows (`pinned=1`)**: explicitly pinned/added or key-customized by humans in the group.
2. Ensure persistence purity: `grp.songs` partition on disk stores *only* pinned rows (`pinned=1`), never persisting derived rows to disk, preventing sync drift.
3. Unify single read-path / iteration helper `rep_for_each_merged` (or unified derivation iterator) across group detail rendering, song pickers, and random song selection.
4. Clean up `handle_sb_add` and ensure `.meta` / `grp` backfill / repair on boot.

## Codebase Audit & Architecture Findings
- **Current State of `handle_sb_add` in `mods/gig/gig.c`:**
  - `index_add_item` creates directory, records ownership, writes title, and calls `source_refresh_row(fd, "gig.items", id)`.
  - When `grp` is present in multipart form, `handle_sb_add` was manually doing `gig_meta_read` + `gig_meta_write` directly to filesystem and calling `source_refresh_row`.
  - Proper way in current architecture: standard `source_update_item` updating `gig.items` dataset with `dh` (qmap with `grp`), properly maintaining inverse index and metadata files via libhyle-source.
- **Current State of `rep_rebuild` in `mods/grp/grp.c`:**
  - Tallies all gigs associated with `grp_pos` via inverse index `qmap_inv_get(sb_def->fields_hd, "grp", grp_pos, ...)`.
  - Collects pinned rows from `grp.songs` and appends derived rows from tally.
  - Currently saves the *entire* merged list (pinned + derived with `pinned=0`) into `grp.songs` on disk via `hyle_source_ordered_save("grp.songs", grp_id)`.
  - By persisting derived rows to `grp.songs`, any deletion or modification of gigs requires disk rewrites of `grp.songs`, and disk partition contains duplicate state.
- **Repertoire Persistence Redesign:**
  - `grp.songs` on disk will store **only pinned rows** (`pinned=1`).
  - `rep_rebuild` prunes non-pinned rows from disk partition if legacy files had them, leaving only pinned rows on disk.
  - Runtime queries for repertoire (group detail SSR, gig seed/randomize, song pickers) query pinned rows + derive from gig songs on the fly via unified callback / iteration `rep_for_each_merged`.
- **Boot Heal / Sync:**
  - In `mods/gig/gig.c` `xy_install`, verify all gigs in `gig.items` have their `grp` field indexed in memory; if missing but present in `.meta` / disk file, update row in hyle.

## Implementation Plan

### Stage 1: Fix `handle_sb_add` in `mods/gig/gig.c`
- Refactor `handle_sb_add` to write `grp` via `source_update_item(fd, "gig.items", id, dh)` instead of direct `gig_meta_write`.
- Verify inverse index updates immediately.
- Test: `tests/e2e/grp-gig-flow.test.ts` & `mods/gig/test.sh`.

### Stage 2: Introduce `rep_for_each_merged` & Repertoire Query Abstraction
- Export `XY_DECL(void, rep_for_each_merged, const char *, grp_id, rep_entry_cb, cb, void *, user)` in `mods/grp/grp.h` and implement in `mods/grp/grp.c`.
- Callback receives: `(const char *song_id, int transpose, const char *format, int pinned, void *user)`.
- Iteration order: pinned rows first, followed by derived rows (in tally order / first-seen tie break) not already pinned.
- Update callers to use `rep_for_each_merged`:
  - `ch_load_repertoire` in `mods/grp/grp.c` (group detail SSR).
  - `get_random_repertoire_by_type` in `mods/gig/gig.c` (gig randomization & seeding).

### Stage 3: Make `grp.songs` Disk Persistence Store Only Pinned Rows
- Update `handle_grp_song_add_auth` and `handle_grp_song_key_auth` to ensure pinned=1 is set.
- Update `rep_rebuild` in `mods/grp/grp.c`:
  - `want[]` contains *only* `pinned=1` rows.
  - Redundant writes are skipped if `grp.songs` already only contains the current pinned rows.
  - On startup or mutation, `rep_rebuild` strips unpinned rows from `grp.songs` on disk.
- Update `handle_grp_song_del_auth` to remove pinned row or unpin.

### Stage 4: Boot Backfill & Self-Healing in `gig.c` / `grp.c`
- In `gig.c` `xy_install`, scan `gig.items` to ensure inverse index is consistent with disk.
- Re-run `rep_rebuild` to sanitize disk partitions.

### Stage 5: Verification & Quality Gates
- Targeted E2E & unit tests:
  - `mods/gig/test.sh`
  - `mods/grp/test.sh`
  - `tests/e2e/auto-repertoire.test.ts`
  - `tests/e2e/auto-repertoire-correctness.test.ts`
  - `tests/e2e/grp-gig-flow.test.ts`
- Full test suite `make test` & boundary checks (`sh scripts/check-module-boundaries.sh`).

## Decisions Made
- `grp.songs` on disk: stores only explicit user additions (`pinned=1`) and key overrides.
- Derived repertoire is ephemeral and computed on demand via `rep_for_each_merged`.
- `handle_sb_add` uses `source_update_item` exclusively.

## Acceptance Criteria & Polish Checklist
- [x] `handle_sb_add` persists `grp` cleanly through `source_update_item`.
- [x] `grp.songs` on disk (`var/grp/<id>/data.txt` or DSV) never contains unpinned rows (`pinned=0`).
- [x] Group page detail correctly renders both pinned and derived songs with majority key.
- [x] Gigs randomize and seed correctly from merged repertoire.
- [x] Boot backfill / healing in `gig.c` repairs any out-of-sync in-memory group links and prunes unpinned rows.
- [x] All existing auto-repertoire tests pass.
- [x] Zero compiler warnings, zero boundary check violations, zero debug artifacts.

## Current Status
Completed all stages:
1. `handle_sb_add` unified to use `source_update_item` for `grp` persistence.
2. `rep_for_each_merged` implemented and exported in `grp.h`/`grp.c`.
3. `rep_rebuild` simplified to only persist pinned rows (`pinned=1`), pruning unpinned rows from disk partition.
4. Consumers (`ch_load_repertoire` and `get_random_repertoire_by_type`) updated to use `rep_for_each_merged`.
5. Full verification and test suite passed with 100% success (99/99 E2E tests, C unit tests, sanitizer tests, boundary checks).

## Next Recommended Step
- Prompt user for task wrap-up flow options.
