# Unified Unbounded Shared-Memory WebAssembly Architecture

**Scope:** `external/mk`, `external/libstele`, `external/libcorm`, `external/liblemma`, `external/libsepal`, `external/libjoint`, `external/libislet`, `external/libhyle`, `external/libhyle-source`, `external/libxylem`, `external/libpitch`, `external/libbud`, `external/liblamina`, `external/libchaff`, `external/libareole`, `external/axil-auren`, and `site`.

---

## 1. Root-Cause Diagnosis

An end-to-end binary and linker-map audit (`wasm-objdump -x/-d` and `wasm-ld --Map`) of all colocated WebAssembly modules across `external/*` identified five structural defects that prevented reliable multi-module execution and constrained in-browser LLM workloads:

### 1.1 The 256 MB Hard Memory Ceiling
- `external/libstele/Makefile` passed `-Wl,--max-memory=268435456` (256 MB), and `external/libstele/htdocs/stele-client.js` + `external/libstele/npm/dist/index.js` hardcoded `new WebAssembly.Memory({ initial: 256, maximum: 4096 })` (4,096 pages = 256 MB).
- Client-side LLM inference (GGUF weights, KV caches, tokenizer state, and vector/lexical recall indices) requires dynamic linear memory expansion up to the full `wasm32` 4 GB limit (65,536 pages).

### 1.2 Dual-`dlmalloc` Heap Corruption Across Shared Memory
- While `@tty-pt/stele` is intended to be the single allocator on the shared `WebAssembly.Memory`, downstream C sources (`libcorm/src/rec.c`, `libpitch/src/*.c`, `libxylem/src/libxylem-wasm.c`, `libbud/src/libbud.c`, `liblamina/npm/wasm/lamina_wasm.c`, `libareole/src/areole.c`, `axil-auren/npm/wasm/auren_wasm.c`) call standard `<stdlib.h>` `malloc`, `calloc`, `realloc`, and `free`.
- Because `wasi-libc`'s `libc.a` provides `dlmalloc.o` and `sbrk.o`, `wasm-ld` statically linked a **separate, private copy of `dlmalloc`** into each downstream `.wasm` module instead of importing `env.malloc`/`env.free` from `stele`.
- Each private `dlmalloc` maintained its own `_gm_` / `mparams` struct in low memory and started its heap at its own `__heap_base`, overwriting `stele`'s stack, `.bss`, and allocator metadata on the first allocation.

### 1.3 `stele.wasm` Heap vs. Downstream `--global-base` Collision
- `stele.wasm` was linked at the default `--global-base=1024`:
  - `0x0400 .. 0x1940` (`1,024 .. 6,464`): `stele` `.rodata` and `.bss` (`g_registered`, `g_slots`, `g_stl_listeners`)
  - `0x1944 .. 0x1B34` (`6,468 .. 6,964`): `wasi-libc` `dlmalloc` state (`_gm_` and `mparams`)
  - `0x1B34 .. 0x11B30` (`6,964 .. 72,496`): `stele`'s 64 KB stack (`__stack_pointer = 72496`)
  - `0x11B30+` (`72,496+`): `stele`'s `dlmalloc` heap (`__heap_base`)
- Any downstream module linked at default `--global-base=1024` (such as `lemma`, `corm`, `sepal`, `joint`, `islet`, `hyle`) placed its `.rodata`/`.data`/`.bss` and `memory.fill` start function right on top of `stele`'s `.bss`, `_gm_`, and stack (`1024 .. 99344`).
- Furthermore, because `stele`'s heap started at `72,496` and grew upward, placing downstream modules at `--global-base=1048576` (1 MB) or `2097152` (2 MB) meant `stele`'s heap would grow directly through those modules' static data and stack bands once heap usage exceeded 1 MB.

### 1.4 The Makefile `+=` Trap Before `include ../mk/include.mk`
- `external/mk/include.mk` defined `WASM_LDFLAGS ?= -mexec-model=reactor -Wl,--gc-sections -Wl,--no-entry -Wl,--allow-undefined`.
- Makefiles that wrote `WASM_LDFLAGS += -Wl,--import-memory` *before* `include ../mk/include.mk` defined `WASM_LDFLAGS` prior to `?=` evaluation, discarding `-mexec-model=reactor -Wl,--no-entry` and producing WASI command modules (`_start`) instead of reactors (`_initialize`).

