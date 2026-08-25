# Proposal / Future Task: ghost-repertoire

Status: **proposal**

## Goals & Scope
Repertoire persistence model + the ghost-gig bug that breaks it.

A group's repertoire is the set of songs its gigs use, plus explicit human customizations. Exactly two classes of rows exist:
1. **Derived rows** — implied by gig contents. Never persisted. Computed at boot / render time.
2. **Customized rows** (`pinned=1`) — human decisions: songs added directly to the repertoire. These are the only rows persisted in `grp.songs`.

**Symptom:**
Songs added to a gig never appear in the group's repertoire, even though `rep_rebuild()` runs on every gig mutation. Most affected flow: "+ add gig" on the group page.

**Root cause (ghost gigs):**
`fs_load`/`fs_scan` populate hyle rows from per-field files only — `.meta` is invisible to them. `handle_sb_add` persists only `title`; the chosen group goes into `.meta` via `gig_meta_write`. The follow-up `source_refresh_row` finds no `grp` file, putting a row without `grp`. The inverse index never registers the gig, derivation tallies zero gigs.

## Requirements
- Persistence rule: only customized rows hit the disk.
- Editing a gig reshapes the repertoire with zero repertoire writes.
- No drift can accumulate between `gig.songs` and `grp.songs`.
- Row writes must go through `source_update_item`; never around it into `var/`.

## Implementation Plan
1. **Add path writes `grp` through `source_update_item` — gig.c**
   Unify both branches of `handle_sb_add`. Delete trailing `source_refresh_row`. Drop local `gig_meta_write` dance.

2. **Boot backfill `.meta` → hyle — gig.c `xy_install`**
   Before all-groups rebuild loop: compare `qmap_get_field_str(..., "grp")` against `gig_meta_read()`. On mismatch, push `{grp}` through `source_update_item`.

3. **`rep_for_each_merged` — one read path (grp.h / grp.c)**
   New XY export, callback style: `XY_DECL(void, rep_for_each_merged, ...)`
   Iteration order: pinned rows first, then derived rows (tally order). Switch consumers: `ch_load_repertoire`, `get_random_repertoire_by_type`, `sb_load_edit_song_options`.

4. **`rep_rebuild` becomes pinned-prune — grp.c**
   Keeps idempotent contract, but `want[]` = current rows with `pinned=1` only. First boot under this strips legacy persisted derived rows.

## Out of scope
- No `.meta` awareness in `fs_load`.
- `song_source` persistence untouched.
- No UX/WASM changes: SSR-only data flow.
- `resolve_song_id` legacy repo-entry mapping left as-is.

## Verification
1. `make`; `sh scripts/check-module-boundaries.sh`
2. Create group → "+ add gig" from group page → assert `var/gig/<id>/grp` contains the group id.
3. Add songs to the gig → group Repertoire lists them with majority transpose; pinned rows persist only.
4. Ghost heal: delete `var/gig/<id>/grp` from an existing gig → restart → file restored from `.meta`, repertoire correct.
5. Majority rule: two gigs with transposes 0/+2 → repertoire shows +2; change one gig → majority flips without repertoire write.
6. Randomize on a gig picks from the merged repertoire, including derived songs.
