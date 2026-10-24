# Build Portability, Linker Purity, and Encapsulation Remediation Strategy Roadmap

**Target Systems:** Main Site Host Repository (`site`) and all 34 Git Submodules (`external/*`, `.pi/extensions/*`).  
**Prerequisites:** Findings documented in `docs/BUILD_PORTABILITY_AND_ENCAPSULATION_AUDIT.md`.  
**Status:** Canonical Implementation Roadmap & Architecture Plan.

---

## 1. Executive Summary & Architectural Principles

The forensic audit documented that while the system functions in the developer's specific workspace (`/home/quirinpa/site`), it relies on five major architectural anti-patterns:
1. **In-Tree Runtime Overrides:** Shell scripts inject 30+ in-tree directories into `LD_LIBRARY_PATH`, masking whether libraries are installed into the system authority (`$PREFIX/lib`).
2. **Hardcoded User Environments:** Submodule Makefiles hardcode `/home/quirinpa/site/...` and bake `/home/quirinpa/.local/lib` into ELF `DT_RUNPATH` headers across 44 binaries.
3. **Forced Sibling Linkage:** Domain module Makefiles force `-L../axil-nd/lib -laxil-nd` and `-L../nd-bus/lib -lnd-bus`, bypassing system package resolution.
4. **Header Wildcard Pollution:** `add-prefix-glob := ../nd-*` flattens include isolation, allowing 53 private cross-module header inclusions (`config.h`, `st.h`, `player.h`, `mcp.h`, `view.h`).
5. **Non-XY Symbol Leaks:** Global mutable arrays (`nd_hds[]`), global function pointers (`nd_dwritef`), and raw C symbols (`nd_evt_*`) bypass the libxylem XY bus.

### Core Architectural Treaties
To achieve true system portability and encapsulation, all remediation must adhere to three foundational treaties:
- **The Single Runtime Authority Treaty:** Dev runs what prod runs. Every shared library must resolve from the system path alone (`$PREFIX/lib`, typically `/usr/lib` or `/usr/local/lib`). Zero in-tree `LD_LIBRARY_PATH` exports; zero `DT_RUNPATH` attributes pointing into user homes or source checkouts.
- **The Independent Submodule Treaty:** Every submodule under `external/` must compile standalone on a clean system without assuming it lives inside `/home/quirinpa/site` or beside sibling checkouts. Include and library paths must use portable relative `add-prefix` directives or system paths.
- **The XY Boundary Treaty:** The libxylem XY bus is the only cross-`.so` boundary. No shared global data arrays, no cross-module function pointers, no forced sibling dynamic linking, and no `#include` of private sibling headers.

---

## 2. Pillar 1: DT_RUNPATH Purge & Linker Purity Remediation

### 2.1 Purge `-Wl,-rpath` and `/home/quirinpa` from Submodule Makefiles

#### Target 1: `external/libcorm/Makefile`
- **Current State (Line 35):**
  ```makefile
  LDFLAGS += -Wl,-rpath,/home/quirinpa/.local/lib
  ```
- **Remediation Action:** Delete line 35. Rely on the system library path (`$PREFIX/lib`) added by `../mk/portable.mk`.
- **Target Objects Cleaned:** 22 ELF binaries, including `libcorm.so`, `librec_axis_*.so`, `corm`, `cormd`, and test runners.

#### Target 2: `external/liblemma/Makefile`
- **Current State (Lines 13–14):**
  ```makefile
  CFLAGS += -I/home/quirinpa/site/external/libcorm/include
  LDFLAGS += -Wl,-rpath,/home/quirinpa/.local/lib
  ```
- **Remediation Action:**
  Replace with portable relative prefix:
  ```makefile
  add-prefix := ../libcorm
  ```
  Delete line 14 (`LDFLAGS += -Wl,-rpath,...`).
- **Target Objects Cleaned:** 7 ELF binaries, including `liblemma.so` and `lemma_*_test`.

