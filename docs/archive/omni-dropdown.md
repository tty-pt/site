# Task: omni-dropdown

## Goal
Abstract omni-dropdown / picker functionality so consumer modules (`mods/gig`, `mods/song`, `mods/grp`, etc.) don't need custom boilerplate or logic that belongs inside the picker abstraction. Consumer modules should only pass parameters or customize callbacks.

## Current Status
- [x] COMPLETED — Standardized picker helper abstractions in `site_ui` (`mods/common/ux/`), refactored `mods/gig`, `mods/song`, and `mods/grp` to use streamlined helper APIs (`site_ui_action_picker`, `site_ui_picker_state_to_json`, `site_ui_picker_state_from_json`, `site_ui_pick_view_collect_fd`), added `search_param` and `page_param` parameter flexibility to `hyle_bud_picker_desc_t` in `libhyle-bud`, and drafted future task `docs/future/hyle-picker-abstractions.md` for moving remaining picker view abstractions down into `hyle` / `libhyle-bud`.

## Why this matters
Currently, consumer modules (like `mods/gig`, `mods/song`, `mods/grp`) carry custom picker boilerplate, JSON serialization/deserialization logic, GET/POST form stitching, or query parameter parsing. Hiding this behind abstractions in `site_ui` and `libhyle-bud` reduces repetition, prevents bugs when adding new fields, and maintains clean module boundaries.

## Decisions made
- HTML is the interface; C is the only renderer; JS is dumb transport.
- No-JS is a working mode, not a fallback mode.
- Abstract picker logic away from module handlers into helper APIs in `mods/common/ux/site_ui.h` / `site_forms.c` and `libhyle-bud`:
  1. `site_ui_action_picker`: Standard standalone action picker component (combines GET sibling search form, POST action form, picker field, custom hidden inputs/prefs, custom headers/buttons).
  2. `site_ui_picker_state_to_json` / `site_ui_picker_state_from_json`: Helper functions for WASM / SSR picker state serialization and deserialization in `bud-state`.
  3. `site_ui_pick_view_collect_fd`: Helper for collecting pick view state from request `QUERY_STRING` in HTTP handlers.
  4. Extended `hyle_bud_picker_desc_t` in `libhyle-bud` with `search_param` and `page_param` so custom search field parameters (like `title` or `author` or `q`) are supported without restricting to `pick_q_`.
- Future work logged in `docs/future/hyle-picker-abstractions.md` to shift remaining picker view state and query parsing primitives directly down into `hyle` / `libhyle-bud`.

## Constraints & Rules
- No-JS must always work.
- `hyle` stays neutral (no component symbols in `external/hyle/src` or `include/hyle`).
- All row writes go through hyle `put`/`del` (`source_update_item` / `source_delete_item`).
- SSR markup is the contract.
- Module boundaries must pass (`sh scripts/check-module-boundaries.sh`).

## Files touched
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `mods/common/ux/site_ui.h`
- `mods/common/ux/site_forms.c`
- `mods/gig/gig.c`
- `mods/gig/ux/detail.c`
- `mods/gig/ux/song_picker.c`
- `mods/song/song.c`
- `docs/future/hyle-picker-abstractions.md`
- `docs/current/omni-dropdown.md`

## Research / findings (enough to continue without re-reading)
- `mods/gig/ux/song_picker.c` manually built sibling GET forms, POST action forms, and preference inputs. This is generalized as `site_ui_action_picker`.
- `mods/gig/gig.c` and `mods/gig/ux/detail.c` hand-rolled JSON creation and parsing for `pick_opts` and `pick_sel`. Replaced with `site_ui_picker_state_to_json` / `from_json`.
- `libhyle-bud` `picker.c` previously assumed `pick_q_<key>` and `pick_page_<key>`. Extended `hyle_bud_picker_desc_t` to accept `search_param` and `page_param` so site forms can search by any query key (e.g. `title` or `q`).

## Remaining work
- None for this task. Future architecture improvements tracked in `docs/future/hyle-picker-abstractions.md`.

## Open questions / risks
- None. Build, WASM checks, module boundary checks, and test suite pass cleanly.

## Next recommended step
Task is complete. To work on moving the core abstractions down into `hyle-bud`, start task `hyle-picker-abstractions` (`/task hyle-picker-abstractions`).
