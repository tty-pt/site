# Ecosystem Release Execution Strategy

> **Location**: `docs/RELEASE_EXECUTION_STRATEGY.md`  
> **Status**: Active Operational Playbook  
> **Author**: Core Systems Architecture & Release Engineering  

This document formalizes the reproducible, battle-tested release methodology used across the **tty-pt** ecosystem (20+ repositories covering pure-C runtime harnesses, storage engines, search axes, isomorphic WASM UI binders, HTTP multiplexers, and game engines).

---

## 1. Core Principles

1. **Topological Ordering (Strict DAG)**:
   Downstream repositories consume upstream dependencies via system packages (`apk`, `deb`, `rpm`, `pacman`, `brew`, `winget`). Upstream packages **must be fully built, released, and published to repository indexes** before downstream CI pipelines are triggered.

2. **Batching Heuristic (At Least 3 to 4 at a Time When Safe)**:
   Do not serialize releases one-by-one. When upstream layer prerequisites are fully published and dependencies are satisfied, release in parallel batches of **at least 3 or 4 repositories at a time**.
   - **Throughput & Efficiency**: Batching 3–4 repositories maintains strong release momentum across the 20+ ecosystem without stalling.
   - **Runner Safety & Resource Limits**: 3–4 concurrent workflows remain within GitHub Actions concurrent runner limits for specialized runners (macOS `arm64`, macOS `x86_64`, and Windows MinGW64), preventing queue starvation.
   - **Topological Integrity**: Only batch repositories whose mutual dependencies are already published or completely independent.

