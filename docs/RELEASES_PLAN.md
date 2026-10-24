# Comprehensive Multi-Repository Release Plan: The tty-pt Ecosystem

> **Scope**: Research audit and release roadmap across 18 tty-pt repositories since previous public releases, integrating core platform upgrades, search/recall axes, isomorphic UI frameworks, and newly modularized services (`axil-nd`, `axil-tty`, `libqllm` → `libauren`, `libqgl`).

---

## 1. Executive Summary & Unified Changelog Item

Below is the **single unified changelog entry** synthesizing the cross-cutting architectural breakthroughs, breaking changes, renames, and newly introduced capabilities across all 18 repositories since the last release wave.

```markdown
# Release Wave 2026.10: The Modular C Data, Search, and Runtime Ecosystem

This release represents a landmark milestone across the entire tty-pt ecosystem: unifying
our core HTTP and module harnesses, overhauling our multi-axis search and retrieval pipeline,
stabilizing our isomorphic C-WASM web runtime, and introducing modular interactive services.

### Highlights & Architecture

1. **Domain-Free Multi-Axis Recall Architecture**:
   - `libcorm` introduces the modernized Recall Kernel (`<ttypt/cormr.h>`), providing
     a standardized "filter by axis → join → rank" query pipeline over uniform 32-bit refs.
   - Pluggable search axes across four independent dimensions:
     * **Time / Intervals**: `libjoint` (interval trees, temporal validity, zero-copy query).
     * **Space / Geometry**: `libislet` (N-dimensional Morton Z-order space-filling curves).
     * **Meaning / Semantics**: `libsepal` (vector embedding store, cosine ANN ranking, embed cache).
     * **Text / Lexical**: `liblemma` (accent-sensitive UTF-8 folding, inverted index FTS).
   - Clean elimination of legacy `rec.h` in favor of eponymous umbrella inclusion `<ttypt/corm.h>`
     and specialized subsystem header `<ttypt/cormr.h>`.
   - Introduction of `cormd`, a resident memory daemon keeping databases and indexes warm.

2. **Major Library Renames & Rebranding**:
   - **`libstoma` → `liblemma`**: Fully rebranded search tokenization and accent-sensitive
     inverted index library (`<ttypt/lemma.h>`, `lib/liblemma.so`).
   - **`libqllm` → `libauren`**: Transitioning the standalone LLM inference engine and Axil
     module to `libauren` (`<ttypt/auren.h>`, `lib/libauren.so`, `libaxil-auren.so`), featuring
     a 16-model registry subsystem, SystemOne support, and sliding-window KV cache.

3. **Core Server & Module Runtime Hardening**:
   - **`axil`**: Framed HTTP responses (`Content-Length` + `Connection: close`), immediate
     security teardown (S5.4) of unauthenticated connections, per-connection generation
     tracking, and 303 redirect connection lifecycle fixes.
   - **`libxylem`**: Dynamic soname resolution respecting `LD_LIBRARY_PATH`, multi-root
     `claim_at` namespace isolation, and strict enforcement of the two-header separation rule.
   - **`axil-auth`**: Fail-closed `/bin/false` default shell for terminal gating, header split
     into `auth-config.h` and `auth-outcome.h`, and exported credential verification primitives.

4. **Isomorphic Pure-C Full-Stack Platform**:
   - **`libbud`**: Dual-compiling C DOM builder for zero-overhead SSR and browser WASM hydration.
   - **`libhyle` & `libhyle-source`**: Schema-driven relational query engine and persistence driver.
   - **`libhyle-bud` & `axil-hyle`**: Declarative UI binder and REST CRUD endpoint bridges.

5. **Interactive Services & 2D Graphics**:
   - **`axil-tty`**: WebSocket-to-PTY terminal streaming bridge with Telnet NAWS negotiation.
   - **`axil-nd`**: Modular NeverDark MUCK engine running in-process behind Axil.
   - **`libqgl`**: Cross-platform immediate-mode 2D graphics layer across OpenGL/GLFW and
     the Linux framebuffer.
```

---

## 2. Repository-by-Repository Audit & Deltas

