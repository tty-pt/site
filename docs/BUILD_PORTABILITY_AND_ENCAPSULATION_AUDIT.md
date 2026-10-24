# Comprehensive Audit of Build Portability, Runtime Linker Hacks, and Encapsulation Boundaries

**Audit Target:** Main repository (`site`) and all 34 Git submodules.  
**Baseline:** `old-main` (`b1db3f44575d18bdfdf4dfeeb944e2427a54bbbc`) $\rightarrow$ `HEAD` (`d2f0608`).  
**Status:** Complete Architectural Review & Forensic Analysis.

---

## 1. Executive Summary & Audit Scope

This audit was conducted to identify any "clever tricks", build system workarounds, runtime linker manipulations (`LD_LIBRARY_PATH` / `DT_RUNPATH`), hardcoded developer environment paths, and module encapsulation breaches introduced across the repository and all submodules since `old-main`.

While the system successfully compiles and passes functional tests (all 121 E2E tests, boundary checks, and full universe verification), the audit uncovered significant architectural compromises that undermine the repository's foundational invariants:

1. **Linker Search Path Hacks (`LD_LIBRARY_PATH`):** Multiple shell scripts (`start.sh`, `scripts/run-with-server.sh`, `scripts/seed-nd-universe.sh`, `tests/e2e/test_full_universe.sh`, and `Makefile`) inject multi-path in-tree `LD_LIBRARY_PATH` strings. This directly violates the documented rule in `start.sh` that dev and test environments must run against system-installed libraries (`$PREFIX/lib`).
2. **Hardcoded User Environments & `DT_RUNPATH` Contamination:** 44 compiled ELF binaries and shared libraries contain `/home/quirinpa/.local/lib` or `/home/quirinpa/site/...` baked directly into their `DT_RUNPATH` headers via `-Wl,-rpath` flags in `external/libcorm`, `external/libjoint`, `external/libsepal`, and `external/liblemma`. These binaries are unportable and will fail in clean container or non-host environments.
3. **Forced Sibling Linkage in Makefiles:** Six domain module Makefiles under `external/nd-*` force `-L../axil-nd/lib -laxil-nd` and `-L../nd-bus/lib -lnd-bus`, bypassing system package resolution.
4. **Header Namespace Pollution via `add-prefix-glob`:** `portable.mk` automatically expands `add-prefix-glob := ../nd-* ../axil-nd` into dozens of `-I` and `-L` flags, flattening the submodule dependency graph and allowing modules to include private, non-canonical headers (`config.h`, `player.h`, `st.h`, `mcp.h`, `view.h`, `noise.h`) from sibling directories.
5. **Cross-Module Symbol Leaks Bypassing XY Bus:** The NeverDark daemon (`external/axil-nd`) and bus (`external/nd-bus`) export mutable global arrays (`nd_hds[]`), global function pointers (`nd_dwritef`), and raw C symbols (`hd_resolve`, `nd_evt_*`, `euid`). Multiple domain modules import these symbols directly, creating hard ELF dynamic linker dependencies that circumvent the libxylem XY bus.
6. **Host-Tenant Source Coupling:** `mods/nd/Makefile` reaches across repository boundaries to directly compile tenant UI source files (`external/nd-*/ux/*_ui.c`) into `nd.so` and `nd.wasm`.
7. **Asset & Capability Redundancy:** Multiple identical copies of browser bridge scripts (`axil-bridge.js`, `axil-ws-bridge.js`, `axil-tty-bridge.js`, `tty-bridge.js`) and transport headers (`axil-ws.h`) exist across the codebase due to capability naming drift.

---

## 2. Section 1: Runtime Linker & Search Path Manipulation (`LD_LIBRARY_PATH` & `RPATH`)

### 2.1 In-Tree `LD_LIBRARY_PATH` Injection

In `old-main`, `start.sh:6-7` explicitly documented the system invariant:
> *"# No LD_LIBRARY_PATH: every library resolves from the system path ($PREFIX/lib, mk/portable.mk) -- the same authority the link uses, so dev runs what prod runs. `make install-libs` publishes a rebuilt library there."*

Across recent commits, this rule was systematically overridden across 8 files:

| File Location | Line(s) | Injected Code / Mechanism | Invariant Violated |
|---|---|---|---|
| `start.sh` | 14–15 | `in_tree_nd_libs=$(printf ':%s' "$SCRIPT_DIR"/external/nd-*/lib)`<br>`export LD_LIBRARY_PATH="$SCRIPT_DIR/external/libcorm/lib:...${in_tree_nd_libs}:${LD_LIBRARY_PATH:-/usr/lib}"` | Overrides system library resolution; masks uninstalled or stale system libraries. |
| `scripts/run-with-server.sh` | 31–32 | `in_tree_nd_libs=$(printf ':%s' "$REPO_ROOT"/external/nd-*/lib)`<br>`export LD_LIBRARY_PATH="${REPO_ROOT}/external/libcorm/lib:...${in_tree_nd_libs}:${LD_LIBRARY_PATH:-/usr/lib}"` | Test runner executes against local tree libraries instead of installed packages. |
| `Makefile` | 402–403 | `@in_tree_nd_libs=$$(printf ':%s' "$$PWD"/external/nd-*/lib);`<br>`export LD_LIBRARY_PATH="$$PWD/external/axil-nd/lib:...$${in_tree_nd_libs}:$${LD_LIBRARY_PATH:-}";` | `run-universe` target synthesizes 30-path in-tree lookup. |
| `scripts/seed-nd-universe.sh` | 38 | `export LD_LIBRARY_PATH="$AXIL_ND_DIR/lib:$REPO_ROOT/external/axil/lib...${in_tree_nd_libs}:${LD_LIBRARY_PATH:-}"` | Seeder daemon runs with in-tree library search overrides. |
| `tests/e2e/test_full_universe.sh` | 34 | `export LD_LIBRARY_PATH="$AXIL_ND_DIR/lib:$REPO_ROOT/external/axil/lib...${in_tree_nd_libs}:${LD_LIBRARY_PATH:-}"` | Integration test daemon runs with in-tree library overrides. |
| `scripts/migrate-store-fs-to-tok.sh` | 8 | `export LD_LIBRARY_PATH="$(pwd)/external/liblemma/lib:$(pwd)/external/libcorm/lib:${LD_LIBRARY_PATH}"` | Migration CLI requires manual LD_LIBRARY_PATH. |
| `tests/unit/run-nd-ui-slot.sh` | 29 | `LD_LIBRARY_PATH="$SRC_DIR/external/libbud/lib:$LD_LIBRARY_PATH" "$BUILD_DIR/nd_ui_slot_test"` | Unit test harnesses require in-tree library paths. |
| `external/liblemma/Makefile` | 20–25 | `LD_LIBRARY_PATH=$(abspath lib) ./bin/lemma_test` | Submodule test target forces tree-local library path. |

**Architectural Consequence:**
In-tree `LD_LIBRARY_PATH` creates a split-authority problem. As documented in `scripts/check-no-tree-rpath.sh`:
> *"A DT_RUNPATH into the source tree makes the tree the runtime authority for that library while xy_load("lib...") still resolves the same soname from the system search path: the loader then maps two distinct files as two distinct objects, only one of which gets the xy context bound into it. Calls made through the other copy dispatch on a zero context and fault."*

### 2.2 Hardcoded User Directories in `DT_RUNPATH`

A forensic scan using `readelf -d` across all compiled ELF objects in the repository revealed **44 binaries and shared libraries** with hardcoded `DT_RUNPATH` entries pointing to `/home/quirinpa/.local/lib` or `/home/quirinpa/site/...`:

