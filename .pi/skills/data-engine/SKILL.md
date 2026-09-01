---
name: data-engine
description: "Hyle data engine, query, FTS, and source writes. Use when touching source_update_item, hyle put/del, DSV/JSON stores, or accent folding. Triggers on: hyle, source_update_item, hyle put/del, DSV, FTS, stoma_fold"
---

# Data Engine — Hyle & Source Writes

## Invariants (`GOALS.md §1.2`, `ARCHITECTURE.md §6`, `CONVENTIONS.md`)

- **All row writes via `source_update_item` / `source_delete_item` → `hyle put/del` (FTS), never direct `var/`** — `var/` literals only `mods/common/common_storage.c`/`mods/source/source.c` or `source_setup` (`scripts/check-module-boundaries.sh` M05).
- **Search accent-sensitive** `pão≠pao` (no TRANSLIT, only `axil_slugify`) — `stoma` `stoma_fold` without `iconv`.
- **No-JS must always work** (`SSR-CONTRACT`) — SSR emits plain HTML + `data-*` hooks (`data-hyle-*` contract), `data-bud-*` additive.
- **Thin wrapper** `mods/source/source.c` over `libhyle-source` `hyle_source_store_ops_t` (`store_fs`/`store_mem` `engine.c` `dsv.c` `json.c`).

## Stores

| Store | Path | Engine |
|---|---|---|
| `store_fs` | `external/hyle/c/libhyle-source/src/store_fs.c` | filesystem DSV |
| `store_mem` | `src/store_mem.c` | memory (tests) |
| `hyle` core | `external/hyle/src/{libhyle.c,field.c,query.c,source.c,ctx.c,view.c}` `include/hyle/*.h` | `hyle_schema_desc_t`, `hyle_query_t`, FTS |

## Checks

```bash
grep -rn 'source_update_item\|source_delete_item' mods/ --include="*.c" --include="*.h"
grep -rn 'hyle_put\|hyle_del\|hyle_source_put' mods/ --include="*.c" | grep -v mods/source  # must be 0
grep -rn 'var/' mods/ --include="*.c" --include="*.h" | grep -v 'common_storage.c\|source.c\|source_setup'
grep -rn 'stoma_fold\|axil_slugify' mods/ --include="*.c"
grep -rn 'TRANSLIT' mods/ external/hyle/  # must be 0
```

Thin modules `~150 lines` (`poem.c` exemplar) — flags/structs/hooks `register_standard_item_handlers`, `with_module_item_access(ICTX_*)`, `index_module_init`, `source_setup`.

See `docs/ARCHITECTURE.md` §6-7, `docs/SCHEMA.md`, `docs/DESIGN.md` §4.7, `external/hyle` `external/hyle/c/libhyle-source` `external/stoma`.
