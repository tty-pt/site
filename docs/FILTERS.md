# Filters — multi-ref and field-filter semantics

Everything an agent needs to know before touching list filtering. Covers storage,
query parsing, the pre-filter pipeline, the multi-ref union/intersect semantics,
and the widget wire format.

## 1. Storage of field values

- Scalar fields: `"<row_id>:<field>"` key in the fields registry qmap.
- **Multi-ref fields** (`HYLE_FIELD_MULTI_REFERENCE` /
  `SOURCE_FIELD_MULTI_REFERENCE`, type 5): the value is **newline-separated**
  position indices (typed-record sources) or slugs. Example:
  `"12\n34\n"` — positions into the target source's row map.
- Position ↔ slug: `qmap_pos(target->fields_hd, slug)` resolves a slug to its
  position; `qmap_get_key(target->fields_hd, pos)` resolves the reverse. When
  slug→pos fails, the raw slug is used as the token.

## 2. Query string → filters (C parser)

`hyle_parse_query` (`external/hyle/src/query.c`):

- Splits on `&`/`=`, url-decodes.
- Special keys: `sort`, `page`, `per_page`, `q`, `include`.
- **Everything else is a field filter** — and **each repeated `key=val` becomes
  its OWN filter entry** (`out->filters[]`). The C parser does NOT join repeated
  keys. (The Rust crate `parse_query_params` DOES join repeated keys with commas
  — keep the asymmetry in mind when touching filter semantics.)

So `?type=comunhao&type=natal` produces two filters, both on field `type`.

## 3. The pre-filter pipeline (`hyle_source_query`)

Order matters (`external/hyle/src/source.c`):

1. `prefilter_fts` (source.c:315) — for searchable fields, resolves each filter
   against the stoma index, **intersects** the matching row sets. Rebuilds the
   index lazily after mutations.
2. `prefilter_multi_ref` (source.c:217) — for multi-ref fields, resolves
   slug→position and pre-filters rows. Runs with
   `base_hd = fts_hd ? fts_hd : e->row_hd` (i.e. FTS narrowing is preserved).
3. `hyle_apply_view` — the residual filter/sort on the pre-filtered row set.
   Handled filters have their `value` set to NULL so apply_view doesn't
   double-filter.

## 4. The multi-ref bug and the required semantics (IMPORTANT)

`prefilter_multi_ref` currently iterates filters and for EACH one REPLACES the
running pre-filtered set with the new single-value set (`if (pre_hd)
qmap_close(pre_hd); pre_hd = new_hd;`). With repeated keys only the **last**
value wins — multi-select is broken.

Required semantics (framework-neutral, hyle core):

- **Union within a field**: rows matching ANY of the selected values for the
  same field.
- **Intersect across fields**: rows matching field A AND field B.
- Single-value behavior must remain byte-identical to today.

Algorithm sketch: group filters by field name; for each distinct field build a
union map over `base_hd`; then fold the per-field union maps together by
intersection. Remember to `qmap_close` intermediate maps and to NULL the filter
values that were handled.

## 5. Server-side handling of filter values

`source_query` (`mods/source/source.c:1046`):

- `hyle_parse_query`, then for each filter whose field is multi-ref, the value
  is **slugified via `axil_slugify`** (`source.c:1076-1086`).
- Gotcha: `axil_slugify("a,b")` → `"a_b"` — **commas do not survive slugify**.
  Therefore the wire format for multi-select must use **repeated keys**
  (`type=a&type=b`), NOT comma-joined values. Do not "optimize" to a
  comma-joined single param unless you also split before slugify.

## 6. SSR checked-state round-trip

- `idx_query_param(qs, name, buf, len)` (`mods/index/ux/list.c:38`) returns only
  the FIRST occurrence of a repeated key — fine for `sort`/`page`/`per_page`,
  wrong for multi-ref selections.
- Add/use `idx_query_params_join`: collect ALL occurrences of `name`, url-decode
  each, join with `,` for display/checked-state. The SSR widget then
  comma-splits that value to mark checkboxes checked (same split logic as
  `hyle_bud_checkbox_fieldset`).
- Keep `idx_query_param` for single-value params.

## 7. Filter UI options

- `idx_resolve_filter_options(target_source, target_hd, opts, max)`
  (`mods/index/ux/list.c:118`): builds `hyle_bud_option_t {id,label}[]` — id =
  row_id (slug), label = the target source's first non-`id` display field value
  (via `"<row>:<display_field>"` lookup). Native-only (qmap).
- `idx_filter_bar` (list.c:191) reads `cur`, resolves options for
  REFERENCE/MULTI_REFERENCE columns, calls `hyle_bud_filter_field(key, label,
  type, cur, opts, nopts)`.
- Renderers live in `external/hyle/c/libhyle-bud/src/filter.c`:
  - `hyle_bud_checkbox_fieldset` — the current full-width multi-ref grid;
    comma-splits `current_value` for checked state.
  - `hyle_bud_reference_select` — single-value `<select>`.
  - `hyle_bud_text_input` — text field.
  - `hyle_bud_filter_field` — dispatch switch; MULTI_REFERENCE → grid (text
    input fallback when no options).

## 8. The multi-ref dropdown widget (planned contract)

Per-field opt-in via a schema hint (see `docs/SCHEMA.md`); when set, the grid is
replaced by a `<details>`-based dropdown (contract in `docs/SSR-CONTRACT.md`):

- Checkboxes are real form fields → they submit **repeated keys** natively;
  no-JS works.
- Enhancement (bud WASM bundle, per `docs/C-ISOMORPHIC-BUD.md`) adds live
  option search and summary label sync; the summary shows labels joined with
  `;` (display only).
- The `bud-state` JSON for the list page must carry: cols (key/label/type/hint/
  current selection), options (id/label) per multi-ref col, rows (id + display
  values), pagination/sort/user, so the WASM tree reproduces the SSR tree
  exactly (id alignment — see the C-isomorphic doc).

## 9. Tests

- hyle unit (`external/hyle/src/hyle_test.c`): same-field OR, cross-field AND,
  single-value unchanged, accent-sensitivity preserved.
- pages-test: repeated-key filtering (`/song/?type=comunhao&type=natal`);
  SSR HTML contains visible checkboxes.
- e2e: open dropdown, tick two, Apply → filtered; no-JS submit path.

## 10. Related docs

- `docs/SCHEMA.md` — the `"f"` hint and schema metadata.
- `docs/SSR-CONTRACT.md` — the widget DOM contract.
- `docs/C-ISOMORPHIC-BUD.md` — how the enhancement ships as a wasm bundle.