```text
./external/libcorm/lib/libcorm.so:                  RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/lib/librec_axis_fold.so:         RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/lib/librec_axis_mock.so:         RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/lib/librec_axis_plain.so:        RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/lib/librec_axis_probe.so:        RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/lib/libstub.so:                  RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/lib/libzed.so:                   RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/bench_multivalue:            RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/bench_rec:                   RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/corm:                        RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/cormd:                       RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/cormr_test:                  RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/rec_axis_bench:              RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/rec_axis_store_test:         RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/rec_axis_test:               RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/rec_cli_test:                RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/rec_test:                    RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/test:                        RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/test_extended:               RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/test_multivalue:             RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/test_persist:                RUNPATH: [/home/quirinpa/.local/lib]
./external/libcorm/bin/test_record:                 RUNPATH: [/home/quirinpa/.local/lib]
./external/libjoint/lib/libjoint.so:                RUNPATH: [/home/quirinpa/.local/lib]
./external/libjoint/bin/joint_axis_roundtrip_test:  RUNPATH: [/home/quirinpa/.local/lib]
./external/libjoint/bin/joint_axis_store_test:      RUNPATH: [/home/quirinpa/.local/lib]
./external/libjoint/bin/test:                       RUNPATH: [/home/quirinpa/.local/lib]
./external/libjoint/bin/test_extended:              RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/lib/liblemma.so:                RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/bin/lemma_axis_rebuild_test:    RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/bin/lemma_axis_roundtrip_test:  RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/bin/lemma_axis_store_test:      RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/bin/lemma_prop_test:            RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/bin/lemma_test:                 RUNPATH: [/home/quirinpa/.local/lib]
./external/liblemma/bin/lemma_tok_test:             RUNPATH: [/home/quirinpa/.local/lib]
./external/libsepal/lib/libsepal.so:                RUNPATH: [/home/quirinpa/.local/lib]
./build/test/auth_disk_permissions_test:            RUNPATH: [/home/quirinpa/site/external/liblemma/lib]
./build/test/auth_group_permissions_test:           RUNPATH: [/home/quirinpa/site/external/liblemma/lib]
./external/libhyle/target/debug/deps/* (4 files):   RUNPATH: [/home/quirinpa/site/external/libhyle/../libcorm/lib:...]
./external/mm/bin/engine_test:                      RUNPATH: [/home/quirinpa/site/external/libqmap/lib:/home/quirinpa/site/external/libstoma/lib]
./external/mm/bin/mm:                               RUNPATH: [/home/quirinpa/site/external/libqmap/lib:/home/quirinpa/site/external/libstoma/lib]
./mods/common/lib/liblemma.so:                      RUNPATH: [/home/quirinpa/.local/lib]
```

**Root Cause:**
`external/libcorm/Makefile:35`, `external/liblemma/Makefile:14`, `external/libjoint/Makefile:14`, and `external/libsepal/Makefile:9` explicitly contain:
`LDFLAGS += -Wl,-rpath,/home/quirinpa/.local/lib`
This breaks distribution packaging, chroot builds, and multi-user environments.

---

## 3. Section 2: Makefile Portability Breakages & Forced Search Paths

### 3.1 Hardcoded Absolute Workspace Paths

In `external/libjoint` and `external/libsepal`:
- **`external/libjoint/Makefile:11–14`**:
  ```makefile
  CFLAGS += -I/home/quirinpa/site/external/libcorm/include
  CFLAGS += -I/home/quirinpa/site/external/liblemma/include
  LDFLAGS += -L/home/quirinpa/site/external/liblemma/lib -Wl,-rpath,/home/quirinpa/.local/lib
  ```
- **`external/libsepal/Makefile:7–9`**:
  ```makefile
  CFLAGS += -I/home/quirinpa/site/external/libcorm/include
  CFLAGS += -I/home/quirinpa/site/external/liblemma/include
  LDFLAGS += -L/home/quirinpa/site/external/liblemma/lib -Wl,-rpath,/home/quirinpa/.local/lib
  ```
- **`external/liblemma/Makefile:13`**:
  ```makefile
  CFLAGS += -I/home/quirinpa/site/external/libcorm/include
  ```

**Impact:**
Submodules cannot be built standalone outside `/home/quirinpa/site`. A clone on any other user account or CI machine immediately errors on missing include paths.

### 3.2 Forced Sibling Linkages in `external/nd-*/Makefile`

Six domain module Makefiles contain hardcoded relative library search paths:

- `external/nd-obj/Makefile:6`:
  `LDLIBS-libnd-obj := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-session/Makefile:6`:
  `LDLIBS-libnd-session := -lxylem -lcorm -lqsys -laxil-tty -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-view/Makefile:5`:
  `LDLIBS-libnd-view := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-world/Makefile:6`:
  `LDLIBS-libnd-world := -lxylem -lcorm -lislet -lqsys -L../axil-nd/lib -laxil-nd -Wl,--no-as-needed -L../nd-bus/lib -lnd-bus -Wl,--as-needed`
