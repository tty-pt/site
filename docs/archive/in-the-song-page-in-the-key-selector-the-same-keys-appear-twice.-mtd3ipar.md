# Quest: in-the-song-page-in-the-key-selector-the-same-keys-appear-twice.

## Goal
Fix the song page key selector (and related key selectors) where keys appear twice due to iterating from -11 to +11 (23 options) instead of 12 distinct chromatic pitch offsets (0 to 11).

## Original request
> Goal: In the song page the key selector the same key appears twice.

## Parent Quest
> If this is a sub-quest, reference the parent quest here (e.g. [[parent-quest-name]]).

## Current Status
- [x] done

## Build & Run Commands
> Commands to build, run, and test the project (discovered BEFORE modifying feature code).
- Build: `make`
- Run: `./start.sh` (or `make watch`)
- Test: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/<file>.test.ts`
- Quality gate: `make test`

## TDD & Quality Checklist
- [x] **1. Discovery**: Discovered how to build, run, and test the project.
- [x] **2. Write Tests First**: Developed test(s) for the quest BEFORE feature code (`tests/e2e/song-ssr-chords.test.ts`).
- [x] **3. Feature Implementation**: Developed feature to satisfy tests (`mods/song/ux/detail.c`, `mods/gig/ux/detail.c`, `mods/gig/ux/edit.c`).
- [x] **4. Build & Run**: Built and ran project with zero build errors. Restart server/process to verify clean boot.
- [x] **5. Clean Code**: Verified code has zero debug artifacts or leftover logs.
- [x] **6. Server Restart & Full Test Suite**: Restarted fresh server instance and executed FULL test suite with zero errors (104 of 104 E2E tests, unit tests, pages tests, ASAN matrix tests).

## In-Depth Analysis & Findings
> Root cause analysis, architectural friction, abstraction opportunities.
- **Root Cause**: In `mods/song/ux/detail.c` (and similarly `mods/gig/ux/detail.c` & `mods/gig/ux/edit.c`), key options were rendered using a loop `for (int i = -11; i <= 11; i++)`. This created 23 option elements across 12 pitch classes, repeating every non-zero pitch class twice (e.g., `-11` and `+1` both represent +1 semitone; `-10` and `+2` both represent +2 semitones).
- **WASM Hydration & Patching**: `g_key_options` in `mods/song/ux/detail.c` was sized to 23 (`g_key_options[23]`) and indexed by `i + 11`. When updating upon WASM fetch / Latin toggle, it patched 23 options.
- **Normalization**: Transposition in `transp.c` (`transp_buffer`) normalizes semitone shift to `((semitones % 12) + 12) % 12` (0..11).
- **Resolution**: Sizing the option list to exactly 12 semitones (`0..11`), normalizing current transpose value when marking `selected`, and updating `g_key_options` to size 12. Also ensure `gig/ux` key selectors are aligned to 12 distinct options.

## Detailed Multi-Stage Execution Plan
> Each stage must be self-contained as if it were a single quest, with exact signatures, touched files, and targeted tests.
### Stage 1: Add Targeted Tests for Key Selector Uniqueness & Count
- **Target**: Create/update E2E test verifying that the song page key selector renders exactly 12 unique key options with no duplicate pitch names, and selecting keys updates the transposition.
- **Touched Files**: `tests/e2e/song-ssr-chords.test.ts`, `tests/e2e/song-detail-transpose.test.ts`
- **Targeted Tests**: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/song-ssr-chords.test.ts tests/e2e/song-detail-transpose.test.ts`

### Stage 2: Fix Key Selector in `mods/song/ux/detail.c`
- **Target**: Update `render_key_options`, `g_key_options`, and `wasm_fetch_callback` to iterate `0..11` (12 options) and normalize selected transpose index.
- **Touched Files**: `mods/song/ux/detail.c`
- **Targeted Tests**: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/song-ssr-chords.test.ts tests/e2e/song-detail-transpose.test.ts`

### Stage 3: Align Gig Key Selectors & Regression Verification
- **Target**: Update `mods/gig/ux/detail.c` and `mods/gig/ux/edit.c` to use 12 distinct options (0..11) so the same duplicate issue does not exist in gig views.
- **Touched Files**: `mods/gig/ux/detail.c`, `mods/gig/ux/edit.c`
- **Targeted Tests**: `AUTH_SKIP_CONFIRM=1 deno test --allow-all tests/e2e/gig-transpose.test.ts tests/e2e/gig-ssr-chords.test.ts tests/e2e/gig-edit.test.ts`

### Stage 4: Clean Boot, Quality Gates & Full Test Suite
- **Target**: Rebuild all WASM/native binaries, verify zero build warnings/errors, restart axil server, run full test suite `make test`.
- **Targeted Tests**: `make test`

## Acceptance Criteria & Polish Checklist
- [x] Song detail key selector contains exactly 12 `<option>` elements.
- [x] Every key appears exactly once in the song page key selector (no duplicate pitches).
- [x] Original key is marked with `(Original)` and selected by default.
- [x] Transposition works seamlessly in both SSR and WASM mode when choosing any of the 12 keys.
- [x] Toggling Latin notation updates all 12 key option labels properly in WASM.
- [x] Gig key selectors also render 12 distinct options.
- [x] Zero build warnings, zero test failures across full `make test`.

## Sub-Quests
> Sub-quests, follow-ups, or tangent quests spawned from this quest.
- [ ] None

## Quest Refinements & User Feedback Loops
> Mid-workflow refinements, post-implementation iterations, and user adjustments.
- Initial proposal and plan presented for user confirmation.

## Why this matters
> Context, motivation, stakeholders.
Musicians transposing songs on the song page are confused when every key appears twice with positive and negative semitone shifts. A clean 12-key dropdown provides an intuitive, standard transposition selector.

## Decisions made
- Use 12 options (0 to 11 semitones) for the key dropdown.
- Selected option matches `((transpose % 12) + 12) % 12` so any legacy query params or negative numbers resolve to the matching key.
- Update `g_key_options` array in `mods/song/ux/detail.c` from 23 to 12 elements.

## Constraints & Rules
- Isomorphic C / WASM compliance: no compile-time `#ifdef __wasm__` in UX DOM branches.
- No site-specific JavaScript.
- All WASM imports must remain pure and pass `scripts/check-wasm-imports.sh`.

## Files touched
- `mods/song/ux/detail.c`
- `mods/gig/ux/detail.c`
- `mods/gig/ux/edit.c`
- `tests/e2e/song-ssr-chords.test.ts`
- `docs/current/in-the-song-page-in-the-key-selector-the-same-keys-appear-twice..md`

## Remaining work
- [x] Write targeted test verifying 12 unique options
- [x] Implement 12-option key selector in `mods/song/ux/detail.c`
- [x] Update `mods/gig/ux/detail.c` and `mods/gig/ux/edit.c`
- [x] Build & run targeted tests
- [x] Run full test suite & final verification

## Next recommended step
1. Prompt user for wrap-up flow via `ask_questions`.

## Resume prompt
> All stages complete and quality gates passed. Follow wrap-up flow.
