# Task: In-place song replacement in gig detail (No full page reload in JS mode)

## Goal
When replacing a song in `/gig/:id` using the song title omni-dropdown in JS/WASM mode, perform the replacement in-place without triggering a full page reload. Keep No-JS SSR degradation strictly working.

## Current Status
Implementation complete and verified across all gig E2E tests.

## Decisions Made
1. **Endpoint & Contract**:
   - Updated `handle_sb_song_replace_authorized` in `mods/gig/gig.c` to inspect `Accept: application/json`.
   - When requested via JSON fetch, returns a `200 OK` JSON object containing `index`, `song_id`, `title`, `type`, `original_key`, `target_key`, `transpose`, `chord_html`, `yt`, `audio`, `pdf`.
   - For standard form POST (No-JS / non-JSON), continues to redirect 303 to `/gig/:id`.
2. **In-place DOM Updates (`htdocs/hyle-fragments.js`)**:
   - `submitPickerAction` intercepts `/song/*/replace` POST requests when triggered on auto-submit.
   - Performs `fetch(action, ...)` with `Accept: application/json`.
   - Updates the target row's summary title, values slot, chord data `<pre>`, media embeds (YouTube iframe / audio / pdf), target key badge, song view link, type badge, and transpose selector without triggering navigation.
   - Preserves fallback behavior.

## Constraints & Rules
- Isomorphic BUD rule respected: No structural discrepancies between SSR and WASM.
- C backend handles JSON generation with zero memory leaks.
- Zero leftover debug logs.

## Remaining work
- [x] Backend JSON replace endpoint support.
- [x] Client-side in-place row update handler in `hyle-fragments.js`.
- [x] E2E test verification (`tests/e2e/gig-replace.test.ts`).
- [x] All 15 gig E2E tests passing.

## Next recommended step
Run full `make test` for final verification.