| Repository | Baseline Tag | Proposed Version | SemVer Justification | Key Deltas & Breaking Changes |
| :--- | :--- | :--- | :--- | :--- |
| **`libqsys`** | `v1.2.1` | **`v1.3.1`** | Minor | Added universal isomorphic C export contract `<ttypt/iso.h>` (`ISO_EXPORT`, `ISO_EXPORT_AS`, `ISO_IMPORT`), packaging in `/usr/include/ttypt/iso.h`. Updated `qsys.pc` to 1.3.1. |
| **`libcorm`** | `v1.4.0` | **`v2.0.1`** | Major | **Breaking**: deleted `rec.h`; unified under `corm.h` and `cormr.h`. Added `cormd` resident daemon, linear memory arena shims for WASM, and multi-axis query engine (`cormr_query_run`). Updated `corm.pc` to 2.0.1. |
| **`libxylem`** | `v1.4.0` | **`v1.6.0`** | Minor | In-WASM Xylem core dispatcher (`libxylem-wasm.c`), `XY_LISTENER_TAG`, dynamic loader bridge, and `LD_LIBRARY_PATH` soname resolution. Updated `xylem.pc` to 1.6.0. |
| **`libjoint`** | `v1.3.0` | **`v1.5.0`** | Minor | Exported `rec_axis_store_typed` for HTOK binary date ingestion. Adopted modern `cormr.h` / `corm.h`. Updated `joint.pc` to 1.5.0. |
| **`libislet`** | `v1.1.0` | **`v1.2.1`** | Patch | Purged hardcoded `/home` paths, enhanced build portability with `add-prefix`. Updated `islet.pc` to 1.2.1. |
| **`libsepal`** | `v1.0.0` | **`v1.2.0`** | Minor | Exported `rec_axis_store_typed` for HTOK binary vector embedding ingestion. Updated `sepal.pc` to 1.2.0. |
| **`liblemma`** *(was `libstoma`)* | `v1.0.0` | **`v1.1.0`** | Minor | Lossless tokenizer, 3-gram index, and elimination of `doc_hd`. Rebranded from `libstoma` (`<ttypt/lemma.h>`, `lib/liblemma.so`). Updated `lemma.pc` to 1.1.0. |
| **`libbud`** | `v1.0.0` | **`v1.0.2`** | Patch | Namespaced host imports (`ISO_IMPORT`), universal `bud_slot` engine, keyed hydration, and 60fps `wasm_tick` frame loop. Updated `bud.pc` to 1.0.2. |
| **`libhyle`** | `v1.3.0` | **`v1.5.0`** | Minor | Added binary token array storage and rotating ring buffer for lazy SSR decoding. Updated `hyle.pc` to 1.5.0. |
| **`libhyle-source`** | `v1.0.0` | **`v1.2.0`** | Minor | Added `store_tok`, companion `data.tok`, `record.tok` container writing, and `hyle-tok` tool. Updated `hyle-source.pc` to 1.2.0. |
| **`libhyle-bud`** | `v1.0.0` | **`v1.0.2`** | Patch | Ecosystem synchronization and verification with updated Hyle 1.5.0 and Bud 1.0.2. Updated `hyle-bud.pc` to 1.0.2. |
| **`axil`** | `v1.4.0` | **`v1.6.0`** | Minor | Provided canonical browser `axil-bridge.js`, single-socket WebSocket multiplexing, and wasm allowed imports. Updated `axil.pc` to 1.6.0. |
| **`axil-auth`** | `v1.1.0` | **`v1.2.1`** | Patch | Added support for port-qualified and configured cookie names. Updated `axil-auth.pc` to 1.2.1. |
| **`axil-hyle`** | `v1.0.0` | **`v1.0.1`** | Patch | Ecosystem synchronization with Hyle 1.5.0 and Corm 2.0.1. Updated `axil-hyle.pc` to 1.0.1. |
| **`axil-tty`** | `v1.4.0` | **`v1.4.1`** | Patch | Pure tty terminal capability bridge consuming axil transport. Updated `axil-tty.pc` to 1.4.1. |
| **`libauren`** *(was `libqllm`)* | `v1.1.0` | **`v1.0.0`** | Major / Rebrand | Rebranding from `libqllm` to `libauren` (`<ttypt/auren.h>`, `lib/libauren.so`, `libaxil-auren.so`, CLI `auren-chat`). Multi-model registry (16 models), SystemOne, sliding-window KV cache. |
| **`external/mk`** | `v0.1.18` | **`v0.2.0`** | Minor | Reusable isomorphic C engine (`iso.mk`), WASM ABI auditor (`wasm-audit.sh`), and purged `portable.mk`. |
| **`pi-quest`** | `v1.1.0` | **`v1.2.0`** | Minor | Full quest journal engine: evidence gate, subquest foreman, Jev System One intent judge, TUI plan viewer, and zip bundle packaging. |
| **`pi-mm`** | `v0.1.0` | **`v0.3.1`** | Minor | Cormd-first memory daemon, HTOK v4 cognitive triads, associative graph, epistemic safety states, and reciprocal rank fusion. |

