---
name: hyle-bud-bridge
description: "Bridge Hyle schema to Bud binder for filters/tables/pickers. Use when editing filter.c, table.c, picker.c, bud_field_desc_t, or WASM wiring. Triggers on: hyle-bud, filter.c, table.c, picker.c, bud_field_desc_t, hyle_schema_desc_t"
---

# Hyle-Bud Bridge — Filter/Table/Picker

`libhyle-bud` is the **ONLY** bud-dependent bridge (`external/hyle/c/libhyle-bud`), compiling via `hyle-bud-wasm.mk`:

```make
include external/hyle/c/libhyle-bud/hyle-bud-wasm.mk  # defines HYLE_BUD_WASM_SRC / CFLAGS
EXTRA_CFLAGS += -I external/hyle/c/libhyle-bud/include
EXTRA_LDLIBS += -lhyle-bud -lhyle -lbud -lqmap
```

Used only by `mods/index/Makefile`, `mods/gig/Makefile`, `mods/grp/Makefile`.

## Bridge

- **Hyle** owns canonical `hyle_schema_desc_t` (no DOM) per `external/hyle`.
- **Bud** is pure 5-field binder `bud_field_desc_t` (`external/bud` 5 fields) no DB.
- **libhyle-bud** `src/{filter.c,table.c,picker.c,form.c,libhyle-bud.c}` maps `hyle_schema_desc_t → bud_field_desc_t` + `hyle_bud_state_apply_stride_len` + `hyle_bud_filter_*`/`hyle_bud_table_*` helpers. SSR emits plain HTML + `data-*` hooks; `data-bud-*`/patch ops are additive (`SSR-CONTRACT`).

## Wiring per module

- `mods/index/ux/list_*.c` `list_fill.c` → `htdocs/list.wasm` (filter + table + picker).
- `mods/gig/ux/{add,detail}.c` → `htdocs/gig_detail.wasm`.
- `mods/grp/ux/{all,detail}.c` SSR-only `hyle-bud` (no WASM).
- `site_chrome WASM_ONLY` not bridged.

## Hints & pickers

`hyle` carries UI hints opaquely (`FILTERS.md`/`SCHEMA.md` `f=dropdown` `filter.c`/`field.c`). `FILTERS:§8 f=dropdown` → `SCHEMA:§5 hint` → `PICKERS.md omni-dropdown` universal `hyle_bud_filter`. See `docs/FILTERS.md` `docs/SCHEMA.md` `docs/PICKERS.md`.

## Checks

```bash
grep -rn 'hyle_bud' mods/ --include="*.c" --include="*.h"
cat mods/index/Makefile | grep HYLE_BUD_WASM_SRC
grep -rn 'bud_field_desc_t' mods/ --include="*.c"
```

Do not add `bud` logic to `hyle` nor `hyle` query to `bud` — only via bridge.

See `docs/C-ISOMORPHIC-BUD.md` §3-5, `docs/WASM-BRIDGE.md`, `external/hyle/c/libhyle-bud/hyle-bud-wasm.mk`.