### 1.5 Detached `ArrayBuffer` Views on `memory.grow()`
- Under dynamic heap growth (`sbrk` $\rightarrow$ `memory.grow()`), WebAssembly detaches the previous `memory.buffer` instance.
- Any JavaScript wrapper that captures `const mem = substrate.memory.buffer` *before* calling `wasm.exports.malloc()` or a C function that allocates will throw `TypeError: Cannot perform Construct on a detached ArrayBuffer` whenever that allocation grows linear memory.

---

## 2. Target Architecture & Five Core Invariants

### Invariant 1: Unbounded (4 GB `wasm32`) Growable Linear Memory with `[0 .. 2 MB)` Static Reservation and `[2 MB .. 4 GB)` Heap
- **`stele.wasm` is linked with `-Wl,--global-base=2097152` (2 MB), `-Wl,--initial-memory=16777216` (16 MB), and NO `--max-memory` cap.**
  - `stele.wasm`'s `.rodata`, `.bss` (`_gm_`, `mparams`, `g_slots`, `g_stl_listeners`), and 64 KB stack reside at **`2,097,152 .. 2,168,640` (`0x200000 .. 0x211740`)**.
  - `stele.wasm`'s `dlmalloc` `__heap_base` starts at **`2,168,640` (~2.07 MB)** and grows **upward** unimpeded up to **4 GB (`0xFFFF0000`)**.
- **All downstream WASM reactor modules reside in fixed, non-overlapping `--global-base` bands strictly below 2 MB (`[65,536 .. 2,097,152)`):**

| Slot | Library (`ONELIB`) | `--global-base` | Stack Size | Reserved Band Range | Band Size |
| :--- | :--- | :--- | :--- | :--- | :--- |
| 0 | Reserved low memory | `0` | — | `0 .. 65,536` (`0x000000 .. 0x010000`) | 64 KB |
| 1 | `corm` | `65536` | `32768` | `65,536 .. 196,608` (`0x010000 .. 0x030000`) | 128 KB |
| 2 | `lemma` | `196608` | `32768` | `196,608 .. 327,680` (`0x030000 .. 0x050000`) | 128 KB |
| 3 | `sepal` | `327680` | `32768` | `327,680 .. 458,752` (`0x050000 .. 0x070000`) | 128 KB |
| 4 | `joint` | `458752` | `32768` | `458,752 .. 720,896` (`0x070000 .. 0x0B0000`) | 256 KB |
| 5 | `islet` | `720896` | `32768` | `720,896 .. 851,968` (`0x0B0000 .. 0x0D0000`) | 128 KB |
| 6 | `hyle` | `851968` | `32768` | `851,968 .. 983,040` (`0x0D0000 .. 0x0F0000`) | 128 KB |
| 7 | `xylem` | `983040` | `32768` | `983,040 .. 1,114,112` (`0x0F0000 .. 0x110000`) | 128 KB |
| 8 | `hyle-source` | `1114112` | `32768` | `1,114,112 .. 1,245,184` (`0x110000 .. 0x130000`) | 128 KB |
| 9 | `pitch` / `transp` | `1245184` | `32768` | `1,245,184 .. 1,376,256` (`0x130000 .. 0x150000`) | 128 KB |
| 10 | `bud` | `1376256` | `32768` | `1,376,256 .. 1,507,328` (`0x150000 .. 0x170000`) | 128 KB |
| 11 | `lamina` | `1507328` | `32768` | `1,507,328 .. 1,638,400` (`0x170000 .. 0x190000`) | 128 KB |
| 12 | `chaff` / `noise` | `1638400` | `32768` | `1,638,400 .. 1,769,472` (`0x190000 .. 0x1B0000`) | 128 KB |
| 13 | `areole` / `chunk` | `1769472` | `32768` | `1,769,472 .. 1,900,544` (`0x1B0000 .. 0x1D0000`) | 128 KB |
| 14 | `auren` | `1900544` | `32768` | `1,900,544 .. 2,031,616` (`0x1D0000 .. 0x1F0000`) | 128 KB |
| 15 | Reserved expansion | `2031616` | `32768` | `2,031,616 .. 2,097,152` (`0x1F0000 .. 0x200000`) | 64 KB |
| **Heap** | **`stele` (kernel + `dlmalloc`)** | **`2097152`** | **`65536`** | **`2,097,152 .. 4,294,901,760` (`0x200000 .. 0xFFFF0000`)** | **2 MB .. 4 GB** |

