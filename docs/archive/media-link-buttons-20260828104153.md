# Task: Media Link Buttons (`media-link-buttons`)

## Goal
Replace embedded YouTube `<iframe>` with a small icon link button redirecting to the corresponding YouTube link (`https://www.youtube.com/watch?v=<id>`) with `target="_blank" rel="noopener noreferrer"`. Standardize the PDF link to also be a small document icon button (`📄`) with `target="_blank" rel="noopener noreferrer"`. Display both icon buttons in a neat row aligned to flex-end next to the title and types in both `song` and `gig` modules. Remove the obsolete "Video" checkbox.

## Current Status
- Fully implemented, styled, and verified.
- YouTube and PDF icon buttons are aligned to flex-end next to the title and types.
- The obsolete "Video" checkbox has been removed from both song and gig viewer control menus.
- All unit, smoke, and targeted E2E tests pass with zero failures.

## Architectural & Code Findings
1. In `mods/common/ux/site_media.c`:
   - `site_ui_render_media_slot()` renders `.media-buttons flex flex-row items-center justify-end gap-2` containing:
     - YouTube button: `<a href="https://www.youtube.com/watch?v=%.11s" target="_blank" rel="noopener noreferrer" class="btn btn-icon btn-sm" title="Watch on YouTube" aria-label="Watch on YouTube">▶</a>`.
     - PDF button: `<a href="%s" target="_blank" rel="noopener noreferrer" class="btn btn-icon btn-sm" title="View PDF" aria-label="View PDF">📄</a>`.
   - `site_ui_build_media_html()` generates matching HTML for WASM innerHTML hydration.
2. In `mods/song/ux/detail.c`:
   - Positioned the types and author on the left and the media button row on the right (`flex justify-end items-center gap-2 flex-shrink-0 ml-auto`) above the chord content.
   - Removed the "Video" (`m`) checkbox from the transposition form.
   - Media buttons are always rendered whenever media exists, so they remain permanently available in both owner/edit and visitor views.
3. In `mods/gig/ux/detail.c`:
   - Positioned `media_node` in the row header next to the title/types before the owner action buttons.
   - Removed the "Video" (`m`) checkbox from gig viewer options.
   - Media buttons are always rendered whenever media exists.
4. In `htdocs/styles.css`:
   - Added styles for `.btn-sm`, `.btn-icon`, `.media-buttons`, and `.justify-end`.

## Decisions made
- UTF-8 `▶` (`\xe2\x96\xb6`) for YouTube and `📄` (`\xf0\x9f\x93\x84`) for PDF.
- Both buttons open in external tabs via `target="_blank"` with `rel="noopener noreferrer"`.
- Buttons are aligned to flex-end next to the title and types in both song and gig modules.
- Removed the "Video" toggle checkbox since small link buttons are non-intrusive and should always be accessible.

## Files touched
- `docs/current/media-link-buttons.md`
- `mods/common/ux/site_media.c`
- `mods/song/ux/detail.c`
- `mods/gig/ux/detail.c`
- `htdocs/styles.css`
- `tests/e2e/song-media.test.ts`
- `tests/e2e/gig-media.test.ts`
- `tests/e2e/content-security.test.ts`
- `tests/e2e/gig-zoom.test.ts`

## Remaining work
- None.