---

## 3. Dependency Graph & Topological Release Ordering

To prevent broken downstream builds, releases must be executed in strict topological order based on library dependency linkages:

```
[Wave 1: Foundations]
   libqsys (1.3.1) ──┬────────────────────────────────────────────────────────┐
   libbud  (1.0.2) ──┼───────────────────────────────────────┐                │
   mk      (0.2.0)   │                                       │                │
   libtransp (sync)  │                                       │                │
                     ▼                                       │                │
[Wave 2: Storage]    │                                       │                │
   libcorm (2.0.1) ──┼──────────┬──────────┬──────────┐      │                │
                     │          │          │          │      │                │
[Wave 3: Modules & Axes]        │          │          │      │                │
   libxylem (1.6.0) ─┼───┐      │          │          │      │                │
   libjoint (1.5.0)  │   │      │          │          │      │                │
   libislet (1.2.1)  │   │      │          │          │      │                │
   libsepal (1.2.0)  │   │      │          │          │      │                │
   liblemma (1.1.0) ─┼───┼──────┼───┐      │          │      │                │
   libqgl   (1.1.0)  │   │      │   │      │          │      │                │
                     ▼   │      │   ▼      │          │      │                │
[Wave 4: Schema & Web]   │      │   │      │          │      │                │
   libhyle  (1.5.0) ─────┼──────┼───┼──────┼──┐       │      │                │
   axil     (1.6.0) ─────┼───┐  │   │      │  │       │      │                │
                         │   │  │   │      │  │       │      │                │
[Wave 5: Persistence & UI]   │  │   │      │  │       │      │                │
   libhyle-source (1.2.0)────┼──┼───┼──────┼──┼───┐   │      │                │
   libhyle-bud    (1.0.2) ◄──┘  │   │      │  │   └───┼──────┘                │
                                │   │      │  │       │                       │
[Wave 6: Services & Apps]       │   │      │  │       │                       │
   axil-auth (1.2.1) ◄──────────┘   │      │  │       │                       │
   axil-tty  (1.4.1) ◄──────────────┘      │  │       │                       │
   axil-hyle (1.0.1) ◄─────────────────────┘──┘       │                       │
   axil-nd   (1.2.0) ◄────────────────────────────────┘                       │
   libauren  (1.0.0) ◄────────────────────────────────────────────────────────┘

[Tooling Extensions]
   pi-quest  (1.2.0)
   pi-mm     (0.3.1)
```

### Detailed Wave Sequence

1. **Wave 1: Foundational Zero-Dependency Libraries**
   - Step 1: `libqsys` (`v1.3.1`)
   - Step 2: `libbud` (`v1.0.2`)
   - Step 3: `mk` (`v0.2.0`)
   - Step 4: `libtransp`

2. **Wave 2: Core Storage Engine**
   - Step 5: `libcorm` (`v2.0.1`)

3. **Wave 3: Dynamic Runtime, Domain Axes, and Graphics**
   - Step 6: `libxylem` (`v1.6.0`)
   - Step 7: `libjoint` (`v1.5.0`)
   - Step 8: `libislet` (`v1.2.1`)
   - Step 9: `libsepal` (`v1.2.0`)
   - Step 10: `liblemma` (`v1.1.0`)
   - Step 11: `libqgl` (`v1.1.0`)

