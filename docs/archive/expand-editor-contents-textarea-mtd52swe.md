# Quest: expand-editor-contents-textarea

## Goal
1. When editing a poem, or a song, the contents textbox should be big. Should fit the content area, horizontally. When adding a song, likewise.
2. Fix the quest system naming bug: when prompting or receiving a description, the quest system should not name the quest with the entire raw description sentence as a long unwieldy slug.

## Original request
> When editing a poem, or a song, the contents textbox should be big. Should fit the content area, horizontally. When adding a song, likewise.
> The quest system is still naming the quest with what I put in when it asks me for the description. It's wrong. Make that part of the current quest.

## Current Status
- [x] done

## Build & Run Commands
- Build: `make`
- Run: `make watch` or `./start.sh`
- Test: `make test` / targeted tests

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build and run the project.
- [x] **2. Write Tests First**: Developed `tests/e2e/editor-textarea-dimensions.test.ts` and unit tests before finalizing feature code.
- [x] **3. Feature Implementation**: Developed feature to satisfy tests.
- [x] **4. Build & Run**: Built and ran project with zero build errors.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed FULL test suite with 105 passed / 0 failed.

## In-Depth Analysis & Findings
1. **Quest System Naming Fix (`.pi/extensions/quest-journal.ts`)**:
   - In `promptForQuestChoice`, "New quest…" now explicitly prompts for a short quest name/slug (e.g. `expand-editor-textarea`) and then optionally for the goal description.
   - When a user passes natural language arguments or command input, `slugify(..., 45)` extracts a concise, sensible slug instead of transforming the entire sentence into an unwieldy filename.
2. **Poem Edit Form Generator Fixes (`mods/common/ux/site_forms.c` & `mods/index/index.c`)**:
   - In `site_forms.c`, `body_content` and text content files render as a `<textarea>` instead of `<input type="file">`.
   - The label for `body_content` is formatted as `"Content:"`.
   - In `index_generic_edit_auth`, `BUD_QM_VSTR` content (e.g., `pt_PT.html`) is slurped and passed to `site_ui_form_from_desc` to pre-populate existing poem content on edit.
3. **Textarea Height & Width (`htdocs/styles.css`)**:
   - `<textarea>` styled with `width: 100%`, `min-height: 20rem`, `box-sizing: border-box`, spanning the full horizontal content width and providing a spacious editing area for chord charts, lyrics, and poem verses.

## Decisions made
- Treat text/HTML files in form schemas as textareas (`type = 1`), preserving file inputs only for binary files (e.g. images, audio).
- Set `<textarea>` to `width: 100%; min-height: 20rem;` in `htdocs/styles.css` so all content editing textareas (Song chords/lyrics, Poem content, Group format) are roomy and fit the full content width.
- Enforce concise slug generation (max 45 chars, word-boundary bounded) in `.pi/extensions/quest-journal.ts` and prompt for short name/slug first during interactive quest creation.

## Files touched
- `.pi/extensions/quest-journal.ts`
- `htdocs/styles.css`
- `mods/common/ux/site_forms.c`
- `mods/index/index.c`
- `tests/e2e/editor-textarea-dimensions.test.ts`
- `tests/e2e/poem-edit.test.ts`
- `tests/e2e/content-security.test.ts`
- `docs/current/expand-editor-contents-textarea.md`

## Remaining work
- [x] None. All tests passing and requirements satisfied.

## Next recommended step
1. Choose an action via the prompt below: archive with compaction, archive without compaction, or continue refining.
