# Future Task: Multi-Omni-Dropdown

## Status: PROPOSED — brainstorm completed 2026-08-25, ready to promote when prioritised.
## Goal & Scope

Enable **multiple independent omni-dropdown (omni-search) picker instances on
the same page**, each scoped to a specific list item (e.g. one inline
song-replace picker per gig song row), while preserving full SSR compat and
no-JS fallback.

The motivating example: instead of a single page-level "Replace Song" flow that
redirects to `?replace=N`, show a compact inline picker **directly in each song
row** of the gig detail page. Clicking the replace icon expands a small
`<details>` right inside that row.

---

## The Problem Today

`pick_view_t` is a single flat struct (`hyle_bud_picker_view_t`) with up to
`HYLE_BUD_PICKER_MAX_FIELDS` (8) entries, each indexed by the ref-field key
(e.g. `"song_id"`). The picker state (query `pick_q_song_id`, page
`pick_page_song_id`) is global to the page — there is only one sibling GET
form, one set of URL params, and one SSR picker panel per key.

Multiple pickers for the **same dataset** (e.g. N song-replace dropdowns, one
per gig song row) all share the same query params — so only one can be open /
searched at a time in the no-JS path. In the WASM path the single
`g_sb_pick_state` tracks one window of results.

---

## Proposed Solution: Scoped Picker Keys

### Core idea

Discriminate pickers by a **compound key** rather than just the dataset key.
Instead of `pick_q_song_id`, use `pick_q_song_id__<scope>` where `<scope>` is
a short string (e.g. the row index `"0"`, `"1"`, `"2"` …).

```
pick_q_song_id__0=&pick_page_song_id__0=0   ← picker for row 0
pick_q_song_id__2=bach&pick_page_song_id__2=0  ← picker for row 2 (being searched)
```

The **sibling GET form** includes only the params for the row it belongs to;
all other rows reload with empty/default state (acceptable — same degradation
as today).

### SSR path (no-JS)

Each song row renders its own `<details class="hyle-multiselect">` + sibling
`<form method="GET" id="pickq-song_id__N">`.  
Submitting that form does a full reload with `?replace=N&pick_q_song_id__N=…`
— one round-trip per picker interaction, same as today for the single picker.

The server handler (`gig_detail_auth`) reads `replace=N` from the query, then
calls `pick_view_collect_fd` for **only** the active row's scoped key.  
Other rows render with empty picker state (collapsed `<details>`, no results
listed) — which is fine since the user is focused on one at a time.

### WASM path

In the enhanced path we can be smarter: each row manages its own
`site_ui_picker_buffer_t` + `pick_view_t` locally (via fetch from
`/pick/song.items/options?q=…`), so all N pickers work independently and
simultaneously. The `data-hyle-ms` JS already does this for multi-select
dropdowns; we extend the same scoping to the action-picker widget.

For SSR→WASM hydration we ship picker state only for the **active** row (the
one matching `?replace=N`) inside the `bud-state` JSON. The WASM bridge
re-initialises the other pickers lazily on first open.

### State schema additions

```c
// In sb_app_state_t (or a parallel per-row array):
int active_picker_row;    // -1 = none; matches ?replace=N
```

The per-row picker buffer is allocated as a small fixed array:
```c
static site_ui_picker_buffer_t g_row_pick_bufs[MAX_SB_SONGS];
static pick_view_t             g_row_pick_states[MAX_SB_SONGS];
```

`wasm_init` initialises only `g_row_pick_states[active_picker_row]` from the
shipped JSON; others start empty.

---

## SSR Rendering Contract Changes

### New HTML structure per song row

```html
<!-- song row wrapper -->
<div data-gig-item="" data-gig-row="2">
  <!-- ...title, chords, media... -->

  <!-- owner-only inline replace picker -->
  <details class="hyle-ms-field" data-gig-replace="2">
    <summary class="btn text-xs">🔄 Replace</summary>
    <div class="hyle-ms-panel" data-hyle-ms-panel>
      <!-- sibling GET form for no-JS search/paging of THIS row -->
      <form method="GET" id="pickq-song_id__2" class="pick-sibling-form">
        <input type="hidden" name="replace" value="2">
        <input name="pick_q_song_id__2" type="search" ...>
        <input name="pick_page_song_id__2" type="hidden" value="0">
        <button type="submit">Search</button>
      </form>
      <!-- picker options (radio buttons POSTing to /api/gig/:id/song/2/replace) -->
      <form method="POST" action="/api/gig/:id/song/2/replace" id="sb-pick-post-2">
        <input type="hidden" name="csrf_token" value="...">
        <input type="hidden" name="n" value="2">
        <div class="hyle-ms-options" data-hyle-slot="rows">
          <label><input type="radio" name="song_id" value="abc123"> Song Title</label>
          ...
        </div>
        <button type="submit">Replace</button>
      </form>
    </div>
  </details>
</div>
```