### Invariant 2: Single Allocator via Linker `--wrap` Shim (`external/mk/wasm_alloc_shim.c`)
- `stele.wasm` exports `malloc`, `free`, `calloc`, and `realloc`.
- `WasmSubstrate` in `@tty-pt/stele` exposes `.malloc(size)`, `.free(ptr)`, `.calloc(nmemb, size)`, and `.realloc(ptr, size)` and injects all four into `env` when instantiating downstream modules via `loadWasmModule` / `loadWasmModuleAsync`.
- `external/mk/wasm_alloc_shim.c` defines `__wrap_malloc`, `__wrap_free`, `__wrap_calloc`, and `__wrap_realloc` forwarding to `env.malloc`, `env.free`, `env.calloc`, and `env.realloc`.
- `external/mk/include.mk` links every downstream module (`${bname} != libstele`) with `-Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc` and `${MPATH}/wasm_alloc_shim.c`, so `wasi-libc`'s `dlmalloc.o` and `sbrk.o` are never linked into downstream modules.

### Invariant 3: Non-Clobberable `WASM_CORE_LDFLAGS` in `external/mk/include.mk`
- `external/mk/include.mk` separates `WASM_CORE_LDFLAGS` from user-supplied `WASM_LDFLAGS`:
  - `WASM_CORE_LDFLAGS` always enforces `-mexec-model=reactor -Wl,--gc-sections -Wl,--no-entry -Wl,--allow-undefined`, plus (for downstream modules) `-Wl,--import-memory -Wl,-z,stack-size=32768 -Wl,--global-base=${WASM_GLOBAL_BASE} -Wl,--wrap=malloc -Wl,--wrap=free -Wl,--wrap=calloc -Wl,--wrap=realloc`.
- Individual library Makefiles only declare `WASM_SRCS` and `WASM_EXPORTS` (or `ISO_EXPORT` in C source).

### Invariant 4: Zero Unnecessary `wasi-libc` Stdio Bloat in Pure String/Math Kernels
- `external/liblemma/src/token.c` avoids `snprintf` and locale-dependent `<ctype.h>` `isalnum` so `lemma.wasm` links zero `vfprintf`/`stdio`/`wasi_snapshot_preview1` imports and zero `.data` segments.

### Invariant 5: Detached-`ArrayBuffer` Immunity & Automated Binary/Runtime Verification
- All JS wrappers (`npm/src/*.ts` and `npm/dist/*.js`) obtain `memory.buffer` views **after** all `malloc()` and C function calls complete.
- `external/mk/scripts/wasm-audit.sh` verifies:
  1. No forbidden native imports or unsanctioned exports.
  2. In `--check-substrate` mode: `stele.wasm` has unbounded max memory and `--global-base >= 2097152`, and every downstream `external/*/npm/dist/*.wasm` imports `env.memory`, exports `_initialize` (never `_start`, `malloc`, `free`), and occupies a strictly disjoint `[data_min, stack_ptr]` interval inside `[65536, 2097152)`.
- `tests/unit/run-wasm-single-instantiation.mjs` tests both default loading and explicit **all-14-modules pure-WASM execution** in one shared `WebAssembly.Memory`, including a **64 MB dynamic `memory.grow()` stress test** mid-flight verifying zero corruption of previously allocated structures across all modules.

---

## 3. Detailed Step-by-Step Implementation Plan

### Step 1: `external/mk` — Allocator Shim & Canonical `include.mk` Rules
1. Create `external/mk/wasm_alloc_shim.c` implementing `__wrap_malloc`, `__wrap_free`, `__wrap_calloc`, and `__wrap_realloc` backed by `env.malloc`, `env.free`, `env.calloc`, and `env.realloc`.
2. Update `external/mk/include.mk`:
   - Add portable `WASM_GLOBAL_BASE` lookup by `${ONELIB}` (`?=`).
   - Define `WASM_CORE_LDFLAGS` and `WASM_ALLOC_SHIM` conditionally on `${bname} = libstele` vs. downstream modules.
   - Update `$(wasmtarget)` to pass `${WASM_CORE_LDFLAGS} ${WASM_LDFLAGS} ${WASM_EXPORT_FLAGS}` and `${WASM_ALLOC_SHIM}`.

