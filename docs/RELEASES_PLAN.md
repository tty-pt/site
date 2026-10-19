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
| **`libqsys`** | `v1.2.1` | **`v1.2.2`** | Patch | Documentation consistency across all symbols in `ttypt/qsys.h`, Doxygen formatting, Windows/Unix split documented per symbol. Zero ABI changes. |
| **`libcorm`** | `v1.4.0` | **`v2.0.0`** *(or `v1.5.0`)* | Major (or Minor) | **Breaking**: deleted `rec.h`; unified under `corm.h` and `cormr.h`. Added `cormd` resident daemon. New copy primitives (`corm_get_copy`, `corm_next_copy`), record registration helpers (`corm_rec_reg`, `corm_field_target`, `corm_rec_type`), multi-axis query engine (`cormr_query_run`), spec scan helpers. |
| **`libxylem`** | `v1.4.0` | **`v1.5.0`** | Minor | Soname loads resolve against `LD_LIBRARY_PATH` before storing identity (`module_load_path`). Multi-root `claim_at` and namespace isolation. Two-header rule enforcement (removed `PTR_ARGS_IMPL` guards). BSD/macOS fixes. |
| **`libjoint`** | `v1.3.0` | **`v1.4.0`** | Minor | Adopted modern `cormr.h` / `corm.h` API. Updated references from `libstoma` to `liblemma`. Memory tuning: scaled down initial allocation footprint. |
| **`libislet`** | `v1.1.0` | **`v1.2.0`** | Minor | Adopted modern `cormr.h` / `corm.h` API. Updated references from `libstoma` to `liblemma`. 4D Morton encoding property tests and multi-value cell enhancements. |
| **`libsepal`** | `v1.0.0` | **`v1.1.0`** | Minor | Added `CORM_SEPAL_EMBED_CACHE` file-backed vector embedding cache. Adopted modern `cormr.h` / `corm.h`. Updated `libstoma` → `liblemma`. |
| **`liblemma`** *(was `libstoma`)* | `v1.0.0` *(stoma)* | **`v1.0.0`** *(or `v2.0.0`)* | Major / Rebrand | Complete rename from `libstoma` to `liblemma`. Header `<ttypt/lemma.h>`, library `lib/liblemma.so`, prefix `lemma_*`. Accent-preserving UTF-8 folding (`lemma_fold`), inverted index FTS. Adopted `corm.h`. |
| **`libbud`** | `v1.0.0` | **`v1.0.1`** | Patch | Asset and documentation refresh. Pure-C isomorphic DOM scaffold and HTML renderer remain stable. |
| **`libhyle`** | `v1.3.0` | **`v1.4.0`** | Minor | Updated registry and view query engines to consume `liblemma` instead of `libstoma`. Rebuilt and verified with Corm 2.0. |
| **`libhyle-source`** | `v1.0.0` | **`v1.1.0`** | Minor | Updated storage persistence drivers to link and consume `liblemma` instead of `libstoma`. |
| **`libhyle-bud`** | `v1.0.0` | **`v1.0.1`** | Patch | Ecosystem synchronization and verification with updated Hyle and Bud. |
| **`axil`** | `v1.4.0` | **`v1.5.0`** | Minor | Framed HTTP responses (`Content-Length` + `Connection: close`). Security hardening (S5.4): teardown unauthenticated connections and gate terminal spawning. Per-connection generation tracking. Fixed 303 redirect connection teardown. Post-chroot module loading. |
| **`axil-auth`** | `v1.1.0` | **`v1.2.0`** | Minor | Enforce `/bin/false` as default shell in `passwd` rows for terminal access gating. Header split: `ttypt/auth-config.h` and `ttypt/auth-outcome.h`. Exported `auth_password_matches()`. |
| **`axil-hyle`** | `v1.0.0` | **`v1.0.1`** | Patch | Build flag cleanups, synchronized with `libhyle` and `libhyle-source`. |
| **`axil-tty`** | *(New release / v1.3.2)* | **`v1.4.0`** | Minor | Debut synchronized release. WebSocket-to-PTY terminal streaming bridge, Telnet NAWS negotiation, browser terminal frontend (`axil-tty.js`, `axil-tty.css`), fail-closed authenticated identity checks. |
| **`axil-nd`** | *(New release / v1.1.0)* | **`v1.2.0`** | Minor / Debut | Debut public release of NeverDark MUCK as a modular Axil plugin (`libaxil-nd.so`). Decoupled from legacy standalone architecture. Clean `law_*` command/data/storage pipeline, in-process Corm database storage, browser xterm client, telnet protocol support. |
| **`libauren`** *(was `libqllm`)* | *(New release / v1.1.0)* | **`v1.0.0`** *(or `v1.2.0`)* | Major / Rebrand | Rebranding from `libqllm` to `libauren`. Header `<ttypt/auren.h>`, library `lib/libauren.so`, module `libaxil-auren.so`, CLI `auren-chat`, pkg-config `auren.pc`. Multi-model registry (up to 16 models), SystemOne, sliding-window KV cache, ggml/Vulkan/Metal backends. |
| **`libqgl`** | *(New release / v1.1.0)* | **`v1.1.0`** | Minor / Debut | Debut public release of 2D graphics layer across OpenGL/GLFW and Linux framebuffer. Immediate-mode rendering, textures, bitmap fonts, tilemaps, layout engine, explicit `qgl_init()` lifecycle. |