#### Target 3: `external/libjoint/Makefile`
- **Current State (Lines 11–14):**
  ```makefile
  CFLAGS += -I/home/quirinpa/site/external/libcorm/include
  CFLAGS += -I/home/quirinpa/site/external/liblemma/include
  LDFLAGS += -L/home/quirinpa/site/external/liblemma/lib -Wl,-rpath,/home/quirinpa/.local/lib
  ```
- **Remediation Action:**
  Replace lines 11–14 with portable relative prefix:
  ```makefile
  add-prefix := ../libcorm ../liblemma
  ```
  `portable.mk` automatically expands `add-prefix` to `-I../libcorm/include -I../liblemma/include` and `-L../libcorm/lib -L../liblemma/lib`.
- **Target Objects Cleaned:** 5 ELF binaries, including `libjoint.so` and test runners.

#### Target 4: `external/libsepal/Makefile`
- **Current State (Lines 7–9):**
  ```makefile
  CFLAGS += -I/home/quirinpa/site/external/libcorm/include
  CFLAGS += -I/home/quirinpa/site/external/liblemma/include
  LDFLAGS += -L/home/quirinpa/site/external/liblemma/lib -Wl,-rpath,/home/quirinpa/.local/lib
  ```
- **Remediation Action:**
  Replace lines 7–9 with portable relative prefix:
  ```makefile
  add-prefix := ../libcorm ../liblemma
  ```
- **Target Objects Cleaned:** `libsepal.so`.

### 2.2 Purge In-Tree `LD_LIBRARY_PATH` from Scripts and Makefiles

#### Target 1: `start.sh`
- **Current State (Lines 13–15):**
  ```sh
  in_tree_nd_libs=$(printf ':%s' "$SCRIPT_DIR"/external/nd-*/lib)
  export LD_LIBRARY_PATH="$SCRIPT_DIR/external/libcorm/lib:$SCRIPT_DIR/external/axil-nd/lib:$SCRIPT_DIR/external/axil-auth/lib:$SCRIPT_DIR/external/liblemma/lib${in_tree_nd_libs}:${LD_LIBRARY_PATH:-/usr/lib}"
  ```
- **Remediation Action:**
  Delete lines 13–15 completely. Re-establish the invariant:
  ```sh
  # Invariant: every library resolves from the system path ($PREFIX/lib).
  # Run `make install-libs` to publish rebuilt libraries before running.
  ```

#### Target 2: `scripts/run-with-server.sh`
- **Current State (Lines 30–32):**
  ```sh
  in_tree_nd_libs=$(printf ':%s' "$REPO_ROOT"/external/nd-*/lib)
  export LD_LIBRARY_PATH="${REPO_ROOT}/external/libcorm/lib:${REPO_ROOT}/external/axil-nd/lib:${REPO_ROOT}/external/axil-auth/lib:${REPO_ROOT}/external/liblemma/lib${in_tree_nd_libs}:${LD_LIBRARY_PATH:-/usr/lib}"
  ```
- **Remediation Action:**
  Delete lines 30–32. The test runner must execute against system-installed libraries. If local development requires testing uninstalled changes, use a dedicated opt-in flag (e.g. `DEV_IN_TREE_LIBS=1`) that emits a prominent warning.

#### Target 3: `Makefile` (`run-universe` Target)
- **Current State (Lines 402–403):**
  ```makefile
  @in_tree_nd_libs=$$(printf ':%s' "$$PWD"/external/nd-*/lib); \
  export LD_LIBRARY_PATH="$$PWD/external/axil-nd/lib:$$PWD/external/axil/lib:$$PWD/external/axil-tty/lib:$$PWD/external/axil-auth/lib$${in_tree_nd_libs}:$${LD_LIBRARY_PATH:-}"; \
  ```
- **Remediation Action:**
  Delete the synthetic `LD_LIBRARY_PATH` construction. Make `run-universe` depend on `install-libs`:
  ```makefile
  run-universe: install-libs
  	@if [ ! -f "$(ND_DB)" ] || [ "$${RESEED:-0}" = "1" ]; then \
  ...
  ```

