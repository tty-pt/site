---
name: pure-c-isomorphic
description: "Enforce pure-C isomorphic UX dual-compile and WASM staleness. Use when editing mods/*/ux/*.c, site_ui.c, list.c, or htdocs/*.wasm. Triggers on: mods/*/ux, isomorphic ux, pure-C, hyle-bud, wasm staleness, site_ui.c"
---

# Pure-C Isomorphic — UX Dual-Compile & WASM Staleness

UX `mods/*/ux/*.c` compiles twice: native `.so` for SSR and `htdocs/*.wasm` for browser via `clang --target=wasm32-wasi -mexec-model=reactor -Wl,--export-all --allow-undefined-file=wasm-allowed-imports.lst`.

## Allowed vs forbidden in `mods/*/ux/*.c`

- **Allowed:** `bud.h / bud_jsx.h / bud_app.h` + `hyle-bud/hyle-bud.h` + pure C. One `bud_app_render(state)` + `bud-state` JSON + `#bud-root` / `#chrome-root` wrapper.
- **Forbidden:** `XY_`/`xy_`/`qmap_`/`source_`/`axil_`/`stoma_`/`hyle_source_`/`var/` literals — must stay in `mods/*/index.c` or `mods/source` or `mods/common/common_storage.c`. Exception: `hyle_bud_*` is sanctioned.
- **Include allowlist:** Only `mods/common/ux/site_ui.c` and `mods/index/ux/list.c` may `#include "*.c"` (`scripts/check-module-boundaries.sh`). Others must `XY_DECL` boundary.
- **Preprocessor:** No `#if`/`#ifdef` for nodes — branch on runtime `state`. Allowed only: `#ifndef *_C` guards, `__attribute__((import_module("env")…))`, `site_ui.c:#ifndef __wasm__` aggregator, `site_page.c:#if __has_include` fallback (`C-ISOMORPHIC-BUD §3`).

## Checks

```bash
grep -En 'qmap_|source_|axil_|hyle_source|XY_|var/' mods/*/ux/*.c | grep -v hyle_bud_
grep -En 'XY_DECL|xy_load|xy_install' mods/*/ux/*.c  # must be 0
grep -n "#if" mods/*/ux/*.c  # only allowlisted above
sh scripts/check-ux-purity.sh  # warn-only, 0 XY hooks expected
sh scripts/check-wasm-imports.sh  # W06 wasm-objdump -x htdocs/*.wasm must not import qmap_/source_/axil_/xy_/hyle_source_/XY_
```

Move data collection to `mods/*/{index,source}.c` via `hyle_source` + `hyle-bud` state apply (`bud_state_apply_stride_len`), not WASM.

## WASM staleness trap

`build.mk` WASM rule probes `WASI_CC` and skips silently if absent (`Skipping WASM build`). Even with WASI, `.d` deps can drift. `C-ISOMORPHIC-BUD.md:243 NO prerequisites — rm *.wasm` is the historical trap; `BUILD.md:48 LIST_UX_DEPS + .d` is now the fix — but `htdocs/*.wasm` is still `1.5 days stale` observed (`ls -lt htdocs/site_chrome.wasm` vs `mods/common/ux/site_paths.c`).

Fix:

```bash
clang --target=wasm32-wasi -c -x c /dev/null -o /tmp/probe.o && echo WASI ok || echo no WASI
rm -f htdocs/{list,song_detail,gig_detail,site_chrome}.wasm{,.d}
make -j4
wasm-objdump -x htdocs/*.wasm | grep -E 'qmap_|source_|axil_|xy_|hyle_source' && echo W06 fail || echo W06 pass
```

If `WASI_CC` absent, `htdocs/*.wasm` is *absent not stale* — SSR still works (`No-JS must work` `SSR-CONTRACT`), WASM enhancement silently disabled.

See `docs/C-ISOMORPHIC-BUD.md` §3-6 + `docs/BUILD.md` §WASM + `docs/WASM-BRIDGE.md`.
