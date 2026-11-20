---
name: externals-map
description: "Map external libraries hyle, bud, libhyle-bud, libhyle-source, axil, libxylem, libcorm, lemma. Use when touching external/libhyle, external/libbud, or cross-harness boundaries. Triggers on: external/libhyle, external/libbud, libhyle-bud, libhyle-source, axil, libxylem, corm, lemma"
---

# Externals Map — hyle / bud / axil / libxylem

## Roles & invariants

| Lib | Path | Role | Invariant |
|---|---|---|---|
| **axil** | `external/axil` | HTTP/sessions/auth/chroot `:8080` | Owns HTTP, never in WASM TU |
| **libxylem (XY)** | `external/libxylem` | `dlopen(RTLD_LOCAL\|NODELETE)` before chroot | **Only** cross-`.so` mechanism; `XY_DECL` behind `#ifndef MODULE_IMPL`, `XY_IMPL` in owner, `static` default |
| **qmap** | `external/libqmap` | opaque store `unsigned` handle | Never free values |
| **lemma** | `external/liblemma` | `lemma_fold` accent-sensitive `pão≠pao` no TRANSLIT | No `iconv` in search fold |
| **hyle** | `external/libhyle/include/hyle/{hyle.h,schema.h,field.h,query.h,source.h}` `src/{libhyle.c,field.c,query.c}` | canonical `hyle_schema_desc_t`, query/FTS | **Zero `bud` symbols** `grep -rn bud external/libhyle/src` must be `0` |
| **libhyle-source** | `external/libhyle-source/include/hyle-source/{hyle_source.h,store.h}` `src/{engine.c,store_fs.c,store_mem.c,dsv.c}` | `hyle_source_store_ops_t` `store_fs`/`store_mem` | All writes via `source_update_item/del` → `hyle put/del` |
| **libhyle-bud** | `external/libhyle-bud/include/hyle-bud/hyle-bud.h` `src/{filter.c,table.c,picker.c,libhyle-bud.c}` `hyle-bud-wasm.mk` | **ONLY** bud-dependent bridge | Bridges Hyle→Bud filters/tables; deps `-lhyle -lbud -lqmap` |
| **bud** | `external/libbud/include/bud/{bud.h,bud_app.h,bud_jsx.h}` `src/libbud.c,bud_wasm_app.c` | 5-field binder `bud_field_desc_t`, SSR+WASM bridge | Zero DB symbols |
| **stele** | `external/libstele` | Isomorphic WASM HAL & reactor kernel (`stele.wasm`) | Leaf library; global-base 2097152 |
| **corm** | `external/libcorm` | Multi-axis database & recall kernel (`joint`, `lemma`, `sepal`, `islet`) | Shared by site & `pi-mm` |
| **jarvis** | `external/libjarvis` | Pure-C voice/intent daemon (`jarvisd -j`) & solver (`libjarvis.so`) | Install with `PREFIX="$HOME/.local"`; run via `systemd-run --user --unit=jarvisd` |

## Building & Installing `external/*` Libraries

All `external/*` libraries include `../mk/include.mk` $\rightarrow$ `external/mk/portable.mk`, which defaults to `prefix-Linux := /usr`.
- **Never run bare `make install` without root** (fails with `Permission denied` on `/usr/include/ttypt/*`).
- Always pass `PREFIX="$HOME/.local"`:
  ```bash
  make -C external/lib<name> PREFIX="$HOME/.local" all install
  ```
- In git worktrees, always initialize submodules first via `sh scripts/worktree.sh init` (see `worktree-and-memory` skill).

## Checks

```bash
grep -rn 'bud' external/libhyle/src external/libhyle/include/hyle  # must be 0
grep -rn 'qmap' external/libbud/src external/libbud/include         # must be 0
grep -rn 'hyle_source_put\|hyle_source_del' mods/ --include="*.c" | grep -v mods/source  # must be 0 (M05)
```

Framework-pair `hyle` neutral → `SSR contract` (plain HTML + `data-*` hooks) → paired `bud↔WASM / React↔hydration`. No-JS must work; `data-bud-*` additive. No Rust/Dioxus/Deno in request path.

## When to change externals

Only via `hyle-bud` bridge or `libhyle-source` store. See `hyle-bud-bridge`, `data-engine`, and `worktree-and-memory` skills. Do not add DB logic to `bud`, nor DOM logic to `hyle`.

See `docs/ARCHITECTURE.md` §1-2 `docs/DESIGN.md` §3 `docs/GOALS.md` §1.2 `docs/CONVENTIONS.md` `external/libhyle/bud`.
