# Schema — field metadata strings and the hint mechanism

How field metadata is stored and parsed, and how UI hints ride it **opaquely**
through the data layer.

## 1. Where the schema strings live

Two separate concepts, don't conflate them:

1. **hyle's registry schema** — the real data schema used by the query/FT S
   layer (`external/hyle/src/source.c`, `qmap_record_field_t`). Parsed from
   record layouts / `bud_field_desc_t`, NOT from JSON strings.
2. **The site's schema JSON strings** — built by `source_build_schema_hd`
   (`mods/source/source.c:1119`) and consumed ONLY by `mods/index`
   (`idx_schema_collect`, `idx_resolve_filter_options`, `idx_resolve_refs`).
   **hyle never parses these strings.** They are site-level metadata.

This split is what lets UI hints ride along without touching the data layer.

## 2. String formats (built in `source_build_schema_hd`)

| Type | Format |
|------|--------|
| scalar | `{"t":<type>}` |
| reference / multi-ref | `{"t":<type>,"s":"<target_source>"}` |
| inverse ref | `{"t":<type>,"s":"<target>","i":"<inverse_name>"}` |

`t` values are the `SOURCE_FIELD_*` / `HYLE_FIELD_*` constants:

- STRING 0, INT 1, BOOL 2, NULLABLE_STRING 3, REFERENCE 4, MULTI_REFERENCE 5.

The map is exposed via `source_get_schema_hd(dataset_id)`.

## 3. Parsing (`mods/index/index.c` `idx_schema_collect`)

```c
sscanf(val, "{\"t\":%d,\"s\":\"%63[^\"]\"", &t, ts);
```

- `m >= 1` → type; `m >= 2` → target source. Extra keys are ignored (the INVERSE
  `"i"` key is precedent: sscanf stops at the mismatch and returns 2).
- Output: `col_t { char key[64]; char label[64]; int type;
  char target_source[64]; unsigned target_hd; }`.
- `target_hd` is resolved later from the target source's fields registry
  (`source_get_fields_hd`) — native-only.

## 4. UI hints — carried opaquely, interpreted by the component layer

Rule (see `docs/ARCHITECTURE.md`): **hyle carries metadata; it never
interprets presentation.** A UI hint is an extra key in the site schema string;
hyle/hyle-core ignores it; the component layer (hyle-bud / mods) reads it.

Implemented hint for the multi-ref filter style (see `docs/FILTERS.md`):

```
{"t":5,"s":"types","f":"dropdown"}     ← dropdown (details+checkbox widget)
{"t":5,"s":"types"}                    ← default: full-width checkbox grid
```

Plumbing (all shipped):

1. `source_build_schema_hd` (source.c): append `,"f":"<style>"` when a field
   opts in (`mods/song/fields.h` sets `filter_style="dropdown"` on `type`).
2. `col_t`: `char filter[16]` (in `mods/index/ux/list.c`).
3. `idx_schema_collect` (index.c): sscanf
   `{"t":%d,"s":"%63[^\"]","f":"%15[^\"]"}` — `m >= 3` → hint set, `m == 2` →
   absent (grid). Initialize `cols[n].filter[0]='\0'` up front (sscanf won't
   touch it on mismatch).
4. `idx_filter_bar` passes it to `hyle_bud_filter_field` (the trailing
   `filter_style` param); `external/hyle/c/libhyle-bud/src/filter.c` renders
   `hyle_bud_multiselect_field` when the hint is `"dropdown"`.

Guard: no `bud`/component symbols may appear in `external/hyle/src` or
`include/hyle` — the hint is an opaque string there.

## 5. Reference display

- `idx_resolve_refs` (index.c:227) resolves stored positions → labels for table
  cells: split stored value on `\n`, treat numeric tokens as positions into the
  target fields registry (`qmap_get_key`), look up `"<slug>:<display_field>"`
  for the label. Falls back to the raw slug. The display field is the target
  source's first non-`id` schema key.
- `idx_resolve_filter_options` (list.c:118) uses the same display-field logic
  to build filter options.

## 6. Related docs

- `docs/FILTERS.md` — how filters use this metadata (multi-ref, repeated keys).
- `docs/ARCHITECTURE.md` — why the data layer stays neutral.
- `docs/SSR-CONTRACT.md` — what the component layer emits from the hint.