Key invariants:
- Each sibling GET form carries `name="replace" value="N"` so the server knows
  which row is active after reload.
- Each POST form has a unique `id` (`sb-pick-post-<N>`) but the same `action`
  pattern.
- The radio `name="song_id"` submits exactly one value — native, zero JS.
- The `<details>` collapses without JS; CSS can auto-close others via `:not([open])`.

---

## Implementation Plan (when promoted to active)

### Phase 1 — `pick_view_collect` scoping

1. Add a `scope` parameter (nullable `const char *`) to `pick_view_collect_fd`.
   When non-null, it appends `__<scope>` to the query param names it reads and
   the URL names it emits.
2. Keep the existing signature as a wrapper with `scope=NULL` for all current
   callers (zero churn).

### Phase 2 — Renderer helpers

1. Add `sb_render_inline_replace_picker(int row_idx, pick_view_t *pv)` inside
   `song_picker.c` — emits the `<details>` + both forms.
2. The sibling form helper (`site_ui_sibling_get_form` or a new
   `site_ui_sibling_get_form_scoped`) passes `scope = row_idx_str` down.
3. `sb_build_body_content` calls the inline picker for each row instead of
   rendering the page-level picker when `replace_index == -1`.

### Phase 3 — Server handler

1. `gig_detail_auth`: loop `N` times only over the **active** row's scoped
   `pick_view_collect_fd`; keep `active_picker_row` in state.
2. Serialize only the active row's `pick_view_t` into `bud-state` JSON
   (keyed `"song_id__<N>"`).

### Phase 4 — WASM bridge

1. `wasm_init`: load `g_row_pick_states[active]` from JSON; all others are
   zero-initialised (lazy fetch on first open).
2. On `<details>` toggle event: fetch `/pick/song.items/options?q=&pick_q_song_id__N=`
   and populate `g_row_pick_states[N]`; re-render that row's picker panel via
   `bud_patch_innerhtml`.

---

## Constraints & Rules

- No `XY_`/`xy_`/`qmap_`/`source_`/`axil_` in UX code (boundary checker).
- No `#if`/`#ifdef` for runtime branching in UX.
- Sibling GET form per-row; POST form stays inside the row (native submit).
- `pick_view_t` entries limit is `HYLE_BUD_PICKER_MAX_FIELDS=8`; per-row bufs
  use 1 entry each, so 8 rows could share one `pick_view_t` — or each row gets
  its own (recommended: own, since they're stack/static).
- SSR degradation: only the active row's picker has populated results; all
  others are empty `<details>` with the sibling form — acceptable.
- No-JS must always work (SSR-CONTRACT §1–3).

---

## Open Questions / Risks

1. **URL length**: `N` rows × 2 params = `pick_q_song_id__0…pick_q_song_id__N`.
   Only the active one is ever non-empty, but all appear in the sibling form.
   Fine in practice (gig rarely exceeds ~20 songs).
2. **`HYLE_BUD_PICKER_MAX_FIELDS` limit**: `pick_view_t.entries` has 8 slots.
   With scoped keys all distinct from each other, the single `pick_view_collect`
   call for the active row uses only 1 slot — no issue.
3. **CSS**: Multiple open `<details>` simultaneously. Could close-others on open
   via vanilla JS or accept them all open (they're in separate rows, fine UX).
4. **Scope parameter API**: `const char *scope` in `pick_view_collect_fd` is a
   minor ABI extension — all callers are internal, no public ABI to break.
5. **Hydration alignment**: SSR emits N `<details>` nodes; WASM renders exactly
   the same N nodes in `sb_build_body_content`. Node-id alignment is preserved
   because the tree structure is driven by `sb_app_state.n_songs` (same both
   sides) and picker state is additive inside the pre-existing `<details>`.
