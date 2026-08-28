# Quest: Gig Format Picker & Customization (Detail & Edit)

## Goal
Enable users with edit permissions on a gig to customize and select the song format (`format` / `song.types`) using the standard omni-dropdown / picker component on both `/gig/:id` (detail page) and `/gig/:id/edit` (edit page), respecting group predetermined default formats while allowing flexible custom format selection.

## Current Status
Implementation completed, verified with targeted unit and E2E tests, and full test suite (`make test` with 102/102 passing tests) confirmed.

## Why this matters
Gigs organize songs by format categories (often defined or seeded by their parent group `grp`, referencing `song.types`). Users with edit permissions can now customize and select the song format (`format` / `song.types`) using the standard omni-dropdown / picker component on both `/gig/:id` (detail page) and `/gig/:id/edit` (edit page), respecting group predetermined default formats while allowing flexible custom format selection. Pickers provide search, pagination, no-JS fallback via sibling forms, and instant dynamic WASM/JS updates.

## Key Architecture & Design Decisions

1. **Detail Page (`/gig/:id`)**:
   - For users with edit permissions (`is_owner`), the format subtitle (`s->type`) on each song row is an interactive inline omni-dropdown trigger (`sb_render_format_picker`).
   - Format picker targets `"song.types"`, with key `format` and scope `row_idx` (`#sb-fmt-pick-post-N`, `pick_q_format__N`, `pickq-format__N`).
   - Sibling GET form retains query params and preferences (`t`, `b`, `l`, `m`, `z`).
   - POST submission targets `/api/gig/:id/song/:n/replace` updating `format` for row `n` without modifying song or transpose.
   - Non-owners see the read-only format badge.

2. **Edit Page (`/gig/:id/edit`)**:
   - Upgraded row format inputs from `<select>` / plain text to the standard single-ref picker component `hyle_bud_picker_field` targeting `"song.types"`.
   - Added sibling GET forms `pickq-fmt_N` so no-JS searching and pagination on format work seamlessly alongside song pickers.

3. **Data & Server Sync (`mods/gig/gig.c`)**:
   - `detail_song_cb` and `sb_load_song_row` carry the row's assigned `format` and its resolved display label into `sb_song_row_data_t` for WASM and SSR.
   - `handle_sb_song_replace_authorized` preserves existing song or updates format independently.
   - Scoped picker collection handles both song and format pickers across detail and edit pages.

## Acceptance Criteria & Polish Checklist
- [x] Row format can be updated on detail page `/gig/:id` via omni-dropdown picker when user has edit perms.
- [x] Format dropdown in detail page provides search, pagination, and no-JS sibling form fallback.
- [x] Group predetermined formats (`grp.format`) appear in format pickers.
- [x] Row format can be updated on edit page `/gig/:id/edit` via `song.types` picker with sibling GET forms.
- [x] Non-owners see clean read-only format labels without edit controls.
- [x] Zero site-specific JavaScript; all logic in C/WASM/pure CSS.
- [x] All targeted E2E and module tests pass with zero errors (102 passed, 0 failed in `make test`).

## Next recommended step
Prompt user for final wrap-up / feedback loops.