- `external/nd-mcp/Makefile:6`:
  `LDLIBS-libnd-mcp := -lxylem -lcorm -lqsys -L../axil-nd/lib -laxil-nd`
- `external/nd-speech/Makefile:5`:
  `LDLIBS-libnd-speech := -lxylem -lcorm -lqsys -L../axil-nd/lib -laxil-nd`

**Impact:**
`check-no-tree-rpath.sh` forbids `-L` flags in build inputs because forced `-L` overrides system authority. These modules cannot be built against system-installed `/usr/lib/libaxil-nd.so`.

### 3.3 `add-prefix-glob` Include & Library Injection

In `external/mk/portable.mk:31–33, 73–77`:
```makefile
add-prefix-dirs := $(shell [ -n "${add-prefix-glob}" ] && for p in ${add-prefix-glob}; do [ -d "$$p" ] && echo "$$p"; done || true)
prefix := ${pwd} ${add-prefix} ${add-prefix-dirs} ${prefix-${uname}} ...
CFLAGS += ${prefix:%=-I%/include}
LDFLAGS += ${prefix:%=-L%/lib}
```
In `external/axil-nd/Makefile:21`: `add-prefix-glob := ../nd-*`  
In `external/nd-*/Makefile`: `add-prefix-glob := ../nd-* ../axil-nd`

**Impact:**
Every module automatically receives `-I../nd-*/include` and `-L../nd-*/lib` for all 27 sibling submodules. This allows modules to accidentally include internal headers from unrelated siblings without declaring them.

---

## 4. Section 3: Cross-Module Encapsulation & Boundary Leaks

### 4.1 Bypassed XY Bus via Exported Global Variables & Function Pointers

Rule 3 of `AGENTS.md` mandates:
> *"XY is the only cross-.so boundary. Unconditional XY_DECL in caller header, XY_IMPL in owner, no X_IMPL guards — two-header rule... static by default, never extern or cross-module #include '*.c'."*

However, the daemon and bus export mutable C symbols directly across dynamic library boundaries:

1. **`extern unsigned nd_hds[];`** (`external/axil-nd/include/nd/law-types.h:20`, defined in `law_store.c:49`):
   Instead of encapsulating Corm handles behind XY accessors, `nd_hds` is an exported global array.
   In `external/nd-obj/include/nd/obj-types.h:9–15`:
   ```c
   #define obj_hd nd_hds[HD_OBJ]
   #define skel_hd nd_hds[HD_SKEL]
   #define contents_hd nd_hds[HD_CONTENTS]
   #define obs_hd nd_hds[HD_OBS]
   #define ent_hd nd_hds[HD_ENTITY]
   #define drop_hd nd_hds[HD_DROP]
   #define adrop_hd nd_hds[HD_ADROP]
   ```
   In `external/nd-session/include/nd/session-types.h:9`:
   ```c
   #define player_hd nd_hds[HD_PLAYER]
   ```
   Forensic symbol inspection via `nm -u` confirms that `libnd-obj.so`, `libnd-session.so`, `libnd-speech.so`, `libnd-view.so`, and `libnd-world.so` directly import the undefined symbol `nd_hds`.
2. **`nd_dwritef` Function Pointer** (`external/axil-nd/include/nd/law.h:135`, defined in `law_io.c:16`):
   Formatted I/O dispatches through an exported function pointer symbol `nd_dwritef` imported by `libnd-obj.so`, `libnd-session.so`, `libnd-speech.so`, and `libnd-world.so`.
3. **`hd_resolve` Function** (`external/axil-nd/src/law_store.c:64`):
   Exported C symbol imported by `libnd-obj.so`, `libnd-session.so`, `libnd-view.so`, and `libnd-world.so`.
4. **`nd_evt_*` Event Symbols** (`external/nd-bus/src/nd_events.c`):
   Exported by `libnd-bus.so` and directly imported by `libnd-session.so` (`nd_event_announce`, `nd_evt_auth`, `nd_evt_new_player`, `nd_evt_vim`), `libnd-obj.so` (`nd_evt_after_enter`, `nd_evt_before_leave`, `nd_evt_status`), `libnd-view.so` (`nd_evt_empty_tile`, `nd_evt_view_flags`), and `libnd-world.so` (`nd_evt_move`, `nd_evt_noise`, `nd_evt_spawn`).