#### Target 4: Peripheral Scripts & Submodule Test Targets
- In `scripts/seed-nd-universe.sh:38`: Remove `export LD_LIBRARY_PATH=...`.
- In `tests/e2e/test_full_universe.sh:34`: Remove `export LD_LIBRARY_PATH=...`.
- In `scripts/migrate-store-fs-to-tok.sh:8`: Remove `export LD_LIBRARY_PATH=...`.
- In `tests/unit/run-nd-ui-slot.sh:29`: Remove `LD_LIBRARY_PATH="$SRC_DIR/external/libbud/lib...`.
- In `external/liblemma/Makefile:20–25`: Remove `LD_LIBRARY_PATH=$(abspath lib)` in `test:` recipes; configure binaries to link against `$PREFIX/lib` or `$ORIGIN/../lib`.

### 2.3 Verification Gate for Pillar 1
Run:
```sh
readelf -d $(find mods/ external/ -name '*.so') | grep -E 'RPATH|RUNPATH'
```
Must return ZERO occurrences of `/home/` across all 34 submodules.

---

## 3. Pillar 2: Makefile Portability & Sibling Decoupling

### 3.1 Eliminate Forced Sibling `-L` Linkages in Domain Modules

#### Problem Analysis
Six Makefiles in `external/nd-*` link directly against sibling build trees:
- `external/nd-obj/Makefile:6`: `-L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-session/Makefile:6`: `-L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-view/Makefile:5`: `-L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-world/Makefile:6`: `-L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-mcp/Makefile:6`: `-L../axil-nd/lib -laxil-nd`
- `external/nd-speech/Makefile:5`: `-L../axil-nd/lib -laxil-nd`

#### Remediation Plan
1. **Decouple Dynamic Linkage via XY Migration (see Pillar 3):**
   Once `nd_hds`, `nd_dwritef`, `hd_resolve`, and `nd_evt_*` are accessed via XY hooks, these domain modules have ZERO direct C symbol dependencies on `libaxil-nd.so` or `libnd-bus.so`.
2. **Purge Linker Flags:**
   Update `LDLIBS` across all 6 Makefiles to:
   ```makefile
   # Before:
   LDLIBS-libnd-obj := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed
   # After:
   LDLIBS-libnd-obj := -lxylem -lcorm -lislet -lqsys
   ```
   Repeat for `nd-session`, `nd-view`, `nd-world`, `nd-mcp`, and `nd-speech`.
3. **Result:**
   Domain modules become true XY plugins that link only against fundamental runtime libraries (`-lxylem -lcorm -lqsys`), with zero forced sibling `-L` paths.

---

## 4. Pillar 3: XY Bus Encapsulation & Global Symbol Elimination

### 4.1 Encapsulate Corm Table Handles (`nd_hds`)

#### Problem Analysis
`libaxil-nd.so` exports `unsigned nd_hds[ND_HD_TABLE_MAX];`.  
Domain modules include `<nd/obj-types.h>` which defines:
```c
#define obj_hd nd_hds[HD_OBJ]
#define skel_hd nd_hds[HD_SKEL]
#define contents_hd nd_hds[HD_CONTENTS]
#define obs_hd nd_hds[HD_OBS]
#define ent_hd nd_hds[HD_ENTITY]
#define drop_hd nd_hds[HD_DROP]
#define adrop_hd nd_hds[HD_ADROP]
```
And `<nd/session-types.h>` which defines:
```c
#define player_hd nd_hds[HD_PLAYER]
```
Because these macros expand to direct global array dereferencing, the dynamic linker must resolve `nd_hds` from `libaxil-nd.so` on library load.

#### Remediation Plan
1. **Define Canonical Table Handle Hook in `<nd/law.h>`:**
   ```c
   /* Handle lookup for core Corm tables */
   XY_DECL(unsigned, nd_table_handle, unsigned, hd_idx);
   ```
2. **Implement Hook in `external/axil-nd/src/law_store.c`:**
   ```c
   static unsigned nd_hds[ND_HD_TABLE_MAX]; /* Make static; do NOT export */

   XY_IMPL(unsigned, nd_table_handle, unsigned, hd_idx)
   {
       return (hd_idx < ND_HD_TABLE_MAX) ? nd_hds[hd_idx] : 0;
   }
   ```
