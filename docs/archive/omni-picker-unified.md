# Archived Task: omni-picker-unified

Status: **Completed and Archived**
(This file was merged from the legacy `plan.md` and `PROGRESS.md` files.)

## Goal
Replace the current "full table" song picker in the Gig Add/Edit Song flow (used in both `/gig/edit` and `/gig/detail`) with the new "Omni-Dropdown" picker. One shared omnisearch picker construct used everywhere a song is chosen.

## Decisions made
- Edit rows drop the per-song `<select>`; they show title link + Change (→ `?replace=N`) + hidden `song_N` so the bulk Save contract is unchanged.
- Replace preserves the row's existing transpose/format silently (picker submits `song_id` only; server keeps current values).
- After a picker action taken on the edit page, redirect back to the edit page (`back` field, validated prefix `/gig/`).

## Files touched
- `mods/gig/gig.c` (replaced `sb_load_song_picks` and `sb_load_edit_song_picks` with `pick_view_collect`, fixed replace POST 400 error by switching `ICTX_CSRF_MPFD` to `ICTX_CSRF_QUERY`)
- `mods/gig/ux/song_picker.c` (new file, stopped using `hyle_bud_table_actions`, used `hyle_bud_picker_field`)
- `mods/gig/ux/detail.c` & `mods/gig/ux/edit.c` (deleted old replace branch, used `pick_view_t`)
- `tests/e2e/gig-edit.test.ts` & `tests/e2e/gig-replace.test.ts` (updated tests for new save contract and new POST mechanism)
- `scripts/check-ux-purity.sh` (updated allowlist)

## Progress Log
- **mods/gig/ux/song_picker.c**: created and rendered.
- **detail.c**: deleted the ~220-line dropdown replace branch, `g_sb_replace_opts` globals, and `extern sb_load_format_options`.
- **edit.c**: rewritten, no more `axil_env_get` loader, real csrf in post form.
- **gig.c**: `sb_load_edit_song_picks` moved here, dead loaders deleted, add/replace handlers honor validated `back`.
- **gig.c bulk-save fix**: accept bare song ids.
- **Bug Fixed**: Replace POST returned 400 "Missing song_id". Fixed by switching `ICTX_CSRF_MPFD` to `ICTX_CSRF_QUERY`.

## Verification
- `make` clean, `make clients`.
- `check-module-boundaries.sh`, `check-ux-purity.sh`, `check-wasm-imports.sh` all pass.
- Playwright e2e run after replace fix: **95 passed / 1 failed** (failure is unrelated `song-filter-dropdown`).