5. **`extern int euid;`** (`external/nd-session/include/config.h:6`, defined in `session.c:70`).

**Impact:**
These non-XY symbol dependencies are the direct reason why domain module Makefiles must link `-laxil-nd` and `-lnd-bus`. They cannot operate as independent, dynamically decoupled XY plugins.

### 4.2 Private Header Leaks Across Domain Modules

Due to `add-prefix-glob`, **53 cross-module header inclusions** bypass public `<nd/...>` headers and include private headers directly from sibling directories:

- `external/nd-obj/src/entity.c`: `#include "config.h"`, `"mcp.h"`, `"params.h"`, `"st.h"`, `"view.h"`
- `external/nd-session/src/session.c`: `#include "config.h"`, `"mcp.h"`, `"player.h"`, `"st.h"`, `"view.h"`
- `external/nd-world/src/map.c`: `#include "config.h"`, `"st.h"`
- `external/nd-world/src/spacetime.c`: `#include "st.h"`, `"config.h"`, `"noise.h"`, `"params.h"`, `"player.h"`, `"view.h"`, `"mcp.h"`
- `external/nd-speech/src/look.c`, `speech.c`, `wiz.c`: `#include "config.h"`, `"player.h"`, `"st.h"`, `"mcp.h"`
- `external/nd-view/src/view.c`: `#include "view.h"`, `"mcp.h"`, `"noise.h"`, `"params.h"`

### 4.3 Host-to-Tenant Source Compilation in `mods/nd/Makefile`

In `mods/nd/Makefile:6–11`:
```makefile
ND_UX_MODULES = \
    $(REPO_ROOT)/external/nd-attr/ux/attr_ui.c \
    $(REPO_ROOT)/external/nd-equip/ux/equip_ui.c \
    $(REPO_ROOT)/external/nd-spell/ux/spell_ui.c \
    $(REPO_ROOT)/external/nd-fight/ux/fight_ui.c \
    $(REPO_ROOT)/external/nd-mortal/ux/mortal_ui.c
```
The site host module compiles tenant source files directly into `nd.so` and `nd.wasm`. This creates compile-time coupling: if any tenant submodule alters its internal `ux/*_ui.c` structure, the site host build breaks.

### 4.4 In-Tree Relative Data Probing in `external/axil-nd/src/law_data.c`

In `law_data.c:74–81`:
```c
snprintf(path, sizeof(path), "external/%s/data/%s.dsv", module, table);
if ((*out = fopen(path, "r"))) return 1;
snprintf(path, sizeof(path), "../%s/data/%s.dsv", module, table);
if ((*out = fopen(path, "r"))) return 1;
snprintf(path, sizeof(path), "data/%s/%s.dsv", module, table);
if ((*out = fopen(path, "r"))) return 1;
snprintf(path, sizeof(path), AXIL_PREFIX "/share/axil-nd/%s/%s.dsv", module, table);
if ((*out = fopen(path, "r"))) return 1;
```
`nd_table_open` probes relative working directories before checking `$AXIL_PREFIX/share/...`. Running `axil` from arbitrary working directories can load unverified local files rather than canonical data.

---

## 5. Section 4: Asset, Client Transport & Capability Redundancy

1. **Duplicate JavaScript Bridge Files:**
   - `htdocs/axil-bridge.js` (244 lines) and `htdocs/axil-ws-bridge.js` (244 lines) are identical files duplicated in the web root.
   - `external/axil/htdocs/axil-bridge.js` is an identical third copy.
   - `htdocs/axil-tty-bridge.js` (217 lines) and `htdocs/tty-bridge.js` (217 lines) are identical copies created to resolve capability naming drift.
2. **Capability Naming Drift:**
   - Fallback SSR in `external/axil-nd/src/http.c:75` specifies `data-capabilities="axil-ws tty lamina bud"`.
   - Full SSR in `mods/nd/ux/nd_ui.c:888` specifies `data-capabilities="axil-ws axil-tty lamina webgl xylem"`.
   - Dynamic asset loader in `bud-client.js` requests `/${cap}-bridge.js`, causing 404 errors when `tty` vs `axil-tty` mismatch.