3. **Update `<nd/obj-types.h>` and `<nd/session-types.h>`:**
   Map macros to function calls:
   ```c
   #define obj_hd      nd_table_handle(HD_OBJ)
   #define skel_hd     nd_table_handle(HD_SKEL)
   #define contents_hd nd_table_handle(HD_CONTENTS)
   #define obs_hd      nd_table_handle(HD_OBS)
   #define ent_hd      nd_table_handle(HD_ENTITY)
   #define drop_hd     nd_table_handle(HD_DROP)
   #define adrop_hd    nd_table_handle(HD_ADROP)
   #define player_hd   nd_table_handle(HD_PLAYER)
   ```
4. **Result:**
   Eliminates `U nd_hds` across `libnd-obj.so`, `libnd-session.so`, `libnd-speech.so`, `libnd-view.so`, and `libnd-world.so`.

### 4.2 Encapsulate Formatted Output (`nd_dwritef`)

#### Problem Analysis
`external/axil-nd/include/nd/law.h:135` declares a global function pointer:
```c
typedef void nd_dwritef_t(unsigned player_ref, const char *fmt, va_list args);
nd_dwritef_t nd_dwritef;
```
`nd_writef` calls `nd_dwritef(player_ref, fmt, va)`. This forces callers to import `nd_dwritef` from `libaxil-nd.so`.

#### Remediation Plan
1. **Promote `nd_printf` to Primary Formatter:**
   `<nd/law.h:105-119>` already defines `nd_printf(...)`:
   ```c
   static inline int
   nd_printf(unsigned player_ref, char *fmt, ...)
   {
       char buf[1024];
       va_list va;
       int n;
       va_start(va, fmt);
       n = vsnprintf(buf, sizeof(buf), fmt, va);
       va_end(va);
       if (n < 0) return n;
       nd_write(player_ref, buf, (size_t)n < sizeof(buf) ? (size_t)n : sizeof(buf) - 1);
       return n;
   }
   ```
   `nd_printf` formats locally on the caller side and dispatches through the existing canonical XY hook `nd_write(...)`.
2. **Deprecate `nd_dwritef` and `nd_writef`:**
   Redefine `nd_writef` as a static inline alias for `nd_printf`.
3. **Remove `nd_dwritef` Symbol:**
   Delete `nd_dwritef_t nd_dwritef;` from `law.h` and `src/law_io.c`.
4. **Result:**
   Eliminates `U nd_dwritef` across `libnd-obj.so`, `libnd-session.so`, `libnd-speech.so`, and `libnd-world.so`.

### 4.3 Encapsulate Game Event Dispatches (`nd_evt_*`)

#### Problem Analysis
`external/nd-bus/src/nd_events.c` exports 17 C functions (`nd_evt_enter`, `nd_evt_leave`, `nd_evt_status`, `nd_evt_move`, etc.) that perform coarse-to-fine region tree walking. Domain modules call these functions directly, requiring dynamic linkage to `libnd-bus.so`.

#### Remediation Plan
- **Consolidation:**
  As documented in `nd_events.c:6-8` (*"Folded into src/libaxil-nd.c (single-TU module)"*), fold `nd_events.c` directly into `external/axil-nd/src/libaxil-nd.c`.
- **XY Adapter Dispatches:**
  Expose event dispatches through canonical XY declarations in `<nd/event.h>` or `<nd/law.h>`:
  ```c
  XY_DECL(int, nd_fire_event, unsigned, event_id, unsigned, player_ref, unsigned, loc_ref);
  ```
- **Retire Standalone `nd-bus.so` Linkage:**
  Eliminates the separate `libnd-bus.so` dynamic library and all `-lnd-bus` flags.

---

## 5. Pillar 4: Header Namespace Isolation & `add-prefix-glob` Removal

### 5.1 Replace Wildcard Include Expansion in `portable.mk`

#### Problem Analysis
In `external/mk/portable.mk:31–33`:
```makefile
add-prefix-dirs := $(shell [ -n "${add-prefix-glob}" ] && for p in ${add-prefix-glob}; do [ -d "$$p" ] && echo "$$p"; done || true)
prefix := ${pwd} ${add-prefix} ${add-prefix-dirs} ${prefix-${uname}} ...
```
And in `external/axil-nd/Makefile:21`: `add-prefix-glob := ../nd-*`  
And in `external/nd-*/Makefile`: `add-prefix-glob := ../nd-* ../axil-nd`

