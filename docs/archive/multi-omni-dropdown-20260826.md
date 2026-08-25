# Task: Multi-Omni-Dropdown (per-row song pickers with default value)

## Task Refinements & Iterations

**2026-08-25 (clarification):** In the gig detail (songbook) page, when the user is the author (`is_owner`), each song row's title area is rendered as a **standard omni-dropdown picker with the current song pre-selected as its default value** (`sel = { id: s->song_id, label: s->title }`, `nsel = 1`):
- The picker displays the current song's title as its default value.
- Opening the picker lets the author search and pick a different song from `song.items`.
- Submitting the row's form POSTs to `/api/gig/:id/song/:n/replace` with the chosen `song_id` and swaps that row's song.
- For visitors (`!is_owner`), the title area renders as a plain link `<a>{s->title}</a>`.
- The legacy 🔁 button, page-level replace mode, and `?replace=N` navigation are completely removed.

**2026-08-25 (refinement — edit page alignment):** Extend the per-row picker with default value pattern to the **gig edit page (`/gig/:id/edit`)**:
- In the gig edit form (`sb_render_edit_form`), each song row's song title area renders a song picker (`hyle_bud_picker_field`) for `song_N` with the row's current song pre-selected as default.
- The user can pick a different song right in the row's dropdown; on saving the edit form (`POST /gig/:id/edit`), the selected `song_N` value is submitted.
- The top omnisearch picker remains for adding new songs.
- The legacy 🔁 Change Song button (`?replace=N`) and `sb_edit_set_replace` on the edit page are removed.

**2026-08-25 (iteration — first-class default_id, auto_submit, and flex-grow layout):**
- Standardized `site_ui_action_picker` in `mods/common/ux/site_forms.c` to accept `default_id`, `default_label`, `scope`, and `auto_submit`.
- All pickers use the standard default `.hyle-picker` appearance (identical between Add and Replace).
- Sized buttons to 36px (`items-center`), cleanly hidden when client scripts are active and auto-submitting on option selection.
- Added `flex: 1 1 0%; min-width: 0; width: 100%` so all pickers flex-grow smoothly across the full available row width on both detail and edit pages.

**2026-08-25 (iteration — full no-JS support & single-select auto-close on edit page):**
- Resolved nested form bug on the edit page by placing the top Add Song action picker outside the main edit form `#edit-form`.
- In `htdocs/hyle-fragments.js`, selecting an option in any single-select picker automatically closes the dropdown (`details.removeAttribute('open')`), exposing form actions cleanly.
- When `POST /gig/:id/edit` is submitted, the newly selected radio option (`input[name="song_N"]:checked`) is cleanly serialized and persisted.

## Current Status
- [x] Brainstorm/design completed 2026-08-25 (promoted from `docs/future/`)
- [x] Picker keyboard/a11y groundwork landed
- [x] Scoped `pick_view_collect_scoped` XY API
- [x] Detail page per-row replace with default value pre-selected
- [x] Edit page per-row replace with default value pre-selected
- [x] Global click-outside, Escape key, and exclusive toggle handling in `hyle-fragments.js`
- [x] Standard default picker look and first-class `default_id` / `auto_submit` in `site_ui_action_picker`
- [x] Flex-grow (`flex: 1 1 0%; min-width: 0; width: 100%`) applied to all pickers
- [x] Edit page no-JS exclusive picker state and search scoping verified
- [x] Edit page form submission with updated song selection verified end-to-end
- [x] Targeted test suite passing with 0 failures, 0 debug artifacts

## Goal & Scope

Enable **per-row omni-dropdown song pickers with default values in the songbook and edit pages**,
sharing the standard single-reference `.hyle-picker` appearance and first-class `site_ui_action_picker` abstractions, with flex-grow layout, complete no-JS fallback, and end-to-end saving on edit forms.

---

## TDD & Quality Checklist

- [x] Detail page tests pass (`tests/e2e/gig-replace.test.ts`, `tests/e2e/gig-replace-view-no-crash.test.ts`)
- [x] Edit page test passing with in-browser selection + save (`tests/e2e/gig-edit.test.ts`)
- [x] Edit page no-JS search scoping and closed Add picker tested in `tests/e2e/gig-edit.test.ts`
- [x] Add row test passing (`tests/e2e/gig-add-row-bug.test.ts`)
- [x] Module boundary & purity checks pass (`scripts/check-module-boundaries.sh`, `scripts/check-ux-purity.sh`, `scripts/check-wasm-imports.sh`)
- [x] Targeted tests pass cleanly (4/4 in 8s)
