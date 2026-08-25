# Task: Fix Picker & Form Regressions

## Goal
Diagnose and resolve the 6 failing tests introduced in the recent commits:
1. `auto-repertoire: pickers derive + row-click add (grp & gig) => ./tests/e2e/auto-repertoire.test.ts:21:6`
2. `gig detail: add and remove songs via API => ./tests/e2e/gig-edit-row.test.ts:20:6`
3. `song type: multiple types detail shows both display names => ./tests/e2e/song-type.test.ts:300:6`
4. `song type: on-disk type file uses newline separators => ./tests/e2e/song-type.test.ts:648:6`
5. `song type: replace type via edit persists and is filterable => ./tests/e2e/song-type.test.ts:711:6`
6. `song type: add second type via edit persists and both filterable => ./tests/e2e/song-type.test.ts:852:6`

## Decisions made
- Fixed `gig-edit-row.test.ts` and `auto-repertoire.test.ts` by aligning test click interactions with auto-submitting selection behavior (`Promise.all([page.waitForNavigation(), opt.first().click()])`).
- `song.types` in `mods/song/song.c` was previously initialized with `SOURCE_FLAG_VOLATILE`, which bypassed `scan()` on `var/song.types` on boot. Changed flags to 0 so type fixtures persist and scan correctly on startup.
- Added ASCII case-insensitive comparison helper `ascii_strcasecmp` and `ascii_strncasecmp` in `mods/common/ux/site_forms.c` for `pick_is_selected_opt` and `pick_val_has_token`, matching display names and slugs reliably.
- Cleaned up preprocessor directives `#ifndef __wasm__` in `mods/common/ux/site_forms.c` so `check-ux-purity.sh` passes with 0 warnings.
- Fixed budget cutoff in `mods/index/pick.c` to reject oversized query strings (>2048 bytes) and use adequate environment buffer size.
- Updated `tests/e2e/song-type.test.ts` to properly locate and await dynamic search results in omni dropdown pickers.

## Current Status
- [x] Task initialized
- [x] Research failure causes for song-type tests
- [x] Research failure causes for auto-repertoire & gig-edit-row tests
- [x] Fix song-type regressions
- [x] Fix gig / auto-repertoire regressions
- [x] Run targeted tests & verify
- [ ] Run full test suite & ensure zero errors

## TDD & Quality Checklist
- [x] Run targeted tests during iteration
- [x] Zero build warnings/errors
- [x] Check scripts pass (module boundaries, UX purity, WASM imports)
- [ ] Full make test passes