This exposes all private headers of all 27 sibling modules to every compilation.

#### Remediation Plan
1. **Remove `add-prefix-glob` from `axil-nd` and `nd-*` Makefiles:**
   Delete `add-prefix-glob` across all 28 Makefiles.
2. **Declare Explicit Dependency Prefixes:**
   Each module declares only its direct dependencies:
   ```makefile
   # In nd-obj/Makefile:
   add-prefix := ../axil-nd
   # In nd-equip/Makefile:
   add-prefix := ../axil-nd ../nd-obj
   ```
3. **Deprecate Wildcard Globbing in `portable.mk`:**
   Retain `add-prefix-glob` only for backward compatibility, but emit a warning if used.

### 5.2 Eliminate Private Header Inclusions (53 Inclusions Mapped)

All cross-module `#include "..."` statements must be migrated to public `<nd/...>` headers:

| File Location | Existing Private Include | Remediation (Canonical Header) |
|---|---|---|
| `external/nd-obj/src/entity.c:9` | `#include "config.h"` | `#include <nd/session-types.h>` |
| `external/nd-obj/src/entity.c:10` | `#include "mcp.h"` | `#include <nd/mcp.h>` |
| `external/nd-obj/src/entity.c:11` | `#include "params.h"` | `#include <nd/speech-types.h>` |
| `external/nd-obj/src/entity.c:12` | `#include "st.h"` | `#include <nd/region.h>` |
| `external/nd-obj/src/entity.c:18` | `#include "view.h"` | `#include <nd/view.h>` |
| `external/nd-session/src/session.c:32–42` | `#include "config.h"`, `"mcp.h"`, `"player.h"`, `"st.h"`, `"view.h"` | `#include <nd/mcp.h>`, `<nd/region.h>`, `<nd/view.h>`, `<nd/player.h>` |
| `external/nd-world/src/map.c:8–9` | `#include "config.h"`, `"st.h"` | `#include <nd/session-types.h>`, `<nd/region.h>` |
| `external/nd-world/src/spacetime.c:24–29` | `#include "config.h"`, `"noise.h"`, `"params.h"`, `"player.h"`, `"view.h"`, `"mcp.h"` | `#include <nd/session.h>`, `<nd/view.h>`, `<nd/mcp.h>`, `<nd/region.h>` |
| `external/nd-speech/src/look.c:5–7` | `#include "mcp.h"`, `"player.h"`, `"st.h"` | `#include <nd/mcp.h>`, `<nd/player.h>`, `<nd/region.h>` |
| `external/nd-speech/src/speech.c:9–11` | `#include "config.h"`, `"player.h"`, `"st.h"` | `#include <nd/session-types.h>`, `<nd/player.h>`, `<nd/region.h>` |
| `external/nd-view/src/view.c:1, 6–8` | `#include "view.h"`, `"mcp.h"`, `"noise.h"`, `"params.h"` | `#include <nd/view.h>`, `<nd/mcp.h>`, `<nd/speech-types.h>` |

---

## 6. Pillar 5: Client Asset, Capability & Host Purity Alignment

### 6.1 Browser Bridge Single-Sourcing & Capability Alignment

#### Problem Analysis
- `htdocs/axil-bridge.js`, `htdocs/axil-ws-bridge.js`, and `external/axil/htdocs/axil-bridge.js` are three duplicate copies.
- `htdocs/axil-tty-bridge.js` and `htdocs/tty-bridge.js` are two duplicate copies.
- Fallback SSR in `external/axil-nd/src/http.c:75` specifies `data-capabilities="axil-ws tty lamina bud"`.
- Full SSR in `mods/nd/ux/nd_ui.c:888` specifies `data-capabilities="axil-ws axil-tty lamina webgl xylem"`.

#### Remediation Plan
1. **Single-Source Bridges in Submodules:**
   - Canonical WebSocket bridge lives strictly in `external/axil/htdocs/axil-ws-bridge.js`.
   - Canonical TTY bridge lives strictly in `external/axil-tty/htdocs/axil-tty-bridge.js`.