3. **Duplicate Transport Headers:**
   - `include/ttypt/axil-ws.h` and `external/axil/include/ttypt/axil-ws.h` and `external/axil-tty/include/ttypt/axil-ws.h` are three identical copies of the WebSocket import declarations.

---

## 6. Section 5: Exhaustive Submodule Commit Pointer Inventory (34 Submodules)

The following table documents all 34 Git submodules modified between `old-main` (`b1db3f44`) and `HEAD` (`d2f0608`):

| # | Submodule Path | Old Hash | New Hash | Commit Subject | Diffstat | Architectural Impact |
|---|---|---|---|---|---|---|
| 1 | `.pi/extensions/pi-mm` | `94a44066` | `3e33d221` | Clip assistant response to 500 chars and set default judgeTimeoutMs to 15s | 77 files, +20301/-1085 | HTOK v3 binary transition, vector cache, model telemetry. |
| 2 | `.pi/extensions/pi-quest` | `faa0a5c4` | `572f27dd` | Implement deterministic trigger word families, offline fail-closed safety, and /quest doctor | 9 files, +423/-30 | Fail-closed offline safety, quest packaging zip tools. |
| 3 | `external/axil` | `cfc2dedf` | `be40a0f7` | feat(ws): provide canonical browser axil-bridge.js and wasm allowed imports | 4 files, +354/-21 | Single-socket persistent WebSocket multiplexer, port cookie isolation. |
| 4 | `external/axil-auth` | `eae488b5` | `f0f8d102` | fix(axil-auth): support port-qualified and configured cookie names | 1 file, +32/-8 | Scoped `QSESSION_<port>` cookie isolation for multi-daemon dev. |
| 5 | `external/axil-nd` | `058c2d33` | `76ccffe0` | feat(nd): pure-C dynamic fallback SSR, canonical BCP protocol, pluggable viewports, and extensible UI slots | 15 files, +1295/-17 | Canonical scene/BCP/slot headers; fallback SSR; `nd_hds` & `nd_dwritef` exports. |
| 6 | `external/axil-tty` | `4fe45d17` | `ebb525a7` | feat(bridge): provide pure tty terminal capability bridge consuming axil transport | 4 files, +314/-0 | Pure TTY capability bridge (`axil-tty-bridge.js`) consuming `axil` bus. |
| 7 | `external/libbud` | `bde235fc` | `a73bd34e` | feat(bud): use namespaced bud import module and capability allowlist | 4 files, +30/-17 | Namespaced host imports (`bud.*`), `patch-listener` diff emission. |
| 8 | `external/libcorm` | `b38cbf94` | `def392b1` | feat(wasm): add WASM linear memory arena shims and host persistence imports | 6 files, +330/-50 | `corm_blob_type()` for HTOK; hardcoded `DT_RUNPATH` to `~/.local/lib`. |
| 9 | `external/libhyle` | `5950b42e` | `e89a4eed` | feat(hyle): add binary token array storage and rotating ring buffer for lazy SSR decoding | 3 files, +210/-12 | Lazy field decoding ring buffer; binary token ingestion. |
| 10 | `external/libhyle-source` | `89fdf207` | `6d887c46` | feat(hyle-source): add store_tok, companion data.tok, record.tok container writing, and hyle-tok tool | 9 files, +572/-5 | Tokenized container driver (`store_tok.c`), companion `.tok` writing. |
| 11 | `external/libjoint` | `1de0c4cb` | `e47d3924` | feat(joint): export rec_axis_store_typed for HTOK date ingestion | 4 files, +118/-20 | HTOK date parsing; dynamic `dlsym` for lemma decoder; hardcoded `/home/` path. |
| 12 | `external/liblemma` | `d0a2c250` | `b2c929d1` | feat(lemma): implement lossless tokenizer, 3-gram index, and eliminate doc_hd | 8 files, +33302/-205 | Varint HTOK tokenizer (`tok.c`); hardcoded `DT_RUNPATH` to `~/.local/lib`. |
| 13 | `external/libsepal` | `60e3378a` | `983e1edd` | feat(sepal): export rec_axis_store_typed for HTOK embedding ingestion | 3 files, +339/-2 | HTOK embedding ingestion; in-memory vector rebuild; hardcoded `/home/` path. |
| 14 | `external/libxylem` | `cca111f7` | `5938b5a2` | feat(wasm): in-WASM Xylem core dispatcher, XY_LISTENER_TAG, and dynamic loader bridge | 3 files, +362/-0 | In-WASM Xylem core (`libxylem-wasm.c`), dynamic loader bridge, `XY_LISTENER_TAG`. |
| 15 | `external/nd-attr` | `23930a65` | `1763bd17` | feat(attr): standardize documentation, add character sheet and HP/MP gauges, and register stats command | 5 files, +414/-0 | Isomorphic `attr_ui.c` (character sheet, vitality gauges via `ND_UI_GLUE`). |
| 16 | `external/nd-biome` | `40858ebf` | `4f73f549` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 4 files, +64/-20 | Standardized documentation and asset packaging. |
| 17 | `external/nd-class` | `26946c50` | `20495133` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 4 files, +41/-5 | Standardized documentation and asset packaging. |
| 18 | `external/nd-core` | `d8c23aae` | `629416ff` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 1 file, +19/-0 | Standardized XY lifecycle hook documentation. |
| 19 | `external/nd-drink` | `883e378c` | `74923483` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 1 file, +7/-0 | Standardized documentation. |
| 20 | `external/nd-equip` | `b4c62c38` | `7f2100d8` | feat(equip): standardize documentation, declarative BCP unpacker, and paper doll UI modal | 5 files, +427/-2 | Isomorphic `equip_ui.c` (4x5 paper doll modal via `ND_UI_GLUE`). |
| 21 | `external/nd-fight` | `533e22d2` | `e3c36b40` | feat(fight): standardize documentation and add combat target HUD overlay component header | 3 files, +120/-0 | Isomorphic `fight_ui.c` (combat target HUD via `ND_UI_GLUE`). |
| 22 | `external/nd-level` | `beea5128` | `43f81047` | feat(level): standardize documentation, introduce level-types.h, and enforce two-header separation | 3 files, +32/-0 | Extracted `include/nd/level-types.h` (two-header compliance). |
| 23 | `external/nd-mob` | `6b867979` | `2e854ac6` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 4 files, +76/-23 | Documented AI spawn tables and BCP entity streams. |
| 24 | `external/nd-mortal` | `7e3445b1` | `744550bc` | feat(mortal): standardize documentation, introduce mortal-types.h, and enforce two-header separation | 6 files, +119/-12 | Extracted `include/nd/mortal-types.h`, added `ux/mortal_ui.c`. |
| 25 | `external/nd-other` | `00ab4dec` | `1f6fd331` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 1 file, +4/-0 | Documented generic entity fallbacks. |
| 26 | `external/nd-plant` | `c83be4f8` | `f6c09366` | feat(plant): standardize documentation, extract plant-types.h, and enforce two-header separation | 6 files, +86/-15 | Extracted `plant-types.h`, migrated `plants.dsv` to canonical schema. |
| 27 | `external/nd-race` | `3b189ff4` | `70483dd1` | feat(race): standardize documentation, introduce race-types.h, and enforce two-header separation | 6 files, +67/-15 | Extracted `race-types.h`, added `races.dsv`. |
| 28 | `external/nd-seat` | `dd71d458` | `af613c5d` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 1 file, +7/-0 | Documented sitting state. |
| 29 | `external/nd-shop` | `b03a3c2f` | `893d6d11` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 2 files, +15/-4 | Documented merchant interactions. |
| 30 | `external/nd-spell` | `6c3610d2` | `7eccd662` | feat(spell): standardize documentation, establish public headers, spellbook modal UI, and enforce two-header separation | 9 files, +698/-55 | Extracted `spell-types.h` and `spell.h`, created `ux/spell_ui.c` (grimoire modal). |
| 31 | `external/nd-stone` | `47a354c7` | `6abefa8d` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 4 files, +32/-8 | Documented mineral tables and geology mechanics. |
| 32 | `external/nd-vanilla` | `822312b1` | `77988e9c` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 1 file, +18/-0 | Documented default player spawning. |
| 33 | `external/nd-wts` | `b56c521c` | `f387fa55` | docs: standardize README with man-src, art, BCP wire events, and modder guide | 4 files, +59/-21 | Documented vocabulary lookup. |
| 34 | `external/pi-instruct` | `be222c10` | `bada519b` | Streamline setup guide into fast 3-minute quickstart | 3 files, +89/-74 | Setup documentation streamlining. |

