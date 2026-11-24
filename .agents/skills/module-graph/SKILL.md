---
name: module-graph
description: "Audit XY module graph and mods.load DAG. Use when editing mods/core/core.c, mods.load, XY_DECL/XY_IMPL, or adding cross-module calls. Triggers on: mods.load, xy_load, XY_DECL, XY_IMPL, module graph, mods/core/core.c"
---

# Module Graph — XY & mods.load DAG

## Load order (`mods.load` ascii, `docs/ARCHITECTURE.md:79-96`)

```
core.so → common.so → mpfd.so, source.so  (common xy_loads mpfd)
        → mods.load: i18n, poem→index, song→index+mpfd(+libtransp), grp→index+mpfd+song, gig→index+mpfd+song+source+grp
        + index→common+auth+mpfd+source, auth→common+libaxil-auth
site_chrome WASM_ONLY=1 → htdocs/site_chrome.wasm (#chrome-root, not .so)
mods/redir exists but never loaded
```

`core.c:xy_install` loads `common+source` then `mods.load` order `i18n→poem→song→grp→gig`.

## XY is the only cross-.so boundary

- `XY_DECL` in header behind `#ifndef MODULE_IMPL`, `XY_IMPL` in owner `.c`, constants outside guard, `static` by default, never `extern` or cross-module `#include "*.c"` (except `site_ui.c|list.c` sanctioned `scripts/check-module-boundaries.sh`).
- Declare deps via `xy_load()` in `xy_install()` — maximally independent = explicit DAG, not zero edges (`poem→index`, `song→index+mpfd`, `grp→index+mpfd+song`, `gig→index+mpfd+song+source+grp`; `core` only `common+source`). See `ARCHITECTURE §5`, `CONVENTIONS`.

## Checks

```bash
grep -rn 'XY_DECL\|XY_IMPL' mods/ --include="*.h" --include="*.c"
grep -rn 'xy_load' mods/ --include="*.c"  # must be in xy_install DAG order
cat mods.load
grep -rn '#include ".*\.c"' mods/ --include="*.c"  # only site_ui.c|list.c allowed
sh scripts/check-module-boundaries.sh  # includes xy_load allowlist + M07 idx_* switch
```

When adding `grp→song` etc, update `xy_load()` in `xy_install` and `mods.load` atomically; audit `XY_DECL` outside guard and `static` default; deduplicate via `libxylem` not `#include`.

See `docs/ARCHITECTURE.md` §3-5, `docs/CONVENTIONS.md` XY, `docs/DESIGN.md` §3, `mods/core/core.c`.