### Step 2: `external/libstele` — Unbounded 4 GB Memory, 2 MB Global Base, and Full Allocator Exports
1. Update `external/libstele/Makefile`:
   - Set `WASM_LDFLAGS` to export `malloc`, `free`, `calloc`, `realloc`, use `--global-base=2097152`, `--initial-memory=16777216`, and remove `--max-memory=268435456`.
2. Update `external/libstele/htdocs/stele-client.js`:
   - Change `new WebAssembly.Memory({ initial: 256, maximum: 4096 })` to `new WebAssembly.Memory({ initial: 256 })` (unbounded up to 4 GB).
3. Update `external/libstele/npm/src/index.ts` and `external/libstele/npm/dist/index.js`:
   - Remove `maximum: 4096` from `new WebAssembly.Memory({ initial: 256 })`.
   - Set fallback `_allocOffset = 2168640`.
   - Add `calloc(nmemb, size)` and `realloc(ptr, size)` to `WasmSubstrate`.
   - Inject `calloc` and `realloc` into `baseImports.env` and `exportsProxy` in `loadWasmModule` and `loadWasmModuleAsync`.
4. Rebuild `external/libstele` (`make -C external/libstele all wasm npm-build`).

### Step 3: `external/liblemma` & Sibling Libraries — Source, Makefile, and Detached-Buffer Fixes
1. **`external/liblemma`:**
   - Replace `snprintf` and `<ctype.h>` `isalnum` in `src/token.c` with freestanding helpers so `lemma.wasm` has zero stdio/WASI imports.
   - Simplify `external/liblemma/Makefile` to use `include.mk` defaults.
   - Update `external/liblemma/npm/dist/index.js` and `npm/src/index.ts` to read `memory.buffer` after `malloc` and C calls, and support `{ wasm: true }` option on `foldAccents` and `tokenize`.
   - Update `external/liblemma/npm/package.json` (`build:wasm`) and `npm/tests/lemma.test.mjs` to test both native and WASM paths.
2. **`external/libcorm`, `external/libjoint`, `external/libsepal`, `external/libislet`, `external/libhyle`, `external/libhyle-source`, `external/libxylem`, `external/libpitch`, `external/libbud`, `external/liblamina`, `external/axil-auren`:**
   - Remove legacy `WASM_LDFLAGS` overrides and `WASM_EXPORTS := malloc free` from Makefiles.
   - Export `xy_areg`, `xy_errno`, `xy_strerror`, `xy_call` cleanly in `external/libxylem/src/libxylem-wasm.c`.
   - Fix `memory.buffer` access ordering after `malloc` in `libsepal/npm`, `libbud/npm`, `axil-auren/npm`, `libjoint/npm`, `libislet/npm`.
3. **`external/libareole` (`@tty-pt/areole` & `@tty-pt/chunk`) and `external/libchaff` (`@tty-pt/chaff` & `@tty-pt/noise`):**
   - Update their `wasm` targets to link with `--import-memory`, their assigned `--global-base` (`1769472` for `areole`/`chunk`, `1638400` for `chaff`/`noise`), `-Wl,-z,stack-size=32768`, and `-Wl,--wrap=malloc/free/calloc/realloc` with `../mk/wasm_alloc_shim.c`.
   - Sync `areole.wasm` $\rightarrow$ `external/libchunk/npm/dist/chunk.wasm` and `chaff.wasm` $\rightarrow$ `external/libchaff/npm/dist/noise.wasm`.
4. Rebuild `.wasm` and `npm-build` across all packages.

### Step 4: Audit Tooling & Comprehensive Verification
1. Upgrade `external/mk/scripts/wasm-audit.sh` with `--check-substrate` to validate `stele.wasm` and all `external/*/npm/dist/*.wasm` for shared memory import, single allocator, reactor entry (`_initialize`), and non-overlapping `[data_min, stack_ptr]` bands below `2097152`.
2. Wire `sh external/mk/scripts/wasm-audit.sh --check-substrate` into `Makefile` (`boundary-check` and `npm-test-all`).
3. Expand `tests/unit/run-wasm-single-instantiation.mjs` to verify:
   - Single substrate instantiation.
   - Pure-WASM execution across all downstream libraries in one shared `WebAssembly.Memory`.
   - A 64 MB dynamic heap allocation (`memory.grow()`) mid-flight with zero corruption of live WASM state across all modules.
4. Run `make npm-test-all`, `make test-fast`, and `~/duna` unit tests.
