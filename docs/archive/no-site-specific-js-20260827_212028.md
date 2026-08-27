# Task: Generic-Only JS & Zero Site-Specific JavaScript Convention

Status: **in_progress**

## Goal & Summary
Establish and enforce the invariant that **no site-specific JavaScript code exists in the codebase**. All custom domain/application behaviors must be implemented in pure C (SSR + isomorphic WASM). JavaScript files (`htdocs/*.js`) must remain strictly generic, framework-level transports (for `hyle`, `bud`, and generic progressive enhancement) driven purely by neutral `data-hyle-*` / `data-bud-*` attributes and standard HTML semantics.

## Architectural Audit & Findings
1. **Audit of `htdocs/*.js`**:
   - `htdocs/bud-client.js`: Completely generic WASM loader and host import bridge. Zero site-specific strings.
   - `htdocs/bud-hydrate.js`: Completely generic Bud DOM reconciler and virtual node hydration engine. Zero site-specific strings.
   - `htdocs/hyle-fragments.js`: Contained a site-specific `/song/.../replace` handler and `.querySelector('[data-gig-item]')`, `[data-gig-chord-data]`, `[data-gig-media]`, `[data-gig-target-key]` patching code.
2. **Analysis of Progressive Action Picker Behavior**:
   - The generic transport contract for action pickers (omni-dropdowns / action forms marked with `data-hyle-auto-submit="1"`) is:
     - On selecting an option (radio input change or enter key), automatically submit the surrounding `<form>` via `f.requestSubmit()`.
     - Forms handle their submission normally (POST with 303 redirect in SSR/progressive enhancement, or WASM event handling in rich client mode).
   - In pure SSR/progressive enhancement without JS-customized AJAX payloads, standard form submission updates the server state and redirects.
   - Removing the bespoke `data-gig-*` patch block from `hyle-fragments.js` cleans the transport layer entirely and makes `submitPickerAction` perform `f.requestSubmit()` for all `data-hyle-auto-submit` forms.
3. **Automated Verification & CI Boundary Check**:
   - Create `scripts/check-no-site-specific-js.sh` to enforce zero occurrences of domain keywords (`gig`, `song`, `poem`, `grp`, `repertoire`, `transpose`, `sb-`, `chord`) in `htdocs/*.js`.
   - Wire `scripts/check-no-site-specific-js.sh` into `boundary-check` in `Makefile`.

## Multi-Stage Implementation Plan

### Stage 1: Purify `htdocs/hyle-fragments.js`
- Remove all site-specific branching, `/song/`, `/replace`, `data-gig-*` queries, and hardcoded domain logic from `htdocs/hyle-fragments.js`.
- Make `submitPickerAction` generic:
  ```js
  function submitPickerAction(input) {
      var f = input.closest('form');
      if (!f) return;
      if (f.requestSubmit) f.requestSubmit();
      else f.submit();
  }
  ```
- Retain generic click-outside, Escape key, keyboard navigation, and pagination behaviors driven by `.hyle-*` and `[data-hyle-*]`.

### Stage 2: Automated Boundary Check & CI Integration
- Create `scripts/check-no-site-specific-js.sh` with executable permissions (`chmod +x`).
- Add `sh scripts/check-no-site-specific-js.sh` to `boundary-check` target in `Makefile`.
- Verify the script catches any forbidden keywords and passes cleanly on purified files.

### Stage 3: Update Architecture and Convention Documentation
- Update `docs/CONVENTIONS.md`, `docs/SSR-CONTRACT.md`, `docs/VIOLATIONS.md` (or relevant docs), and `AGENTS.md` to document the "Generic-Only JS" invariant.

### Stage 4: Verification & Quality Gates
- Update any e2e test assertions that expected custom JS-level DOM hijacking instead of standard progressive form submission.
- Run targeted tests:
  - `tests/e2e/gig-replace.test.ts`
  - `tests/e2e/picker-omni.test.ts`
  - `tests/e2e/picker-nojs.test.ts`
  - `tests/e2e/gig-edit.test.ts`
- Run full test suite: `make test`.

## Acceptance Criteria & Polish Checklist
- [x] `htdocs/hyle-fragments.js` contains 0 site-specific terms or selectors (`gig`, `song`, `poem`, `grp`, `repertoire`, `transpose`, `sb-`, `chord`).
- [x] `scripts/check-no-site-specific-js.sh` exists and passes in `make boundary-check`.
- [x] All action pickers and dropdowns continue working seamlessly across SSR, no-JS, and WASM modes.
- [x] Documentation (`docs/CONVENTIONS.md`, `AGENTS.md`) updated to reflect the pure generic JS transport rule.
- [x] Zero build warnings, zero test regressions across the full `make test` suite (99/99 E2E, unit tests, ASAN, boundary checks).

## Current Status
Completed all stages:
1. `htdocs/hyle-fragments.js` purified of all site-specific branching and selectors.
2. `scripts/check-no-site-specific-js.sh` created and integrated into `boundary-check` in `Makefile`.
3. Architecture documentation updated in `docs/CONVENTIONS.md` and `AGENTS.md`.
4. Full verification passing with 100% success across all unit, integration, and E2E tests.

## Next Recommended Step
- Prompt user for task wrap-up flow options.