2. **Symlink / Copy in `make assets-sync`:**
   Main repository copies or symlinks these directly into `htdocs/`:
   ```sh
   htdocs/axil-ws-bridge.js -> external/axil/htdocs/axil-ws-bridge.js
   htdocs/axil-tty-bridge.js -> external/axil-tty/htdocs/axil-tty-bridge.js
   ```
   Add compatibility symlinks:
   ```sh
   htdocs/axil-bridge.js -> axil-ws-bridge.js
   htdocs/tty-bridge.js -> axil-tty-bridge.js
   ```
3. **Standardize Capability Tokens:**
   Update `external/axil-nd/src/http.c:75` and `mods/nd/ux/nd_ui.c:888` to declare identical tokens:
   `data-capabilities="axil-ws axil-tty lamina bud webgl xylem"`
4. **Consolidate Header Duplications:**
   Delete `include/ttypt/axil-ws.h` from main repo and `external/axil-tty`. Retain single source of truth in `external/axil/include/ttypt/axil-ws.h`, published to `/usr/include/ttypt/axil-ws.h`.

### 6.2 Host-Tenant Source Decoupling in `mods/nd/Makefile`

#### Problem Analysis
`mods/nd/Makefile:6–11` compiles foreign tenant source files directly:
```makefile
ND_UX_MODULES = \
    $(REPO_ROOT)/external/nd-attr/ux/attr_ui.c \
    $(REPO_ROOT)/external/nd-equip/ux/equip_ui.c \
    $(REPO_ROOT)/external/nd-spell/ux/spell_ui.c \
    $(REPO_ROOT)/external/nd-fight/ux/fight_ui.c \
    $(REPO_ROOT)/external/nd-mortal/ux/mortal_ui.c
```

#### Remediation Plan
1. **WASM Module Streaming Integration:**
   `external/libxylem/src/libxylem-wasm.c` and `htdocs/xylem-bridge.js` already implement dynamic in-browser WASM module loading (`wasm_xy_load_module`).
2. **Build Tenant Modules as Standalone WASM Reactors:**
   Add WASM build targets to each tenant submodule:
   ```makefile
   # In external/nd-attr/Makefile:
   htdocs/nd-attr.wasm: ux/attr_ui.c
   ```
3. **Dynamic UI Registration via XY in WASM:**
   The host shell (`mods/nd/ux/nd_ui.c`) dynamically streams `nd-attr.wasm`, `nd-equip.wasm`, `nd-spell.wasm`, which call `nd_ui_glue_register()` at runtime, completely eliminating foreign `.c` compilation in `mods/nd/Makefile`.

---

## 7. Phased Implementation Schedule & Milestones

The remediation strategy resolves milestone sequencing dependencies by concurrently staging DSO decoupling alongside search path stripping:

```
[Milestone 1: DT_RUNPATH & Absolute Paths Across 34 Submodules]
                             │
                             ▼
[Milestone 2 & 3 (Concurrent): XY Bus Encapsulation & LD_LIBRARY_PATH Removal]
  - Decouple domain DSOs (eliminate -laxil-nd, -lnd-bus)
  - Encapsulate nd_hds, nd_dwritef, nd_evt_*
  - Strip in-tree LD_LIBRARY_PATH from start.sh, Makefile, scripts
  - Publish libraries via make install-libs to $PREFIX/lib
                             │
                             ▼
[Milestone 4: Header Namespace Isolation Across All 34 Submodules]
                             │
                             ▼
[Milestone 5: Client Asset & Capability Harmonization]
```

### Milestone 1: DT_RUNPATH & Absolute Path Purge (All 34 Submodules)
- **Scope:** `external/libcorm`, `external/liblemma`, `external/libjoint`, `external/libsepal`, and all 34 submodule Makefiles.
- **Actions:**
  - Strip `-Wl,-rpath,/home/quirinpa/.local/lib` from all Makefiles.
  - Replace `/home/quirinpa/site/...` with `add-prefix := ../libcorm ../liblemma`.
  - Recompile and verify `readelf -d` output has 0 occurrences of `/home/` across the entire submodule tree.
