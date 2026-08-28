# Task: Media Link Buttons (`media-link-buttons`)

## Goal
Replace embedded YouTube `<iframe>` with a small icon link button redirecting to the corresponding YouTube link (`https://www.youtube.com/watch?v=<id>`) with `target="_blank" rel="noopener noreferrer"`. Standardize the PDF link to also be a small document icon button (`📄`) with `target="_blank" rel="noopener noreferrer"`. Display both icon buttons in a neat, compact row in both `song` and `gig` modules.

## Current Status
- Fully implemented and verified across SSR and WASM.
- All unit, smoke, and 101/101 E2E tests pass with zero failures.

## Why this matters
YouTube iframes were heavy, slow to load, occupied significant vertical space on song and gig pages, and raised privacy concerns. Small icon link buttons (play icon for YouTube, document icon for PDF) arranged in a row provide a compact, lightweight, accessible, and consistent UX across both `song` and `gig` modules.

## Architectural & Code Findings
1. In `mods/common/ux/site_media.c`:
   - Updated `site_ui_render_media_slot()` to render a `.media-buttons` flex row containing:
     - YouTube button: `<a href="https://www.youtube.com/watch?v=%.11s" target="_blank" rel="noopener noreferrer" class="btn btn-icon btn-sm" title="Watch on YouTube" aria-label="Watch on YouTube">▶</a>` (UTF-8 `\xe2\x96\xb6`).
     - PDF button: `<a href="%s" target="_blank" rel="noopener noreferrer" class="btn btn-icon btn-sm" title="View PDF" aria-label="View PDF">📄</a>` (UTF-8 `\xf0\x9f\x93\x84`).
   - Updated `site_ui_build_media_html()` to generate matching HTML for WASM innerHTML hydration / toggle updates.
2. In `htdocs/styles.css`:
   - Added styles for `.btn-sm`, `.btn-icon`, and `.media-buttons`.
3. In `tests/`:
   - Updated `tests/e2e/song-media.test.ts` to verify YouTube and PDF link buttons (and assert no iframes).
   - Updated `tests/e2e/content-security.test.ts` to verify YouTube link buttons and invalid ID handling.
   - Added `tests/e2e/gig-media.test.ts` to verify media button row rendering in gig pages.

## Decisions made
- Used UTF-8 `▶` (`\xe2\x96\xb6`) for YouTube and `📄` (`\xf0\x9f\x93\x84`) for PDF.
- Both buttons open in external tabs via `target="_blank"` with `rel="noopener noreferrer"`.
- Buttons are styled with `.btn .btn-icon .btn-sm` inside a `.media-buttons` row (`flex flex-row items-center gap-2`).
- Both `song` and `gig` modules leverage the shared `site_media.c` functions for complete isomorphism across SSR and WASM.

## Files touched
- `docs/current/media-link-buttons.md`
- `mods/common/ux/site_media.c`
- `htdocs/styles.css`
- `tests/e2e/song-media.test.ts`
- `tests/e2e/content-security.test.ts`
- `tests/e2e/gig-media.test.ts`

## Remaining work
- None.