---

## 3. Dependency Graph & Topological Release Ordering

To prevent broken downstream builds, releases must be executed in strict topological order based on library dependency linkages:

```
[Wave 1: Foundations]
   libqsys (1.2.2) ──┬────────────────────────────────────────────────────────┐
   libbud  (1.0.1) ──┼───────────────────────────────────────┐                │
   libtransp (sync)  │                                       │                │
                     ▼                                       │                │
[Wave 2: Storage]    │                                       │                │
   libcorm (2.0.0) ──┼──────────┬──────────┬──────────┐      │                │
                     │          │          │          │      │                │
[Wave 3: Modules & Axes]        │          │          │      │                │
   libxylem (1.5.0) ─┼───┐      │          │          │      │                │
   libjoint (1.4.0)  │   │      │          │          │      │                │
   libislet (1.2.0)  │   │      │          │          │      │                │
   libsepal (1.1.0)  │   │      │          │          │      │                │
   liblemma (1.0.0) ─┼───┼──────┼───┐      │          │      │                │
   libqgl   (1.1.0)  │   │      │   │      │          │      │                │
                     ▼   │      │   ▼      │          │      │                │
[Wave 4: Schema & Web]   │      │   │      │          │      │                │
   libhyle  (1.4.0) ─────┼──────┼───┼──────┼──┐       │      │                │
   axil     (1.5.0) ─────┼───┐  │   │      │  │       │      │                │
                         │   │  │   │      │  │       │      │                │
[Wave 5: Persistence & UI]   │  │   │      │  │       │      │                │
   libhyle-source (1.1.0)────┼──┼───┼──────┼──┼───┐   │      │                │
   libhyle-bud    (1.0.1) ◄──┘  │   │      │  │   └───┼──────┘                │
                                │   │      │  │       │                       │
[Wave 6: Services & Apps]       │   │      │  │       │                       │
   axil-auth (1.2.0) ◄──────────┘   │      │  │       │                       │
   axil-tty  (1.4.0) ◄──────────────┘      │  │       │                       │
   axil-hyle (1.0.1) ◄─────────────────────┘──┘       │                       │
   axil-nd   (1.2.0) ◄────────────────────────────────┘                       │
   libauren  (1.0.0) ◄────────────────────────────────────────────────────────┘
```

### Detailed Wave Sequence

1. **Wave 1: Foundational Zero-Dependency Libraries**
   - Step 1: `libqsys` (`v1.2.2`)
   - Step 2: `libbud` (`v1.0.1`)
   - Step 3: `libtransp`

2. **Wave 2: Core Storage Engine**
   - Step 4: `libcorm` (`v2.0.0`)

3. **Wave 3: Dynamic Runtime, Domain Axes, and Graphics**
   - Step 5: `libxylem` (`v1.5.0`)
   - Step 6: `libjoint` (`v1.4.0`)
   - Step 7: `libislet` (`v1.2.0`)
   - Step 8: `libsepal` (`v1.1.0`)
   - Step 9: `liblemma` (`v1.0.0`)
   - Step 10: `libqgl` (`v1.1.0`)

4. **Wave 4: Canonical Schemas & HTTP Server Engine**
   - Step 11: `libhyle` (`v1.4.0`)
   - Step 12: `axil` (`v1.5.0`)

5. **Wave 5: Persistence & UI Bridge Libraries**
   - Step 13: `libhyle-source` (`v1.1.0`)
   - Step 14: `libhyle-bud` (`v1.0.1`)

6. **Wave 6: Axil Dynamic Modules, Interactive Engines & Services**
   - Step 15: `axil-auth` (`v1.2.0`)
   - Step 16: `axil-tty` (`v1.4.0`)
   - Step 17: `axil-hyle` (`v1.0.1`)
   - Step 18: `axil-nd` (`v1.2.0`)
   - Step 19: `libauren` (`v1.0.0`)

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
3. **Step 3: Root Site & Submodule Pointer Updates**
   - Update git submodules in `site` to the newly tagged release commits.
   - Run root site verification (`make clean && make && make test`).
   - Publish GitHub Release announcements with the unified changelog text from Section 1.
