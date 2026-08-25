# Task: hyle-picker-abstractions

## Original request
"That's better but it's still in the wrong place. hyle itself should provide the core abstractions needed. hyle-bud the component. The site provides omnisearch because it wants to, but theoretically, a site should be able to search by title, for example. This should not be restricted in hyle's side. Make a task to improve this."

## Goal
Move core picker search, query parsing, and state abstractions down into `hyle` / `libhyle-bud` so `hyle` provides neutral, site-agnostic picker primitives rather than hardcoding omnisearch (`pick_q_` / `pick_page_`) parameter conventions or site-specific glue in higher-level site code.

## Current Status
- [x] COMPLETED — Extended `hyle_bud_picker_desc_t` in `libhyle-bud` with `.search_param` and `.page_param` parameter flexibility, defined `hyle_bud_picker_view_t`, `hyle_bud_picker_entry_t`, and `hyle_bud_picker_buffer_t` in `hyle-bud.h`, implemented `hyle_bud_picker_state_to_json` and `hyle_bud_picker_state_from_json` in `libhyle-bud/src/picker.c`, unified `pick_view_t` and `hyle_bud_picker_view_t` struct layout, linked `-ljson-c` in `libhyle-bud/Makefile`, and verified all 97 E2E tests pass.

## Why this matters
Currently, `mods/index/pick.c` and `mods/common/ux/site_forms.c` contain picker query collection (`pick_view_collect`), picker state data structures (`pick_view_t`, `pick_entry_t`), and JSON serialization/deserialization logic. `libhyle-bud` is the neutral C HTML component library for `hyle`, so it should provide these core picker abstractions directly. Sites and consumer modules (`mods/gig`, `mods/song`, `mods/grp`) should consume native `hyle-bud` picker APIs without relying on custom site-specific picker collector code or hardcoded `pick_q_` query parameter constraints.

## Decisions made
- HTML is the interface; C is the renderer; JS is dumb transport.
- No-JS is a working mode, not a fallback mode.
- `hyle` stays neutral — `libhyle-bud` (the bud HTML component library for hyle) owns component rendering, picker state structures (`hyle_bud_picker_view_t`, `hyle_bud_picker_entry_t`, `hyle_bud_picker_buffer_t`), and JSON state helpers (`hyle_bud_picker_state_to_json`, `hyle_bud_picker_state_from_json`).
- `hyle_bud_picker_desc_t` supports custom `search_param` and `page_param` parameter names so site fields (e.g. `title=`, `author=`, `q=`) work natively with any picker without restricting to omnisearch (`pick_q_`).
- `pick_view_collect_fd` exported as an XY module API in `mods/index/index.h` so consumer modules load it cleanly through xylem without DAG violations in `common.so`.
- `pick_view_t`, `pick_entry_t`, and `site_ui_picker_buffer_t` in `site_ui.h` are typedef aliases of native `hyle_bud_picker_*` structures so WASM/SSR memory alignment is 100% unified.

## Constraints & Rules
- No-JS must always work.
- `hyle` core (`external/hyle/src` or `include/hyle`) stays neutral; `libhyle-bud` (`external/hyle/c/libhyle-bud/`) is the designated place for bud component abstractions.
- Module boundaries must pass (`sh scripts/check-module-boundaries.sh`).
- UX purity must pass (`sh scripts/check-ux-purity.sh`).

## Files touched
- `external/hyle/c/libhyle-bud/include/hyle-bud/hyle-bud.h`
- `external/hyle/c/libhyle-bud/src/picker.c`
- `external/hyle/c/libhyle-bud/Makefile`
- `mods/index/index.h`
- `mods/index/pick.c`
- `mods/common/ux/site_ui.h`
- `mods/common/ux/site_forms.c`
- `mods/gig/gig.c`
- `mods/gig/ux/detail.c`
- `mods/gig/ux/song_picker.c`
- `mods/song/song.c`
- `docs/current/hyle-picker-abstractions.md`

## Research / findings (enough to continue without re-reading)
- `hyle_bud_picker_desc_t` in `hyle-bud.h` has `.search_param` and `.page_param` fields.
- `libhyle-bud` `picker.c` checks `d->search_param` and `d->page_param`, defaulting to `pick_q_<key>` and `pick_page_<key>` when NULL.
- `hyle_bud_picker_state_to_json` and `hyle_bud_picker_state_from_json` now live natively in `libhyle-bud`.
- Struct alignment of `pick_view_t` vs `hyle_bud_picker_view_t` was synchronized so WASM/SSR state buffers match perfectly.
- All 97 Playwright E2E tests pass with zero failures.

## Remaining work
- None. Full build, boundary checks, UX purity, and 97/97 E2E tests verified.

## Open questions / risks
- None.

## Next recommended step
Task complete. Archive `docs/current/hyle-picker-abstractions.md` to `docs/archive/hyle-picker-abstractions.md`.

## Resume prompt
Resume task hyle-picker-abstractions. All work completed and 97/97 tests passing cleanly.
