# Task: Generic-Only JS & Zero Site-Specific JavaScript Convention

## Goal & Summary

Establish and enforce the invariant that **no site-specific JavaScript code exists in the codebase**. All custom domain/application behaviors must be implemented in pure C (SSR + isomorphic WASM). JavaScript files (`htdocs/*.js`) must remain strictly generic, framework-level transports (for `hyle`, `bud`, and generic progressive enhancement) driven purely by neutral `data-hyle-*` / `data-bud-*` attributes and standard HTML semantics.

---

## Why This Matters

1. **Stack Identity**: The project is a pure-C music application (`poem`, `song`, `gig`, `grp`) on `axil`, `libxylem`, and `bud`, designed with SSR-first architecture and progressive WASM enhancement.
2. **Framework Separation & Portability**: `hyle-fragments.js`, `bud-client.js`, and `bud-hydrate.js` are generic infrastructure components (part of the external `hyle` and `bud` ecosystems). Embedding domain concepts (e.g. `gig-song-title-picker`, `songbook`, `grp`) into generic JS transports breaks module encapsulation, violates the framework-neutral SSR contract, and prevents reuse.
3. **Architectural Purity**: All site-specific reactive logic, data manipulation, event handling, and interactive behaviors belong in `mods/*/ux/*.c` compiled to native `.so` (SSR) and WASM (`htdocs/*.wasm`).

---

## Core Invariants & Rules

1. **Zero Site-Specific JS Files**:
   - No `.js` files may contain site-specific selectors (e.g. `.gig-*`, `.song-*`, `#sb-*`, `.poem-*`), domain endpoints, or business logic.
   - The only permitted JavaScript files in `htdocs/` are generic library runtime files:
     - `bud-client.js` (Bud WASM loader & runtime host bindings)
     - `bud-hydrate.js` (Bud isomorphic DOM hydration engine)
     - `hyle-fragments.js` (generic transport for `[data-hyle-frag-url]` slot swapping and progressive infinite scroll)

2. **Generic Attribute Contracts Only**:
   - If a generic transport like `hyle-fragments.js` needs to support a behavior (such as auto-submitting a single-select action form on choice), it must be driven exclusively by generic data attributes (e.g. `data-hyle-picker-auto-submit="1"`, `data-hyle-frag-url`, `data-hyle-slot`).
   - Generic JS must never query or test domain-specific class names, IDs, or URL paths.

3. **Domain Interactivity Belongs in WASM**:
   - Application-specific client logic (transposition calculations, zoom slider DOM patching, audio/media toggles, songbook state management) must be written in pure C in `mods/*/ux/*.c` and compiled to WASM.

4. **CI & Static Analysis Enforcement**:
   - Add an automated linter/check script (`scripts/check-no-site-specific-js.sh`) to `make test` / `make check` that scans `htdocs/*.js` and fails if any domain keywords, module names (`poem`, `song`, `gig`, `grp`), or non-generic selectors are detected.

---

## Audit of Existing JavaScript

### 1. `htdocs/hyle-fragments.js`
- **Current violation**: References `.gig-song-title-picker` for auto-submitting on radio change.
- **Fix**: Make auto-submit generic via `[data-hyle-auto-submit]` or `[data-hyle-picker-submit="auto"]` on the picker root element. Remove all `.gig-*` selector references.
- **Click-outside and Escape**: Ensure click-outside and Escape handlers target only generic `details.hyle-picker-details[open]`, `details.hyle-multiselect[open]`, and `details.hyle-singleselect[open]` elements.

### 2. `htdocs/bud-client.js` & `htdocs/bud-hydrate.js`
- Confirm they contain zero domain-specific hooks or module hardcoding.

---

## Implementation Plan

1. **Phase 1 — Purify `htdocs/hyle-fragments.js`**:
   - Replace `.gig-song-title-picker` check with generic `[data-hyle-auto-submit]` attribute check.
   - Update `mods/gig/ux/detail.c` / `song_picker.c` to emit `data-hyle-auto-submit="1"` on action pickers that require auto-submit.
   - Verify `hyle-fragments.js` has zero references to domain concepts.

2. **Phase 2 — Documentation & Conventions**:
   - Update `docs/CONVENTIONS.md`: Add a prominent "No Site-Specific JavaScript" section.
   - Update `docs/SSR-CONTRACT.md`: Reaffirm that JS layer is generic transport only; custom client behavior is WASM-driven.
   - Update `AGENTS.md`: Add the rule to the "Guidelines — read this, 2 minutes" index.

3. **Phase 3 — Automated Boundary Check**:
   - Create `scripts/check-no-site-specific-js.sh` to grep `htdocs/*.js` against a forbidden domain list (`gig`, `song`, `poem`, `grp`, `repertoire`, `transpose`, `sb-`, `chords`).
   - Wire `scripts/check-no-site-specific-js.sh` into `make all:boundary-check` and `make test`.

4. **Phase 4 — Verification**:
   - Run targeted e2e tests (`gig-replace`, `gig-edit`, `picker-omni`, `picker-nojs`).
   - Run full `make test` to ensure 100% green build and quality gates.

---

## Open Questions & Risks

- None. Making `auto-submit` generic via `data-hyle-auto-submit="1"` keeps `hyle-fragments.js` completely generic and reusable across any future dataset or form without JS modifications.