---

## 7. Section 6: Comprehensive Risk Matrix & Recommended Remediation Roadmap

| # | Vulnerability / Anti-Pattern | Severity | Manifestation | Concrete Remediation Action |
|---|---|---|---|---|
| 1 | **Hardcoded User Workspace Paths in Makefiles** | **Critical** | `external/libjoint/Makefile:11–14`<br>`external/libsepal/Makefile:7–9`<br>`external/liblemma/Makefile:13` | Replace `/home/quirinpa/site/external/...` with portable `${prefix}` inclusions; eliminate absolute path references. |
| 2 | **User Directory in `DT_RUNPATH`** | **Critical** | 44 compiled ELF objects in `libcorm`, `liblemma`, `libjoint`, `libsepal` contain `/home/quirinpa/.local/lib`. | Remove `-Wl,-rpath,/home/quirinpa/.local/lib` from all submodule Makefiles. Build against system libraries in `$PREFIX/lib`. |
| 3 | **In-Tree `LD_LIBRARY_PATH` Injections** | **High** | `start.sh:14–15`<br>`scripts/run-with-server.sh:31–32`<br>`Makefile:402–403` | Remove in-tree `LD_LIBRARY_PATH` exports. Require running `make install-libs` so that dev and test execute against `/usr/lib` or `$PREFIX/lib`. |
| 4 | **Bypassed XY Boundary via Exported Data Symbols** | **High** | `nd_hds` in `law_store.c:49` and `law-types.h:20`<br>`nd_dwritef` in `law_io.c:16` and `law.h:135` | Encapsulate Corm table access behind XY hooks (`nd_open`, `nd_get`, `nd_put`, `nd_del`). Eliminate `extern unsigned nd_hds[]`. |
| 5 | **Forced Sibling `-L` Linkages in Domain Modules** | **Medium** | `external/nd-obj/Makefile:6`<br>`external/nd-session/Makefile:6`<br>`external/nd-view/Makefile:5`<br>`external/nd-world/Makefile:6`<br>`external/nd-mcp/Makefile:6`<br>`external/nd-speech/Makefile:5` | Remove forced `-L../axil-nd/lib` and `-L../nd-bus/lib`. Link dynamically against system-installed `-laxil-nd` and `-lnd-bus`. |
| 6 | **Unbounded Include Namespace Pollution via `add-prefix-glob`** | **Medium** | `external/mk/portable.mk:31–33`<br>`external/nd-*/Makefile` | Restrict `add-prefix` to explicit dependencies rather than wildcard `../nd-*`. Enforce public `<nd/...>` includes. |
| 7 | **Host-to-Tenant Direct Source Compilation** | **Medium** | `mods/nd/Makefile:6–11` compiles `external/nd-*/ux/*_ui.c` | Standardize client UI registration into standalone WASM plugins or loadable modules rather than compiling foreign `.c` sources. |
| 8 | **Working-Directory Relative Data Probing** | **Low** | `external/axil-nd/src/law_data.c:74–81` probes `"external/%s/..."` and `"../%s/..."` | Prioritize `$AXIL_ND_DATA`, then `$AXIL_PREFIX/share/axil-nd/...`. Disable relative fallback paths in production builds. |
| 9 | **Duplicate JavaScript Bridges & Header Declarations** | **Low** | `axil-bridge.js` vs `axil-ws-bridge.js`<br>`axil-tty-bridge.js` vs `tty-bridge.js`<br>`axil-ws.h` duplicated in 3 repos | Single-source `axil-ws-bridge.js` in `external/axil` and `axil-tty-bridge.js` in `external/axil-tty`. Symlink into `htdocs/`. Standardize `data-capabilities="axil-ws axil-tty lamina bud"`. |