3. **Mandatory Documentation & Changelog Currency**:
   No repository may be tagged or released with stale documentation or out-of-date changelogs.
   - **`CHANGELOG.md`**: Must adhere to [Keep a Changelog](https://keepachangelog.com/) and SemVer standards. It must contain the release version header (e.g. `## [1.6.0]`), the release date, architectural breakthroughs, API shifts, and detailed bullet points for all breaking or added functionality.
   - **`README.md` & Manpages**: Must document newly introduced flags, configuration options, and public C headers.
   - **Header Contracts (`include/ttypt/*.h`)**: Must include complete Doxygen/C-style contract comments for every newly exported function, enum, and struct.

3. **Zero Tolerance for Unchecked Releases**:
   Every release must be verified to have completed all packaging and publishing steps before releasing its direct dependents.

4. **Semantic Versioning & Bumping Rule**:
   **Bump minor only if public API changes. Otherwise bump patch.**
   - Minor increments are strictly required when public headers (`include/ttypt/*.h`), ABI structures, exported symbol tables, or public CLI options are added or modified.
   - Patch increments are reserved for internal refactorings, performance optimizations, bug fixes, documentation updates, and implementation improvements that leave public API signatures and header contracts unchanged.

---

## 2. Topological Dependency Layers

```
Layer 0: System Fundamentals
         └── libqsys (cross-platform OS, dlopen, memory, timer)

Layer 1: Storage Engine & UI Kernels
         ├── libcorm (memory-mapped columnar storage, arenas)
         └── libbud   (isomorphic UI binder, 60fps frame loop)

Layer 2: Harnesses & Corm Search Axes
         ├── libxylem (dynamic module loader, in-WASM hook dispatcher)
         ├── libjoint (time axis: interval trees, temporal validities)
         ├── libislet (space axis: 4D Morton space-filling curves)
         ├── libsepal (semantics axis: vector store, cosine ANN search)
         └── liblemma (lexical axis: lossless tokenizer, 3-gram index)

Layer 3: Multiplexing Server & Data Kernel
         ├── axil     (HTTP/WS server, framed streaming, socket bus)
         └── libhyle  (canonical schemas, query engine, token array storage)

Layer 4: Data Persistence & UI Bridge
         ├── libhyle-source (storage drivers: DSV, JSON, store_tok binary)
         └── libhyle-bud    (schema-to-DOM declarative bridge)

Layer 5: Axil Companion Services
         ├── axil-auth (PAM/shadow auth, session tokens, gating)
         └── axil-tty  (pure terminal capability bridge over axil)

Layer 6: Integrated Application Harnesses
         ├── axil-hyle (HTTP API layer for Hyle datasets)
         └── axil-nd   (MUCK engine core, protocol multiplexer)

Layer 7: Domain Modules & Tool Extensions
         ├── nd-*      (nd-attr, nd-biome, nd-equip, nd-fight, etc.)
         ├── pi-quest  (autonomous quest journal harness)
         └── pi-mm     (memory management & session tracking harness)

Layer 8: Main Platform Deployment
         └── site      (top-level isomorphic platform aggregator)
```

---

## 3. Pre-Flight Quality Gates

Before creating a Git tag and triggering the remote CI workflow, every repository must satisfy these checks:

### 3.1 Header & Portability Hygiene
- **Windows / MinGW (`winget`) Compatibility**:
  - **No unconditional `<dlfcn.h>`**: Windows lacks `<dlfcn.h>`. Dynamic symbols must be loaded through `<ttypt/qsys.h>` (`qsys_dlopen`, `qsys_dlsym`, `qsys_dlclose`) or guarded behind `#ifndef _WIN32`.
  - **GNU Extensions**: Define `#ifndef _GNU_SOURCE \n #define _GNU_SOURCE \n #endif` when using `dlsym(RTLD_DEFAULT, ...)`.
- **OpenBSD Portability**:
  - Ensure iconv, math (`-lm`), and socket libraries are conditionally linked per `Makefile` conventions.
- **WASM Safety**:
  - Isomorphic / guest components must not link native host POSIX symbols (`fork`, `exec`, `dlopen`, direct filesystem APIs).

### 3.2 Version & Manifest Parity
Verify that the intended tag version matches:
1. `include/ttypt/*.h` / `src/*.c` version macros (if any).
2. `pkg-config` `.pc` file `Version:` field.
3. `CHANGELOG.md` entry header (e.g., `## [1.6.0]`).
4. `README.md` badges or installation commands.

### 3.3 Local Build & Test Verification
Run the local test suite prior to release:
```bash
make clean
make test
```
The exit code must be `0` with zero test assertion failures and zero compilation warnings.

---

## 4. Release Execution Protocol (Step-by-Step)

### Step 1: Ensure Working Tree Cleanliness
```bash
git -C external/<repo> status -s
# Must output empty (0 uncommitted changes)
git -C external/<repo> log -1 --oneline
```

### Step 2: Create Annotated Semantic Version Tag
```bash
git -C external/<repo> tag -a vX.Y.Z -m "Release vX.Y.Z: <Summary of Key Features>"
```

### Step 3: Push Tag to Origin
```bash
git -C external/<repo> push origin vX.Y.Z
```

### Step 4: Monitor Pipeline Progress via GitHub CLI
```bash
# Check the newly queued workflow run
gh run list --repo tty-pt/<repo> -L 1

# Inspect real-time status of individual packaging jobs
gh run view <run-id> --repo tty-pt/<repo>
```

### Step 5: Verify All Packaging & Publishing Targets
Every release job runs through:
- `✓ build / test`
- `✓ build / package` across 10 matrix targets:
  - `winget` (Windows MinGW64)
  - `apk` (Alpine Linux)
  - `deb` (Ubuntu / Debian)
  - `rpm` (Rocky Linux)
  - `pacman` (Arch Linux)
  - `pacman_mingw` (MSYS2 / MinGW)
  - `brew x86_64` (macOS Intel)
  - `brew arm64` (macOS Apple Silicon)
  - `openbsd` (OpenBSD 7.x)
- `✓ build / release` (GitHub Releases asset upload)
- `✓ build / publish` (Package repository indexes updated)

### Step 6: Handling Transient Runner Flakes & Locking Hazards
- **Homebrew Remote Lock Contention**:
  - `publish (brew)` pushes formulas to `github.com/tty-pt/tap` and releases tarballs on GitHub Releases. It does not publish to `/var/www/htdocs`.
  - In `tty-pt/ci/publish/action.yml`, remote locking (`publish.lock`) against `/var/www/htdocs/brew` must be bypassed for `format == 'brew'` to avoid orphaned directory locks in the remote user's home directory.
- **OpenBSD SSH Network Flakes**: OpenBSD builds use an external runner host. If `publish (openbsd)` fails due to an SSH drop while all packages succeeded, rerun only the failed job:
  ```bash
  gh run rerun <run-id> --failed --repo tty-pt/<repo>
  ```
- **Rocky Linux (`rpm`) Duration**: The Rocky builder installs full dependency toolchains and may take 2–5 minutes. Allow it to finish naturally without cancellation.

---

## 5. Ecosystem Status & Wave Tracking Matrix

| Layer | Repository | Target Version | Status | Notes |
|:---:|:---|:---:|:---:|:---|
| **0** | `libqsys` | `v1.3.1` | **PUBLISHED** | `<ttypt/iso.h>` isomorphic C contract |
| **1** | `libcorm` | `v2.0.1` | **PUBLISHED** | WASM linear memory arena shims, Windows dl cleanup |
| **1** | `libbud` | `v1.0.2` | **PUBLISHED** | Namespaced imports, 60fps frame loop, universal slot |
| **2** | `libxylem` | `v1.6.0` | **PUBLISHED** | In-WASM core dispatcher, `XY_LISTENER_TAG` |
| **2** | `libjoint` | `v1.5.0` | **PUBLISHED** | Time axis: `rec_axis_store_typed` ISO date ingestion |
| **2** | `libislet` | `v1.2.1` | **PUBLISHED** | Space axis: 4D Morton encoding, portable build |
| **2** | `libsepal` | `v1.2.0` | **PUBLISHED** | Semantic axis: `rec_axis_store_typed` vector embed |
| **2** | `liblemma` | `v1.1.0` | **PUBLISHED** | Lexical axis: Lossless tokenizer, 3-gram index |
| **3** | `axil` | `v1.6.0` | **PUBLISHED** | Browser WebSocket bridge, multiplexing channels |
| **3** | `libhyle` | `v1.5.0` | **PUBLISHED** | Binary token array storage, rotating ring buffer |
| **4** | `libhyle-source` | `v1.2.0` | **PUBLISHED** | `store_tok` driver, `data.tok` tables |
| **4** | `libhyle-bud` | `v1.0.2` | **PUBLISHED** | Schema-driven pure-C isomorphic filters |
| **5** | `axil-auth` | `v1.2.1` | **PUBLISHED** | Port-qualified cookie auth |
| **5** | `axil-tty` | `v1.4.1` | **PUBLISHED** | Pure TTY capability bridge |
| **6** | `axil-hyle` | `v1.0.2` | **PUBLISHED** | Dynamic REST/FTS dataset HTTP handler |
| **6** | `axil-auren` | `v1.2.0` | **PUBLISHED** | Model registry, SystemOne classifier, block KV reuse |
| **6** | `axil-nd` | `v1.3.0` | Pending ND update | Decoupled UI presentation, MUCK core |
| **7** | `pi-quest` | `v1.2.0` | **PUBLISHED** | Autonomous quest journal & evidence gate |
| **7** | `pi-mm` | `v0.3.1` | **PUBLISHED** | Latent mipmap recall & cormd daemon |
| **7** | `nd-*` modules | `v1.0.2+` | Pending ND update | 16 NeverDark gameplay and domain modules |
| **8** | `site` | `Wave 2026.11` | Final Stage | Platform deployment and aggregator |

---

## 6. How to Resume the Release Wave

To execute subsequent release batches:
1. Inspect the matrix in Section 5.
2. Select the next unreleased layer whose upstream dependencies are all **PUBLISHED**.
3. Choose at most 2–3 repositories.
4. Execute Steps 1 through 6 from Section 4.
5. Update the status column in this document upon completion.