- **Quality Gate:** `sh scripts/check-no-tree-rpath.sh` PASS; `grep -r "/home/quirinpa" external/` returns 0 hits.

### Milestone 2 & 3 (Staged Concurrently): XY Bus Decoupling & `LD_LIBRARY_PATH` Stripping
- **Scope:** `external/axil-nd`, `external/nd-bus`, domain modules (`nd-obj`, `nd-session`, `nd-view`, `nd-world`, `nd-speech`), `start.sh`, `scripts/run-with-server.sh`, `Makefile`.
- **Sequencing Resolution:** Domain modules must be decoupled from direct dynamic linkage (`-laxil-nd`, `-lnd-bus`) *at the same time* `LD_LIBRARY_PATH` is stripped. Otherwise, testing under `env -u LD_LIBRARY_PATH` would fail to resolve uninstalled sibling DSOs.
- **Actions:**
  - Encapsulate `nd_hds` behind `XY_DECL(unsigned, nd_table_handle, ...)` and make `nd_hds` static in `law_store.c`.
  - Replace `nd_dwritef` with inline `nd_printf`.
  - Fold `nd_events.c` into `axil-nd` and eliminate `-lnd-bus` and `-laxil-nd` linkages in domain Makefiles.
  - Remove all in-tree `LD_LIBRARY_PATH` exports.
  - Execute `make install-libs` into `$PREFIX/lib`.
- **Quality Gate:** `nm -u external/nd-*/lib/*.so | grep -E 'nd_hds|nd_dwritef|nd_evt_'` returns 0 hits; `make test` (121 passed, 0 failed) succeeds under `env -u LD_LIBRARY_PATH`.

### Milestone 4: Header Namespace Isolation Across All 34 Submodules
- **Scope:** `external/mk/portable.mk` and all 27 `external/nd-*` modules.
- **Actions:**
  - Remove `add-prefix-glob` across all Makefiles; declare explicit `add-prefix`.
  - Migrate all 53 cross-module header inclusions to public `<nd/...>` headers.
  - Update `scripts/check-nd-encapsulation.sh` to forbid cross-directory private includes.
- **Quality Gate:** `sh scripts/check-nd-encapsulation.sh` PASS; `sh scripts/check-module-boundaries.sh` PASS.

### Milestone 5: Client Asset & Capability Harmonization
- **Scope:** `htdocs/`, `external/axil`, `external/axil-tty`, `external/axil-nd`, `mods/nd`.
- **Actions:**
  - Single-source `axil-ws-bridge.js` in `external/axil` and `axil-tty-bridge.js` in `external/axil-tty`.
  - Standardize `data-capabilities` across SSR templates.
  - Consolidate duplicate headers.
- **Quality Gate:** `sh scripts/check-no-site-specific-js.sh` PASS; `make test-e2e FILE=nd-browser-post-seed.test.ts` PASS with 0 browser console 404s.

---

## 8. Verification & Governance Matrix

| Check / Verification Command | Condition for Acceptance | Responsible Gate |
|---|---|---|
| `readelf -d $(find mods/ external/ -name '*.so') \| grep -E 'RPATH\|RUNPATH'` | Must contain ZERO occurrences of `/home/`. | `scripts/check-no-tree-rpath.sh` |
| `grep -rn "LD_LIBRARY_PATH" start.sh scripts/ Makefile` | Must return ZERO in-tree path exports. | Pre-commit linter |
| `nm -u external/nd-*/lib/*.so \| grep -E 'nd_hds\|nd_dwritef'` | Must return ZERO hits (pure XY decoupling). | `scripts/check-nd-encapsulation.sh` |
| `grep -rnE '#include "(\.\./\|config\.h\|st\.h\|mcp\.h\|view\.h\|player\.h)' external/nd-*/src/` | Must return ZERO hits (canonical `<nd/...>` includes only). | `scripts/check-module-boundaries.sh` |
| `env -u LD_LIBRARY_PATH make test` | All 121 tests pass with zero failures using system libraries. | CI Gate |