4. **Wave 4: Canonical Schemas & HTTP Server Engine**
   - Step 12: `libhyle` (`v1.5.0`)
   - Step 13: `axil` (`v1.6.0`)

5. **Wave 5: Persistence & UI Bridge Libraries**
   - Step 14: `libhyle-source` (`v1.2.0`)
   - Step 15: `libhyle-bud` (`v1.0.2`)

6. **Wave 6: Axil Dynamic Modules, Interactive Engines & Services**
   - Step 16: `axil-auth` (`v1.2.1`)
   - Step 17: `axil-tty` (`v1.4.1`)
   - Step 18: `axil-hyle` (`v1.0.1`)
   - Step 19: `libauren` (`v1.0.0`)
   - Step 20: `axil-nd` (`v1.2.0` - tenant ongoing)

7. **Wave 7: Agent Harness Tooling Extensions**
   - Step 21: `pi-quest` (`v1.2.0`)
   - Step 22: `pi-mm` (`v0.3.1`)

---

## 4. Per-Repository Preparation & Quality Checklist

For each repository, the following checklist must be satisfied before creating git tags and publishing releases:

### 1. `libqsys` (Target: `v1.2.2`)
- [ ] Verify `CHANGELOG.md` entry for `1.2.2`.
- [ ] Update `qsys.pc`: set `Version: 1.2.2` (currently `0.0.1`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.2.2 -m "Release v1.2.2"`.

### 2. `libbud` (Target: `v1.0.1`)
- [ ] Add `1.0.1` entry in `CHANGELOG.md` documenting logo, asset, and documentation updates.
- [ ] Update `bud.pc`: set `Version: 1.0.1` (currently `0.1.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.0.1 -m "Release v1.0.1"`.

### 3. `libcorm` (Target: `v2.0.0`)
- [ ] Update `CHANGELOG.md` with complete documentation of `cormd`, `cormr.h`, removal of `rec.h`, API streamlining (`corm_rec_reg`, `corm_field_target`, `corm_rec_type`), and multi-axis query engine.
- [ ] Update `corm.pc`: set `Version: 2.0.0` (currently `0.6.0`).
- [ ] Verify Doxygen in `include/ttypt/corm.h` and `include/ttypt/cormr.h`.
- [ ] Rebuild and execute full test matrix:
  `make clean && make && ./test.sh && ./test-cli.sh && ./test-roster.sh && ./test-fanout.sh && ./test-real.sh && ./test-mm.sh && ./test-shortcircuit.sh`.
- [ ] Tag: `git tag -s v2.0.0 -m "Release v2.0.0: Modernized Cormr Recall Kernel & Cormd"`.

### 4. `libxylem` (Target: `v1.5.0`)
- [ ] Finalize `## 1.5.0` in `CHANGELOG.md`: document `LD_LIBRARY_PATH` soname resolution, multi-root `claim_at`, and two-header enforcement.
- [ ] Update `xylem.pc`: set `Version: 1.5.0` (currently `0.2.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.5.0 -m "Release v1.5.0: Enhanced Soname Resolution and Namespace Isolation"`.

### 5. `libjoint` (Target: `v1.4.0`)
- [ ] Add `1.4.0` section in `CHANGELOG.md`: document `liblemma` update, `cormr.h` adoption, and reduced initial allocation footprint.
- [ ] Update `joint.pc`: set `Version: 1.4.0` (currently `1.1.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.4.0 -m "Release v1.4.0: Lemma & Cormr Integration"`.

### 6. `libislet` (Target: `v1.2.0`)
- [ ] Add `1.2.0` section in `CHANGELOG.md`: document `liblemma` update, `cormr.h` adoption, and 4D Morton encoding properties.
- [ ] Update `islet.pc`: set `Version: 1.2.0` (currently `0.5.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.2.0 -m "Release v1.2.0: 4D Morton Indexing & Cormr Integration"`.

### 7. `libsepal` (Target: `v1.1.0`)
- [ ] Add `1.1.0` section in `CHANGELOG.md`: document `CORM_SEPAL_EMBED_CACHE`, `liblemma` update, and `cormr.h` adoption.
- [ ] Update `sepal.pc`: set `Version: 1.1.0` (currently `0.1.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.1.0 -m "Release v1.1.0: Vector Embedding Cache & Cormr Recall"`.

### 8. `liblemma` (Target: `v1.0.0` of `liblemma`)
- [ ] Finalize `CHANGELOG.md`: document transition from `libstoma` to `liblemma`, header `<ttypt/lemma.h>`, and `cormr.h` integration.
- [ ] Ensure `lemma.pc` exists with `Version: 1.0.0`.
- [ ] Verify `README.md` reflects `liblemma` naming throughout.
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.0.0 -m "Release v1.0.0: Rebranded liblemma Lexical FTS Engine"`.

### 9. `libqgl` (Target: `v1.1.0`)
- [ ] Finalize `CHANGELOG.md` for `1.1.0`: document explicit `qgl_init()`, GLFW/OpenGL and Linux framebuffer backends, and layout engine.
- [ ] Update `qgl.pc`: set `Version: 1.1.0` (currently `1.0.2`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.1.0 -m "Release v1.1.0: Debut Standalone 2D Graphics Engine"`.

### 10. `libhyle` (Target: `v1.4.0`)
- [ ] Add `1.4.0` section in `CHANGELOG.md`: document `liblemma` integration and compatibility with Corm 2.0.
- [ ] Update `hyle.pc`: set `Version: 1.4.0` (currently `0.2.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.4.0 -m "Release v1.4.0: Lemma & Corm 2.0 Integration"`.

### 11. `axil` (Target: `v1.5.0`)
- [ ] Finalize `## 1.5.0` in `CHANGELOG.md`: document framed responses, S5.4 security connection teardowns, per-connection generation, and 303 redirect fix.
- [ ] Update `axil.pc`: set `Version: 1.5.0` (currently `1.0.0`).
- [ ] Update `SECURITY.md` and `README.md` to reflect current security and connection state management.
- [ ] Rebuild and run test suite: `make clean && make test && ./test.sh`.
- [ ] Tag: `git tag -s v1.5.0 -m "Release v1.5.0: Framed HTTP Responses & Connection Security Hardening"`.

### 12. `libhyle-source` (Target: `v1.1.0`)
- [ ] Add `1.1.0` section in `CHANGELOG.md`: document `liblemma` integration.
- [ ] Update `hyle-source.pc`: set `Version: 1.1.0` (currently `0.2.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.1.0 -m "Release v1.1.0: Lemma Integration"`.

### 13. `libhyle-bud` (Target: `v1.0.1`)
- [ ] Add `1.0.1` section in `CHANGELOG.md`: document ecosystem synchronization.
- [ ] Update `hyle-bud.pc`: set `Version: 1.0.1` (currently `0.2.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.0.1 -m "Release v1.0.1: Ecosystem Synchronization"`.

### 14. `axil-auth` (Target: `v1.2.0`)
- [ ] Finalize `## 1.2.0` in `CHANGELOG.md`: document `/bin/false` default shell, header separation (`auth-config.h` & `auth-outcome.h`), and `auth_password_matches()`.
- [ ] Create/update `axil-auth.pc`: set `Version: 1.2.0`.
- [ ] Rebuild and run test suite: `make clean && make test && ./test.sh`.
- [ ] Tag: `git tag -s v1.2.0 -m "Release v1.2.0: Terminal Gating Security & Header Separation"`.

### 15. `axil-tty` (Target: `v1.4.0`)
- [ ] Create `## 1.4.0` in `CHANGELOG.md`: document debut public release, WebSocket-to-PTY bridge, Telnet NAWS window resizing, and authenticated identity gating.
- [ ] Create/update `axil-tty.pc`: set `Version: 1.4.0`.
- [ ] Update `README.md` and man page sources (`man-src/sh.10`).
- [ ] Rebuild and run test suite: `make clean && make test && ./test.sh`.
- [ ] Tag: `git tag -s v1.4.0 -m "Release v1.4.0: Debut WebSocket PTY Terminal Module"`.

### 16. `axil-hyle` (Target: `v1.0.1`)
- [ ] Add `1.0.1` section in `CHANGELOG.md`: document build cleanups and Hyle 1.4.0 synchronization.
- [ ] Update `axil-hyle.pc`: set `Version: 1.0.1` (currently `0.1.0`).
- [ ] Rebuild and run test suite: `make clean && make test`.
- [ ] Tag: `git tag -s v1.0.1 -m "Release v1.0.1: Hyle 1.4.0 Synchronization"`.

### 17. `axil-nd` (Target: `v1.2.0`)
- [ ] Finalize `## 1.2.0` in `CHANGELOG.md`: document debut public release as an Axil module, law command/data/storage pipeline, removal of monolithic dead code/JS, and in-process Corm database storage.
- [ ] Create `axil-nd.pc`: set `Version: 1.2.0`.
- [ ] Ensure `motd` and `man/` help topics are fully populated.
- [ ] Rebuild and run test suite: `make clean && make && ./test.sh`.
- [ ] Tag: `git tag -s v1.2.0 -m "Release v1.2.0: Debut Modular NeverDark MUCK Engine for Axil"`.

### 18. `libauren` (formerly `libqllm`, Target: `v1.0.0`)
- [ ] Execute rename migration:
  * Rename header `include/ttypt/qllm.h` → `include/ttypt/auren.h` (with backward-compatibility aliases).
  * Rename source files / module `libaxil-qllm.c` → `libaxil-auren.c`, `libqllm.c` → `libauren.c`.
  * Rename pkg-config `axil-qllm.pc` → `auren.pc` / `axil-auren.pc` with `Version: 1.0.0`.
  * Rename CLI tools `qllm-chat` → `auren-chat`, `qllm-list` → `auren-list`.
  * Update config discovery: check `auren.json` then fallback to `qllm.json`.
- [ ] Update `CHANGELOG.md` with debut `## 1.0.0` release under `libauren`, documenting 16-model registry, SystemOne, sliding-window KV cache, and Vulkan/Metal acceleration.
- [ ] Update `README.md` with new branding and CLI usage examples.
- [ ] Rebuild and run unit tests: `make clean && make && make -C tests test`.
- [ ] Tag: `git tag -s v1.0.0 -m "Release v1.0.0: Rebranded libauren Inference Engine"`.

---

## 5. Implementation Roadmap for Executing the Releases

To execute these releases smoothly, follow this operational checklist:

1. **Step 1: Preparation Work in Sister Repos**
   - Execute the `libqllm` → `libauren` rename in `/home/quirinpa/libqllm`.
   - Update `.pc` file `Version:` fields across all 18 repositories to match the targeted release versions.
   - Finalize `CHANGELOG.md` entries in each repo according to Section 4.
2. **Step 2: Tagging and Publishing per Wave**
   - Execute Wave 1 (`libqsys`, `libbud`), tag, and verify packaging.
   - Execute Wave 2 (`libcorm`), tag, and install system headers/libs if required.
   - Execute Wave 3 (`libxylem`, `libjoint`, `libislet`, `libsepal`, `liblemma`, `libqgl`), tag, and test.
   - Execute Wave 4 (`libhyle`, `axil`), tag, and test.
   - Execute Wave 5 (`libhyle-source`, `libhyle-bud`), tag, and test.
   - Execute Wave 6 (`axil-auth`, `axil-tty`, `axil-hyle`, `axil-nd`, `libauren`), tag, and test.

---

## 6. Release Wave 2026.11: The Full-Universe Isomorphic Runtime, Linker Purity & Binary Tokenization

Building upon Wave 2026.10, this release establishes the complete pure-C isomorphic NeverDark runtime, full 32-planet universe deployment, DT_RUNPATH/RPATH elimination, in-WASM Xylem hook dispatching, and HTOK v3 binary tokenization.

### Full Ecosystem SemVer & Release Matrix (39 Targets)

| Target / Submodule | Prior Version | Next Release | SemVer Type | Scope & Key Additions |
| :--- | :--- | :--- | :--- | :--- |
| **`site` (host platform)** | `none` | **`v1.0.0`** | Major / Debut | Turnkey full-universe deployment (`./start.sh`), pure-C isomorphic Bud UI for NeverDark (`htdocs/nd.wasm`), multi-viewport support (WebGL 3D, Lamina 2D, TTY, Bud DOM), zero site-specific JS. |
| **`libcorm`** | `v2.0.0` | **`v2.0.1`** | Patch | WASM linear memory arena shims (`src/corm_compat.h:63`), resident daemon `cormd` multi-instance resilience, DT_RUNPATH elimination. |
| **`liblemma`** | `v1.0.0` | **`v1.1.0`** | Minor | Lossless subword tokenizer (`src/tok.c`, `lemma_tok_encode/decode/stream`), static BPE vocabulary (`vocab.txt`), 3-gram index, `doc_hd` elimination. |
| **`libjoint`** | `v1.4.0` | **`v1.5.0`** | Minor | Exported `rec_axis_store_typed` for HTOK ISO date ingestion, clean system library linkage. |
| **`libsepal`** | `v1.1.0` | **`v1.2.0`** | Minor | In-memory vector store rebuilding, `rec_axis_store_typed` embedding ingestion, cache synchronization. |
| **`libislet`** | `v1.2.0` | **`v1.2.1`** | Patch | Portable build flags, purge of hardcoded `/home/` paths, portable add-prefix. |
| **`libxylem`** | `v1.5.0` | **`v1.6.0`** | Minor | In-WASM hook registry and dispatcher (`src/libxylem-wasm.c`), `XY_LISTENER_TAG`, browser hook chaining without `dlopen`. |
| **`axil`** | `v1.5.0` | **`v1.6.0`** | Minor | Canonical browser WebSocket bus (`htdocs/axil-bridge.js`), channel multiplexing (Stream 0 text, Stream 1 BCP, Stream 2 RPC), WASM capability allowlist. |
| **`axil-auth`** | `v1.2.0` | **`v1.2.1`** | Patch | Port-qualified session cookies (`QSESSION_<port>`), two-header separation. |
| **`axil-tty`** | `v1.4.0` | **`v1.4.1`** | Patch | Pure TTY capability bridge consuming `axil-bridge.js`, local xterm.js vendoring, decoupled terminal resize. |
| **`axil-nd`** | `v1.2.0` | **`v1.3.0`** | Minor | Declarative BCP protocol (`<nd/bcp.h>`), multi-viewport system (`<nd/viewport.h>`), extensible UI slots (`nd_client_ext`), generic table loader (`nd_table_load`), XY bus table handle encapsulation (`nd_table_handle`). |
| **`libbud`** | `v1.0.1` | **`v1.0.2`** | Patch | Namespaced `bud.*` WASM host imports, hardened capability allowlist (`wasm-allowed-imports.lst`). |
| **`libhyle`** | `v1.4.0` | **`v1.5.0`** | Minor | Binary token array storage, rotating ring buffer for lazy SSR decoding and high-throughput zero-copy field parsing. |
| **`libhyle-bud`** | `v1.0.1` | **`v1.0.2`** | Patch | Ecosystem synchronization with Hyle 1.5 token storage and Bud 1.0.2 host imports. |
| **`libhyle-source`** | `v1.1.0` | **`v1.2.0`** | Minor | Binary token store driver (`store_tok`), companion `data.tok`, `hyle-tok` inspection CLI tool. |
| **`nd-attr`** | `v1.0.1` | **`v1.1.0`** | Minor | Interactive 2-column character sheet (`ux/attr_ui.c`, `<nd/attr-ui.h>`), HP/MP vitality gauges (`<nd/attr-bcp.h>`), `stats` command. |
| **`nd-biome`** | `v1.0.1` | **`v1.0.2`** | Patch | Declarative `data/biomes.dsv` table (20 biomes) loaded dynamically via `nd_table_load()`. |
| **`nd-class`** | `v1.0.2` | **`v1.0.3`** | Patch | Declarative `data/classes.dsv` table (4 classes) loaded dynamically via `nd_table_load()`. |
| **`nd-core`** | `v1.0.2` | **`v1.0.3`** | Patch | Two-header separation (`<nd/core-types.h>`), removed `CORE_IMPL` guards, standardized README. |
| **`nd-drink`** | `v1.0.7` | **`v1.0.8`** | Patch | Two-header separation (`<nd/drink-types.h>`), removed `DRINK_IMPL` guards, standardized README. |
| **`nd-equip`** | `v1.0.2` | **`v1.1.0`** | Minor | 4x5 paper doll modal overlay (`ux/equip_ui.c`, `<nd/equip-ui.h>`), BCP unpacker (`<nd/equip-bcp.h>`), 9-slot descriptor table. |
| **`nd-fight`** | `v1.0.2` | **`v1.1.0`** | Minor | Combat target HUD overlay (`ux/fight_ui.c`, `<nd/fight-ui.h>`) driven by live target vitality gauge. |
| **`nd-level`** | `v1.0.1` | **`v1.1.0`** | Minor | Two-header separation (`<nd/level-types.h>` defining `nd_level_t` and `ND_LEVEL_DEFAULT`), public caller `<nd/level.h>`. |
| **`nd-mob`** | `v1.0.2` | **`v1.0.3`** | Patch | Declarative `data/mobs.dsv` table (14 mob templates) loaded dynamically via `nd_table_load()`. |
| **`nd-mortal`** | `v1.0.2` | **`v1.1.0`** | Minor | Two-header separation (`<nd/mortal-types.h>`), target vitality BCP channel (`<nd/mortal-bcp.h>`), and `ux/mortal_ui.c`. |
| **`nd-other`** | `v1.0.1` | **`v1.0.2`** | Patch | Standardized README with man-src and modder guide. |
| **`nd-plant`** | `v1.0.2` | **`v1.1.0`** | Minor | Two-header separation (`<nd/plant-types.h>` defining `plant_tile_t`), declarative `data/plants.dsv` (10 plant species). |
| **`nd-race`** | `v1.0.1` | **`v1.1.0`** | Minor | Two-header separation (`<nd/race-types.h>` defining `race_t`), declarative `data/races.dsv` (5 playable races). |
| **`nd-seat`** | `v1.0.2` | **`v1.0.3`** | Patch | Two-header separation (`<nd/seat-types.h>`), removed `SEAT_IMPL` guards, standardized README. |
| **`nd-shop`** | `v1.0.1` | **`v1.0.2`** | Patch | Header import cleanup, standardized README with man-src and modder guide. |
| **`nd-spell`** | `v1.0.7` | **`v1.1.0`** | Minor | Two-header separation (`<nd/spell-types.h>`), declarative `data/spells.dsv` (11 spells), BCP unpackers (`<nd/spell-bcp.h>`), grimoire modal UI (`ux/spell_ui.c`, `<nd/spell-ui.h>`). |
| **`nd-stone`** | `v1.0.1` | **`v1.0.2`** | Patch | Declarative `data/stones.dsv` table loaded dynamically via `nd_table_load()`. |
| **`nd-vanilla`** | `v1.0.1` | **`v1.0.2`** | Patch | Standardized README with man-src, art, and modder guide. |
| **`nd-wts`** | `v1.0.1` | **`v1.0.2`** | Patch | Declarative `data/wts.dsv` table (12 weapon types) loaded dynamically via `nd_table_load()`. |
| **`.pi/extensions/pi-mm`** | `v0.1.0` | **`v0.2.0`** | Minor | HTOK v3 binary tokenization format, associative memory co-occurrence graph (`src/associative.ts`), cognitive priming, flashbulb anchors. |
| **Engine Subsystems (8 in-tree)** | `none` | `in-tree` | Internal | Unversioned host subsystems (`nd-bus`, `nd-mcp`, `nd-obj`, `nd-region`, `nd-session`, `nd-speech`, `nd-view`, `nd-world`) providing core spacetime coordinates, object skeletons, and session queues. |

3. **Step 3: Root Site & Submodule Pointer Updates**
   - Update git submodules in `site` to the newly tagged release commits.
   - Run root site verification (`make clean && make && make test`).
   - Publish GitHub Release announcements with the unified changelog text from Section 1.
