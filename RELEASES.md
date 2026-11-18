# Releases

A running record of release waves: what went out, how, what broke, and what is left.
Times are UTC.

## Pre-Wave Baseline (`da9a2ef` — 2026-11-04)

Before the 2026-11-13..2026-11-15 release waves, commit `da9a2ef` (`feat(site): Release Wave 2026.12.1 — universal libstele WASM substrate, pure WASM libraries, and @tty-pt packages`, 2026-11-04 11:41 UTC) pinned each submodule at the following release tag and gitlink commit:

### Core Libraries, Daemons & Pi Extensions

| Submodule | Package | Pre-Wave Release Tag | Gitlink at `da9a2ef` (`git describe`) |
|---|---|---|---|
| `external/libstele` | `@tty-pt/stele` | `v1.5.2` | `96166c7` (`v1.5.2-1-g96166c7`) |
| `external/libcorm` | `@tty-pt/corm` | `v2.2.4` | `6b812b1` (`v2.2.4`, exact tag) |
| `external/libjoint` | `@tty-pt/joint` | `v1.6.3` | `efcfe50` (`v1.6.3`, exact tag) |
| `external/libislet` | `@tty-pt/islet` | `v1.3.2` | `4af45e4` (`v1.3.2`, exact tag) |
| `external/liblemma` | `@tty-pt/lemma` | `v1.2.2` | `7af2e01` (`v1.2.2`, exact tag) |
| `external/libsepal` | `@tty-pt/sepal` | `v1.3.2` | `ed44a47` (`v1.3.2`, exact tag) |
| `external/libhyle` | `@tty-pt/hyle` | `v1.6.1` | `a1b83da` (`v1.6.1`, exact tag) |
| `external/libbud` | `@tty-pt/bud` | `v1.2.2` | `c355ea2` (`v1.2.2`, exact tag) |
| `external/libxylem` | `@tty-pt/xylem` | `v1.7.2` | `f070d93` (`v1.7.2-1-gf070d93`) |
| `external/libhyle-source` | `@tty-pt/hyle-source` | `v1.3.1` *(OS packages only; no npm package yet)* | `3574c67` (`v1.3.1`, exact tag) |
| `external/libhyle-bud` | `@tty-pt/hyle-bud` | `v1.1.1` *(not yet published to npm)* | `10f913d` (`v1.1.1`, exact tag) |
| `external/axil` | `@tty-pt/axil` | `v1.7.1` *(npm `package.json` at `1.7.0`, unpublished)* | `ee6b04b` (`v1.7.1-2-gee6b04b`) |
| `external/axil-auth` | `axil-auth` | `v1.3.1` | `a8dbdf8` (`v1.3.1`, exact tag) |
| `external/axil-hyle` | `axil-hyle` | `v1.1.1` | `c2cdddc` (`v1.1.1-1-gc2cdddc`) |
| `external/axil-nd` | `axil-nd` | `v1.3.2` | `b4eef1c` (`v1.3.2`, exact tag) |
| `external/axil-tty` | `@tty-pt/axil-tty` | `v1.5.1` *(root `package.json` at `1.3.3`)* | `8f2a71c` (`v1.5.1-2-g8f2a71c`) |
| `external/axil-auren` | `@tty-pt/auren` | `v1.4.0` *(standalone repo; npm `package.json` at `1.2.0`, unpublished)* | `7616029` (`v1.4.0`) / `f35584c` (`v1.4.0-2-gf35584c`) |
| `external/mk` | `@tty-pt/mk` | `v0.2.0` *(npm at `0.1.18`)* | `648e90f` (`v0.2.0-1-g648e90f`) |
| `.pi/extensions/pi-mm` | `@tty-pt/pi-mm` | `v0.6.0` | `77aa4be` (`v0.6.0`, exact tag) |
| `.pi/extensions/pi-quest` | `@tty-pt/pi-quest` | `v1.10.1` | `57c90ec` (`v1.10.1-3-g57c90ec`) |
| `external/pi-instruct` | `pi-instruct` | `v1.0.0` | `cbf8b06` (`v1.0.0`, exact tag) |

### Submodules Added or Converted After `da9a2ef`

| Submodule | Package | State at `da9a2ef` (2026-11-04) | First Tag Before 2026-11-13 Wave |
|---|---|---|---|
| `external/liblamina` | `@tty-pt/lamina` | Tracked in-tree (not yet a gitlink; standalone repo had `v1.0.2` from Apr 2026 and `v1.0.1` at `7d1a604` on 2026-11-04) | `v1.0.0` (`8953ed7`, 2026-11-11) |
| `external/libpitch` | `@tty-pt/pitch` | In-tree code in `mods/song`; extracted to submodule after `da9a2ef` | `v1.0.0` (`24b7ce4`, 2026-11-11) |
| `external/libchaff` | `@tty-pt/chaff` | Did not exist at `da9a2ef` | `v1.0.0` (`f09421e`, 2026-11-11) |
| `external/libareole` | `@tty-pt/areole` | Did not exist at `da9a2ef` | `v1.0.0` (`56b1f55`, 2026-11-11) |
| `external/libhyle-react` | `@tty-pt/hyle-react` | Did not exist at `da9a2ef` | `v1.0.0` (`461f92c`, 2026-11-13) |

### NeverDark (`external/nd-*`) Submodules at `da9a2ef`

All 17 `external/nd-*` gitlinks at `da9a2ef` were 1 commit ahead of their pre-wave release tag (`refactor(nd): Update stllog comments, remove add-prefix paths, and clean dependencies`):

| Submodule | Pre-Wave Release Tag | Gitlink at `da9a2ef` (`git describe`) |
|---|---|---|
| `external/nd-attr` | `v1.0.2` | `b4d5d87` (`v1.0.2-1-gb4d5d87`) |
| `external/nd-biome` | `v1.0.2` | `f4fd251` (`v1.0.2-1-gf4fd251`) |
| `external/nd-class` | `v1.0.3` | `389362f` (`v1.0.3-1-g389362f`) |
| `external/nd-core` | `v1.0.3` | `8d71c18` (`v1.0.3-1-g8d71c18`) |
| `external/nd-drink` | `v1.0.8` | `1d57cc4` (`v1.0.8-1-g1d57cc4`) |
| `external/nd-equip` | `v1.0.3` | `fa4acc1` (`v1.0.3-1-gfa4acc1`) |
| `external/nd-fight` | `v1.0.3` | `97c8c39` (`v1.0.3-1-g97c8c39`) |
| `external/nd-level` | `v1.0.2` | `1cbec50` (`v1.0.2-1-g1cbec50`) |
| `external/nd-mob` | `v1.0.3` | `87fc936` (`v1.0.3-1-g87fc936`) |
| `external/nd-mortal` | `v1.0.3` | `651b316` (`v1.0.3-1-g651b316`) |
| `external/nd-other` | `v1.0.2` | `56e62e2` (`v1.0.2-1-g56e62e2`) |
| `external/nd-plant` | `v1.0.3` | `c7164df` (`v1.0.3-1-gc7164df`) |
| `external/nd-race` | `v1.0.2` | `ca5be5b` (`v1.0.2-1-gca5be5b`) |
| `external/nd-seat` | `v1.0.3` | `5a991bf` (`v1.0.3-1-g5a991bf`) |
| `external/nd-shop` | `v1.0.2` | `6a1599c` (`v1.0.2-1-g6a1599c`) |
| `external/nd-spell` | `v1.0.8` | `b6e92c9` (`v1.0.8-1-gb6e92c9`) |
| `external/nd-stone` | `v1.0.2` | `29c7bd4` (`v1.0.2-1-g29c7bd4`) |
| `external/nd-vanilla` | `v1.0.2` | `617b279` (`v1.0.2-1-g617b279`) |
| `external/nd-wts` | `v1.0.2` | `5f20248` (`v1.0.2-1-g5f20248`) |

## 2026-11-13

### What was released

Published to npm (registry timestamps). Every package's other formats (deb, rpm, apk, pacman,
brew, OpenBSD, winget where the repository builds them) were published by the tag run.

| Package | Versions published today | Latest |
|---|---|---|
| `@tty-pt/stele` | 1.6.0, 1.6.1 (17:34), 1.6.2 (21:41) | 1.6.2 |
| `@tty-pt/corm` | 2.3.0, 2.3.1, 2.3.2 (17:53), 2.3.3 (21:55) | 2.3.3 |
| `@tty-pt/joint` | 1.7.0, 1.7.2 (19:31), 1.7.3 (22:29) | 1.7.3 |
| `@tty-pt/islet` | 1.4.0, 1.4.1, 1.5.0, 1.5.1 (19:21), 1.5.2 (22:19) | 1.5.2 |
| `@tty-pt/lemma` | 1.3.0, 1.3.1 (19:20), 1.3.2 (22:28) | 1.3.2 |
| `@tty-pt/sepal` | 1.4.0, 1.4.1 (19:20), 1.4.2 (22:14) | 1.4.2 |
| `@tty-pt/hyle` | 1.7.0, 1.7.2 (19:53), 1.7.3 (23:00) | 1.7.3 |
| `@tty-pt/bud` | 1.3.0 (21:58) | 1.3.0 |
| `@tty-pt/xylem` | 1.8.0 (23:10) | 1.8.0 |
| `@tty-pt/hyle-source` | 1.4.0 (23:32), first publish | 1.4.0 |
| `@tty-pt/lamina` | 1.1.0 (23:41), first publish | 1.1.0 |
| `@tty-pt/hyle-react` | 1.0.0 (23:59), first publish through `npm.yml` | 1.0.0 |

| `@tty-pt/hyle-bud` | 1.2.0 (00:12 on 2026-11-14), first publish | 1.2.0 |
| `@tty-pt/axil` | 1.8.0 (00:29 on 2026-11-14), first publish | 1.8.0 |
| `axil-auth` (OS packages) | 1.3.2 (00:50 on 2026-11-14) | 1.3.2 |
| `@tty-pt/axil-tty` | 1.6.0 (00:50 on 2026-11-14) | 1.6.0 |
| `@tty-pt/auren` | 1.5.0 (01:12 on 2026-11-14), first publish | 1.5.0 |

Notes:
- `@tty-pt/joint` 1.7.1 and `@tty-pt/hyle` 1.7.1 were tagged but never reached npm (1.7.0 went to
  1.7.2).
- Tags moved and re-pushed so CI would rebuild with a fixed workflow: `libjoint` v1.7.3, `liblemma`
  v1.3.2, `libhyle-source` v1.4.0 (twice), `liblamina` v1.1.0, `axil` v1.8.0, `axil-auren` v1.5.0.
  Each republished every package format over the previous one.
- `axil-auren`'s npm package previously had its own number (1.2.0, never published). It now carries
  the repository version (1.5.0), matching the tag and `.pc` version parity rule.

Dependants updated to the new versions: `duna` now uses npm for every `@tty-pt/*` package it
depends on (`corm`, `hyle`, `hyle-react`, `hyle-source`, `islet`, `joint`, `lemma`, `sepal`,
`stele`) with no `file:` links. Unit tests 56/56, `next build` compiles. `nd` requires
`@tty-pt/stele ^1.6.2`.

### How releases work now

Pushing a `v*` tag runs `tty-pt/ci`'s `all-in-one.yml`:

1. `test`, then `package` for each format in `publish_to`.
2. When `npm/native/*_napi.c` exists, the `rpm`, `brew` and `winget` jobs also build one
   self-contained Node-API prebuild (`scripts/build-node-addon.sh`) and upload it as
   `node-prebuild-<os>-<cpu>`.
3. `publish` pushes the OS packages.
4. `npm` (the reusable `.github/workflows/npm.yml`) packs ONE tarball (WebAssembly build plus the
   prebuilds), installs it into an empty project on Linux, macOS (arm64, x64) and Windows, loads it,
   and publishes that same tarball.

A repository that is only an npm package (`libhyle-react`) calls `npm.yml` directly and has no
package matrix.

Rules that fell out of this:
- The npm package is detected from `npm/package.json` (or root `package.json` when non-private, as in
  `axil-tty`, via the `dir` input in `npm.yml`). Nothing is declared in the caller.
- `package.json` version, tag, and `.pc` versions must agree (`scripts/check-pkgconfig-versions.sh`).
- Platforms that get a native addon: `rpm` gives linux-x64 (oldest glibc), `brew` gives
  darwin-arm64 and darwin-x64, `winget` gives win32-x64. Anything else uses WebAssembly.
- `npm/native/LIBS` overrides the addon's linker flags (it may be empty). Default: the `-l` flags of
  the root `*.pc` `Libs:` line plus the repository's own library.
- A missing `NPM_TOKEN` on a publishing run is now an error, not a skip.
- Callers must use `secrets: inherit`. An explicit secrets list silently dropped `NPM_TOKEN`.

### Problems met and how they were fixed

- **`build:native` on the npm runner.** `node_api.h` and `json-c` headers were not found there.
  Native addons are now built in the per-platform package jobs, never on the npm runner.
- **`mk` WASM include path.** `include.mk` used `?=` for `WASM_CFLAGS`, which `portable.mk` had
  already set, so `${npm_inc}` was never applied. It is `+=` now.
- **CRLF `.pc` files on Windows.** `-lstele` became `-lstele<CR>`. The script strips `\r`.
- **Silent exit on Windows.** A `cmd && echo` as the last command of a pipeline loop returns 1 under
  `set -e`. Rewritten with `if`.
- **Missing `libxxhash.dll` on Windows** (`libjoint`, `liblemma`). `libcorm.dll` imports it, and the
  winget job did not install xxhash, so the bundler skipped it as "a system DLL". The bundler now
  fails naming the DLL, and those repositories' winget jobs install `mingw-w64-x86_64-xxhash`. The
  same fix was applied pre-emptively to `libhyle`, `libxylem`, and `axil`.
- **DLL data import of `stllog` on Windows** (`axil`). `axil_init()` assigned `stllog = stl_syslog`
  directly, which failed to link `libaxil.dll` under mingw (`undefined reference to 'stllog'`).
  Replaced with `stllog_set(stl_syslog)`.
- **`npm publish pkg/x.tgz`** is read by npm as a GitHub `user/repo`. The path must start with `./`.
- **Registry lag.** New versions took 1 to 5 minutes to show in `npm view`; a brand-new package
  longer. Check the registry API (`curl https://registry.npmjs.org/@tty-pt%2f<name>`) before
  concluding a publish failed.
- **Intermittent `ssh` exit 255 in `publish` jobs** (`libbud` openbsd, `libislet` openbsd,
  `libjoint` winget). It happened when two releases published at the same time. Tags were then
  pushed several minutes apart, and failed jobs were re-run. The cause is not confirmed; see
  `PLAN-publish-traffic-and-locks.md` in `tty-pt/ci`.
- **Re-running a run does not pick up workflow changes.** It reuses the caller workflow from the
  tagged commit, so a fix to `ci.yml` needs a new tag (or deleting and re-pushing the tag).
- **`axil-auren` v1.5.0 required `axil` 1.8.0 and `<ttypt/stele.h>` migration.** Released `axil`
  1.8.0 first (replacing the old `/usr/include/ttypt/axil.h` that still included `<ttypt/qsys.h>`),
  migrated remaining `<ttypt/qsys.h>` / `qsyslog` references in `axil-auren/src/` and
  `axil-auren/tests/mocks/ttypt/stele.h` to `<ttypt/stele.h>` / `stl_syslog`, added
  `submodules: true` to `tty-pt/ci/test/action.yml`, and added `-Inpm/node_modules/@tty-pt/stele/include`
  to `WASM_CFLAGS` in `axil-auren/Makefile`.
- **`axil-tty` v1.6.0 root package and Node ESM verification.** Extended `npm.yml` with `dir: .`
  for root `package.json` repos, guarded browser globals (`window`, `location`, `WebSocket`,
  `FitAddon`, `Terminal`) in `axil-tty.js`, removed its top-level `.css` import, and made
  `cjs/axil-tty.js` Node-20 safe without `ERR_REQUIRE_ESM`.
- **`libjoint` WebAssembly state.** In WebAssembly mode every `JointTimeline` shared one
  interval buffer. The constructor and `close()` now clear it. A per-timeline handle in the C code
  would be the proper fix.

## 2026-11-14

### What was released

Two follow-up waves went out on 2026-11-14 for the core memory/data libraries alongside `@tty-pt/pi-mm`
and `@tty-pt/pi-quest`:

1. **Wave 1 (15:42–16:33 UTC — `.0` full N-API bindings & fallback removal)**:
   - Added `-musl` platform key detection (`isMuslLinux()`, `getPlatformPrebuildKeys()`) in `@tty-pt/stele` 1.7.0 (`43dbfb1`) and `tty-pt/ci/scripts/build-node-addon.sh`.
   - Removed all simplified JS `Map` fallbacks in `@tty-pt/{corm,joint,islet,lemma,sepal}` and replaced them with full N-API bindings (`corm` 2.4.0, `joint` 1.8.0, `islet` 1.6.0, `lemma` 1.4.0, `sepal` 1.5.0).
2. **Wave 2 (21:20–21:32 UTC — `.1` prebuild discovery, `RTLD_GLOBAL`, & extension releases)**:
   - Added `resolveAxisPrebuildDirs()`, `RTLD_NOW | RTLD_GLOBAL` native shared library preloading (for `libstele` and `libcorm`), and quiet-open handling in `@tty-pt/stele` 1.7.1, `@tty-pt/corm` 2.4.1, `@tty-pt/joint` 1.8.1, `@tty-pt/islet` 1.6.1, `@tty-pt/lemma` 1.4.1, and `@tty-pt/sepal` 1.5.1.
   - Published `@tty-pt/pi-mm` 0.7.1 (with `NativeAddonCormRunner` and verified `MIN_ENGINE_VERSIONS` / `REQUIRED_ENGINE_FEATURES`) and `@tty-pt/pi-quest` 1.12.0.

| Package | Versions published on 2026-11-14 | Latest |
|---|---|---|
| `@tty-pt/stele` | 1.7.0 (15:42), 1.7.1 (21:24) | 1.7.1 |
| `@tty-pt/corm` | 2.4.0 (16:22), 2.4.1 (21:24) | 2.4.1 |
| `@tty-pt/joint` | 1.8.0 (16:30), 1.8.1 (21:29) | 1.8.1 |
| `@tty-pt/islet` | 1.6.0 (16:30), 1.6.1 (21:25) | 1.6.1 |
| `@tty-pt/lemma` | 1.4.0 (16:33), 1.4.1 (21:32) | 1.4.1 |
| `@tty-pt/sepal` | 1.5.0 (16:29), 1.5.1 (21:26) | 1.5.1 |
| `@tty-pt/pi-mm` | 0.7.1 (21:20) *(tag `v0.7.0` also created, published as `0.7.1`)* | 0.7.1 |
| `@tty-pt/pi-quest` | 1.12.0 (21:20) | 1.12.0 |

## 2026-11-15 / 2026-11-16

### What was released (`@tty-pt/pi-mm` `0.8.0` & Memory/Daemon Prebuild Wave)

Following the 2026-11-15 history consolidation and prebuild/daemon fixes, the 6 upstream C native
engines/daemons and `@tty-pt/pi-mm` were tagged and published in dependency order (Wave A1 $\rightarrow$
Wave A2 $\rightarrow$ Wave B $\rightarrow$ Wave C):

| Package | Versions published (UTC) | Latest | OS Formats Published (`publish_to`) | Highlights |
|---|---|---|---|---|
| `@tty-pt/axil` (`external/axil`) | `1.8.1` (23:44 on 2026-11-15), `1.8.2` (00:56 on 2026-11-16), `1.8.3` (02:06 on 2026-11-16) | `1.8.3` | `deb, apk, rpm, pacman, brew, openbsd, winget` | Reusable `axil_cli(argc, argv)`, `npm/native/axil_napi.c`, `npm/bin/axil.js`, `-m` Xylem prebuild module resolver (`resolveModulePath`, `normalizeCliArgs`, `runCli`), `axil_fd_tick`; `1.8.2` added `pacman` (Arch Linux) to `publish_to`; `1.8.3` fixed 8KB `request_handle_autoindex` truncation & 2-byte `Content-Length` overcount in `src/libaxil.c` and rebuilt OpenBSD package against `libxylem` `1.8.1` |
| `@tty-pt/corm` (`external/libcorm`) | `2.4.2` (23:45 on 2026-11-15) | `2.4.2` | `deb, apk, rpm, pacman, brew, openbsd, winget` | 64-slot N-API engine cache with LRU eviction, late axis binding + `@roster` sidecar persistence, `CORM_QUIET` support, standalone `$ORIGIN` RPATH |
| `@tty-pt/sepal` (`external/libsepal`) | `1.5.2` (23:56 on 2026-11-15) | `1.5.2` | `deb, apk, rpm, pacman, brew, openbsd, winget` | In-memory `sepal-embed-cache.bin` caching, curl connect/request timeouts (`SEPAL_EMBED_CONNECT_TIMEOUT_MS` / `SEPAL_EMBED_TIMEOUT_MS`), multiline JSON control-char escaping, `:b` blob text `[SUMMARY]` embedding support, `CORM_QUIET` |
| `@tty-pt/lemma` (`external/liblemma`) | `1.4.2` (23:57 on 2026-11-15) | `1.4.2` | `deb, apk, rpm, pacman, brew, openbsd, winget` | Gated `rec_axis_open` cold-start stderr diagnostics on `CORM_QUIET`, standalone prebuild RPATH |
| `@tty-pt/islet` (`external/libislet`) | `1.6.2` (23:57 on 2026-11-15) | `1.6.2` | `deb, apk, rpm, pacman, brew, openbsd, winget` | Packaged `<ttypt/islet_quad.h>` (`morton_quad_pack`, `islet_quad_map_*`) and `islet_box_is_empty` |
| `@tty-pt/auren` (`external/axil-auren`) | `1.5.1` (00:20 on 2026-11-16), `1.5.2` (01:36 on 2026-11-16), `1.5.3` (19:15 on 2026-11-16) | `1.5.3` | `deb, apk, rpm, pacman, brew, openbsd, winget` | `npm/native/auren_napi.c` bundling prebuilt `libaxil-auren.{so,dylib,dll}`, `getModulePath()`, `$ORIGIN` RPATH, `@tty-pt/axil ^1.8.1`, `llama_memory_clear` KV reuse; `1.5.2` added `pacman` (Arch Linux) to `publish_to`, `curl` fallback for Vulkan SDK, and GCC fallback when container `clang` ABI mismatches; `1.5.3` added `apk`, `openbsd`, and `winget` (`win32-x64` N-API prebuild) for full 7-OS + 4-platform N-API coverage |
| `@tty-pt/pi-mm` (`.pi/extensions/pi-mm`) | `0.8.0` (00:15 on 2026-11-16) | `0.8.0` | *(npm only)* | Multi-axis codebase mipmap graph (`scope=code`), ephemeral 3-tier session memory & mipmap compaction (`scope=session`), `diagnose` failure trace intersection, `guardrail` pre-tool checks, `lore-bindings.json` staleness detection, swarm stigmergy, module dossiers, verification-attested ledger, re-entrant `withLock`, `CORM_QUIET`, and standalone `@tty-pt/{axil,auren,corm,sepal,lemma,islet,joint,stele}` prebuild discovery |

### Follow-Up Wave (`2026-11-16` — `pacman` & `winget` OS Package Expansion)

Following the `publish_to` audit, patch releases were cut across the remaining core libraries, Web HAL
libraries, and Unix daemons to add `pacman` (Arch Linux) and `winget` (Windows + `win32-x64` N-API
prebuilds) wherever supported:

| Package | Version Published (UTC) | OS Formats Published (`publish_to`) | Changes in Patch Release |
|---|---|---|---|
| `@tty-pt/lamina` (`external/liblamina`) | `1.1.1` (00:52 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Added `pacman` to `publish_to` and `deps_pacman: "libstele,libcorm,glfw,libpng,glew"`; published `@tty-pt/stele >=1.7.0` upgrade and offscreen FBO/stencil WASM build |
| `@tty-pt/axil` (`external/axil`) | `1.8.2` (00:56 on 2026-11-16), `1.8.3` (02:06 on 2026-11-16), `1.8.4` (02:55 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Added `pacman` to `publish_to` and `deps_pacman: "libstele,libcorm,libxylem"` (omitting `openssl` from `deps_pacman` to prevent Arch container partial-upgrade `libssl.so.3` breakage); `1.8.3` fixed 8KB `request_handle_autoindex` buffer truncation in `src/libaxil.c` and rebuilt OpenBSD package against `libxylem` `1.8.1`; `1.8.4` prioritized `libaxil-<shortName>` and `@tty-pt/axil-<shortName>` in `resolveModulePath()` so `-m hyle` and `-m auth` resolve to `libaxil-hyle` and `libaxil-auth` |
| `@tty-pt/hyle` (`external/libhyle`) | `1.7.4` (00:57 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Rebuilt OS packages (including OpenBSD `libhyle-1.7.4.tgz`) against `libstele` `1.7.1` and `libcorm` `2.4.2`; upgraded `@tty-pt/stele >=1.7.0` |
| `@tty-pt/hyle-source` (`external/libhyle-source`) | `1.4.1` (00:59 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Added `winget` (Windows + `win32-x64` N-API prebuild) to `publish_to`; added `_WIN32` shims in `src/store_fs.c`, `src/source_utils.c`, `src/store_tok.c`, and `src/json.c`; linked `-liconv` on Windows and `-llemma -lcorm -lstele` for `hyle-tok.exe` |
| `@tty-pt/axil-tty` (`external/axil-tty`) | `1.6.1` (01:02 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd` | Added `pacman` to `publish_to` and `deps_pacman: "axil,libcorm,libxylem,libstele"` |
| `@tty-pt/axil-auth` (`external/axil-auth`) | `1.3.3` (01:02 / 02:10 on 2026-11-16), `1.3.4` (02:55 on 2026-11-16), first npm publish | `deb, apk, rpm, pacman, brew, openbsd` | `1.3.3` added `pacman` to `publish_to` (`deps_pacman: "axil,libcorm,libxylem,libstele"`) and rebuilt OpenBSD package; `1.3.4` added colocated `npm/` package (`@tty-pt/axil-auth`) with N-API addon (`axil_auth.node`), bundled `libaxil-auth.{so,dylib}`, `getModulePath()`, cookie/session helpers, and `<ttypt/{auth.h,auth-config.h,auth-outcome.h}>` C headers |
| `@tty-pt/bud` (`external/libbud`) | `1.3.2` (01:12 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Guarded `__attribute__((weak))` on `bud_patch_attr`, `bud_patch_text`, and `bud_app_set_state` with `#ifndef _WIN32` in `src/libbud.c` so PE/COFF `libbud.dll.a` exports them on Windows; rebuilt against `libstele` `1.7.1` |
| `@tty-pt/xylem` (`external/libxylem`) | `1.8.1` (01:12 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Rebuilt OS packages against `libstele` `1.7.1` and `libcorm` `2.4.2`; published disjoint `--global-base=1048576` `xylem.wasm` reactor |
| `@tty-pt/hyle-bud` (`external/libhyle-bud`) | `1.2.1` (01:15 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Added `winget` (Windows + `win32-x64` N-API prebuild) to `publish_to` and `LDLIBS-libhyle-bud-Windows := -lhyle-source -lstele` in `Makefile` |
| `@tty-pt/auren` (`external/axil-auren`) | `1.5.2` (01:36 on 2026-11-16), `1.5.3` (19:15 on 2026-11-16) | `deb, apk, rpm, pacman, brew, openbsd, winget` | `1.5.2` added `pacman` to `publish_to`, `curl` fallback for Vulkan SDK download, and `clang --version` check in `Makefile` so Arch containers fall back to GCC when `clang` hits `GLIBC_2.44` mismatch; `1.5.3` added `apk` (system `glslc`/`shaderc`, incremental `.spv` check in `scripts/vkgen-repair.sh`, `install-hook` preserving `gzip-man-Linux`), `openbsd` (`comp<REL>.tgz` C++ bootstrap, `<sys/socket.h>`/`<netinet/in.h>`, `install -m 755`), and `winget` (`_WIN32` condition-variable wakeup + `on_axil_update` completion drain, Winsock2, `-Wl,--export-all-symbols`, and bundled OpenSSL 3 DLLs for `win32-x64` N-API prebuild) |
| `@tty-pt/axil-hyle` (`external/axil-hyle`) | `1.1.2` (02:15 on 2026-11-16), `1.1.3` (02:55 on 2026-11-16), `1.1.4` (03:50 on 2026-11-16), first npm publish | `deb, apk, rpm, pacman, brew, openbsd, winget` | `1.1.2` added `pacman` to `publish_to` and `secrets: inherit`; `1.1.3` added colocated `npm/` package (`@tty-pt/axil-hyle`) with N-API addon (`axil_hyle.node`), bundled `libaxil-hyle.{so,dylib}`, `AxilHyleClient`, `getModulePath()`, `xy_install(void)`, and `<ttypt/axil-hyle.h>` C header; `1.1.4` added `_WIN32` shims (`strtok_r`, `strndup`, `S_ISDIR`), `#if __has_include(<ttypt/auth.h>)` fallback, and `winget` (`win32-x64` N-API prebuild with `libaxil-hyle.dll`) |
| `@tty-pt/pitch` (`external/libpitch`) | `1.0.1` (03:50 on 2026-11-16), first npm & 7-OS publish | `deb, apk, rpm, pacman, brew, openbsd, winget` | Added `.github/workflows/ci.yml` (`publish_to: "deb,apk,rpm,pacman,brew,openbsd,winget"`), portable `SONAME-libpitch := libtransp` across `.so`/`.dylib`/`.dll`, `INSTALL_BIN :=`, and `LDLIBS-alpine :=` |
| `@tty-pt/chaff` (`external/libchaff`) | `1.1.0` (03:50 on 2026-11-16), first npm & 7-OS publish | `deb, apk, rpm, pacman, brew, openbsd, winget` | Migrated `Makefile` to `include ../mk/include.mk` (`SONAME-libchaff := libnoise`), inlined `XXH32` in `tests/test_glsl.c` via `<ttypt/noise.h>`, added `repository` metadata to `npm/package.json`, and added `.github/workflows/ci.yml` (`publish_to: "deb,apk,rpm,pacman,brew,openbsd,winget"`) |
| `@tty-pt/areole` (`external/libareole`) | `1.1.0` (03:50 on 2026-11-16), first npm & 7-OS publish | `deb, apk, rpm, pacman, brew, openbsd, winget` | Migrated `Makefile` to `include ../mk/include.mk`, added `src/libareole.c` and `npm/native/areole_napi.c` (shipping `linux-x64`, `darwin-arm64`, `darwin-x64`, `win32-x64` N-API prebuilds alongside `areole.wasm`), added `repository` metadata to `npm/package.json`, and added `.github/workflows/ci.yml` (`publish_to: "deb,apk,rpm,pacman,brew,openbsd,winget"`) |

### OS Package & npm Prebuild Matrix Across Repositories (Post-Expansion & `tty-pt/ci` `11b426e`)

Every repository pushed through `tty-pt/ci/.github/workflows/all-in-one.yml` builds up to **7 OS package formats** (`deb`, `apk`, `rpm`, `pacman`, `brew` [arm64 + x86_64], `openbsd`, `winget`) plus **1 unified `@tty-pt/*` npm tarball** containing WebAssembly (`*.wasm`), C headers (`include/`), and up to **6 per-platform Node-API prebuilds** (`linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`, `win32-x64`).

| Tier / `publish_to` Configuration | Published OS Formats | npm Prebuilds & Runtimes | Skipped OS Formats & Reason | Repositories |
|---|---|---|---|---|
| **Tier A**: `"deb,apk,rpm,pacman,brew,openbsd,winget"` | All 7 (`deb`, `apk`, `rpm`, `pacman`, `brew`, `openbsd`, `winget`) | `linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`, `win32-x64` + WASM *(except `libmm` and `axil-hyle`, which are native N-API + CLI/HTTP bridges without WASM, and `libhyle-bud`, which is WASM + JS without N-API)* | None *(all 7 supported OS package managers enabled)* | **`libstele`**, **`libcorm`**, **`libjoint`**, **`libislet`**, **`liblemma`**, **`libsepal`**, **`libmm`**, **`libhyle`**, **`libbud`**, **`libxylem`**, **`liblamina`**, **`libhyle-source`**, **`libhyle-bud`**, **`axil`**, **`axil-hyle`**, **`axil-auren`**, **`libpitch`**, **`libchaff`**, **`libareole`** (19 repos) |
| **Tier B**: `"deb,apk,rpm,pacman,brew,openbsd"` | All 6 Unix formats (`deb`, `apk`, `rpm`, `pacman`, `brew`, `openbsd`) | `linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64` (`axil-auth`); ESM/CJS browser+Node (`axil-tty`) | `winget` *(POSIX-only daemons using `fork`/`pty`/`termios`/`sys/un.h`/`sys/wait.h`)* | **`axil-auth`**, **`axil-tty`** |
| **Tier C**: `"deb,apk,rpm,brew,openbsd"` | `deb`, `apk`, `rpm`, `brew`, `openbsd` | None *(no `npm/` package)* | `pacman`, `winget` *(unreleased game modules / server)* | **`axil-nd`** and all **17 `nd-*` modules** |
| **Tier D**: Pure JS / TS / Build (`npm.yml`) | None *(npm only)* | Universal ESM/TS | All OS formats | **`@tty-pt/hyle-react`**, **`@tty-pt/pi-mm`**, **`@tty-pt/pi-quest`**, **`@tty-pt/mk`** |

### GitHub Releases & Git Tags Consolidation (`2026-11-16`)

All 25 released library, daemon, and extension repositories under `tty-pt/*` were consolidated so that each repository carries **at most two GitHub Release pages**:
1. **Release 1 (First Branded Release)**: The initial release from the 2026-10-20 / 2026-10-21 branded wave (with `logo.png` / `cover.png`).
2. **Release 2 (Latest Release)**: The latest release page, updated via `tty-pt/ci/scripts/release-notes.sh <CHANGELOG.md> <latest_tag> [first_tag]` so its body joins every intermediate `CHANGELOG.md` entry strictly after Release 1 up to Release 2 (plus the `CHECKSUMS` verification block where applicable).

All intermediate and draft GitHub Release pages and all intermediate git tags strictly between Release 1 and Release 2 (plus un-released `v1.0.0` tags on the 1-release repositories `libareole`, `libchaff`, and `libpitch`, and the un-prefixed `1.2.0` tag on `axil-auren`) were pruned from GitHub and local clones after backfilling any missing release notes into `CHANGELOG.md` (`libstele` `[1.6.2]`, `libislet` `[1.3.0]`, `liblemma` `[1.2.2]`, `libsepal` `[1.3.2]`, and stripping duplicate inline `cover.png` `<img>` tags from `pi-mm` `[0.3.3]` and `pi-quest` `[1.3.0]`).

| Repository | Release 1 (First Branded) | Release 2 (Latest) | Joined `CHANGELOG.md` Sections in Latest Release |
| :--- | :--- | :--- | :--- |
| `libstele` (`external/libstele`) | `v1.3.0` | `v1.7.1` | `[1.7.1]`, `[1.7.0]`, `[1.6.2]`, `[1.6.1]`, `[1.6.0]`, `[1.5.2]`, `[1.4.0]` |
| `libcorm` (`external/libcorm`) | `v2.0.0` | `v2.4.2` | `[2.4.2]`, `[2.4.1]`, `[2.4.0]`, `[2.3.3]`, `[2.3.2]`, `[2.3.1]`, `[2.3.0]`, `[2.2.4]`, `[2.1.0]` |
| `libjoint` (`external/libjoint`) | `v1.4.0` | `v1.8.1` | `[1.8.1]`, `[1.8.0]`, `[1.7.3]`, `[1.7.2]`, `[1.7.1]`, `[1.7.0]`, `[1.6.3]` |
| `libislet` (`external/libislet`) | `v1.2.0` | `v1.6.2` | `[1.6.2]`, `[1.6.1]`, `[1.6.0]`, `[1.5.2]`, `[1.5.1]`, `[1.5.0]`, `[1.4.1]`, `[1.4.0]`, `[1.3.2]`, `[1.3.0]` |
| `liblemma` (`external/liblemma`) | `v1.0.0` | `v1.4.2` | `[1.4.2]`, `[1.4.1]`, `[1.4.0]`, `[1.3.2]`, `[1.3.1]`, `[1.3.0]`, `[1.2.2]` |
| `libsepal` (`external/libsepal`) | `v1.1.0` | `v1.5.2` | `[1.5.2]`, `[1.5.1]`, `[1.5.0]`, `[1.4.2]`, `[1.4.1]`, `[1.4.0]`, `[1.3.2]` |
| `libmm` (`external/libmm`) | — | `v1.0.0` | `[1.0.0]` |
| `libhyle` (`external/libhyle`) | `v1.4.0` | `v1.7.4` | `[1.7.4]`, `[1.7.3]`, `[1.7.2]`, `[1.7.1]`, `[1.7.0]`, `[1.6.1]` |
| `libbud` (`external/libbud`) | `v1.0.1` | `v1.3.2` | `[1.3.2]`, `[1.3.1]`, `[1.3.0]`, `[1.2.2]`, `[1.1.0]` |
| `libxylem` (`external/libxylem`) | `v1.5.0` | `v1.8.1` | `[1.8.1]`, `[1.8.0]`, `[1.7.2]` |
| `libhyle-source` (`external/libhyle-source`) | `v1.1.0` | `v1.4.1` | `[1.4.1]`, `[1.4.0]`, `[1.3.1]` |
| `libhyle-bud` (`external/libhyle-bud`) | `v1.0.1` | `v1.2.1` | `[1.2.1]`, `[1.2.0]`, `[1.1.1]` |
| `liblamina` (`external/liblamina`) | `v1.0.0` | `v1.1.1` | `[1.1.1]`, `[1.1.0]`, `[1.0.1]` |
| `axil` (`external/axil`) | `v1.5.0` | `v1.8.4` | `[1.8.4]`, `[1.8.3]`, `[1.8.2]`, `[1.8.1]`, `[1.8.0]`, `[1.7.0]` |
| `axil-auren` (`external/axil-auren`) | `v1.0.0` | `v1.5.3` | `[1.5.3]`, `[1.5.2]`, `[1.5.1]`, `[1.5.0]`, `[1.4.0]`, `[1.3.1]`, `[1.3.0]`, `[1.2.0]`, `[1.1.0]` |
| `axil-auth` (`external/axil-auth`) | `v1.2.0` | `v1.3.4` | `[1.3.4]`, `[1.3.3]`, `[1.3.2]`, `[1.3.0]` |
| `axil-hyle` (`external/axil-hyle`) | `v1.0.1` | `v1.1.4` | `[1.1.4]`, `[1.1.3]`, `[1.1.2]`, `[1.1.0]` |
| `axil-tty` (`external/axil-tty`) | `v1.4.0` | `v1.6.1` | `[1.6.1]`, `[1.6.0]`, `[1.5.0]` |
| `axil-nd` (`external/axil-nd`) | `v1.2.0` | `v1.3.0` | `[1.3.0]` |
| `libareole` (`external/libareole`) | — | `v1.1.0` | `[1.1.0]`, `[1.0.0]` |
| `libchaff` (`external/libchaff`) | — | `v1.1.0` | `[1.1.0]`, `[1.0.0]` |
| `libpitch` (`external/libpitch`) | — | `v1.0.1` | `[1.0.1]`, `[1.0.0]` |
| `libhyle-react` (`external/libhyle-react`) | — | `v1.0.0` | `[1.0.0]` |
| `pi-mm` (`.pi/extensions/pi-mm`) | `v0.1.0` | `v0.8.0` | `[0.8.0]` .. `[0.2.0]` (15 sections) |
| `pi-quest` (`.pi/extensions/pi-quest`) | `v1.1.0` | `v1.12.0` | `[1.12.0]` .. `[1.2.0]` (17 sections) |

---

## 2026-11-17 / 2026-11-18

### First Full 7-OS & 6-Prebuild Release of `@tty-pt/mm` (`external/libmm` `v1.0.0`)

| Package (`Repository`) | Published Version & Time (UTC) | OS Package Formats (`publish_to`) | Changes & Release Notes |
|---|---|---|---|
| `@tty-pt/mm` (`external/libmm`) | `1.0.0` (GitHub Release & 7 OS repos at 17:23 on 2026-11-17; npm `@tty-pt/mm@1.0.0` at 09:20 on 2026-11-18) | `deb, apk, rpm, pacman, brew, openbsd, winget` | Initial public release of the pure-C Two-Tier Multi-Axis Memory Engine, Codebase Symbol/Call-Graph Indexer, Universal `mm` CLI (`recall`, `record`, `observe`, `verify`, `ingest`, `index`, `search`, `query`, `trace`, `snippet`, `arch`, `adr`, `status`, `mcp`), and N-API bindings (`npm/native/mm_napi.c`) with all 6 platform prebuilds (`linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`, `win32-x64`), `getBinaryPath()`, `runCli()`, and `spawnMcpServer()`. Scoped runtime axis plugins (`libjoint,liblemma,libsepal,libislet`) to `deps_deb` to avoid OpenBSD chroot `pkg_add` conflicts while linking only against `libstele,libcorm`, and omitted the generic `"build"` npm script so `tty-pt/ci`'s `npm.yml` `pack` step preserves downloaded multi-arch `prebuilds/*/mm.node` binaries without requiring `libmm.so` on the bare pack runner. |

---

## Ecosystem Platform & Distribution Holes (Updated `2026-11-18`)

### Resolved in `tty-pt/ci` (`11b426e` — `2026-11-16T23:38:55Z` & `093ad4e` — `2026-11-17T17:17:04Z`)
- **RESOLVED — `linux-arm64` and `linux-x64-musl` (`apk`) Node-API Prebuilds**:
  - `tty-pt/ci/.github/workflows/all-in-one.yml` now includes a dedicated `node-arm64` job (`ubuntu-24.04-arm`) and `tty-pt/ci/package/action.yml` builds the Node-API addon on `apk` (`linux-x64-musl`).
  - `npm-detect` now requires all 6 platform prebuilds (`linux-x64,linux-arm64,linux-x64-musl,darwin-arm64,darwin-x64,win32-x64`) when `rpm`, `apk`, `brew`, and `winget` are enabled, and `npm.yml` verifies native loading on `ubuntu-24.04-arm` (`linux-arm64`) as well.
  - `@tty-pt/mm` `1.0.0` is the first package published with all 6 Node-API prebuilds (`linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`, `win32-x64`) verified and bundled in its npm tarball.
- **RESOLVED — Arch Linux (`pacman -Syu` & HTTP Repo Fix `093ad4e`) & Windows `.msix` DLL Bundling**:
  - `tty-pt/ci/package/action.yml` now runs `pacman -Syu --noconfirm --overwrite "*"` inside `ghcr.io/tty-pt/arch-builder:latest` (eliminating `GLIBC_2.44` / `libssl.so.3` partial-upgrade mismatches), uses `http://${{ inputs.host }}/${{ env.REPO }}` for the custom `pacman` repo (`093ad4e`, avoiding OpenSSL post-quantum TLS ClientHello `tlsv1 alert decode error` after `pacman -Syu`), removes the unused `pacman_mingw` job, and bundles dependent MinGW DLLs into `winget` `.msix` packages.

### Remaining Structural Holes
1. **`axil-auth` Missing Windows (`winget` / `win32-x64`) & `axil-tty` Missing Colocated Xylem Prebuild**:
   - **`axil-auth` on Windows**: `axil-auth` (`src/axil-authd.c`) uses POSIX `sys/un.h`, `fork`, `pwd.h`, and `grp.h`, so `npx @tty-pt/axil -m auth` remains POSIX-only (`linux-x64`, `linux-arm64`, `linux-x64-musl`, `darwin-arm64`, `darwin-x64`).
   - **`axil-tty` Xylem `.so`/`.dylib` prebuild**: `@tty-pt/axil-tty` ships its `package.json` at the repo root (`dir: .`) with client-side JS (`axil-tty.js` + `xterm.js`), but does not have `npm/native/axil_tty_napi.c` or `getModulePath()`.
2. **In-Tree Libraries Without Standalone Repositories or CI Pipelines (`libcorolla`, `nd-*`)**:
   - `external/libcorolla` (`0.1.0`) and 10 newer NeverDark directories (`nd-bus`, `nd-mcp`, `nd-obj`, `nd-region`, `nd-session`, `nd-seven`, `nd-speech`, `nd-view`, `nd-world`, `axil-nd-testprobe`, plus legacy dirs `libchunk`, `libnoise`, `libstoma`, `libtransp`) are in-tree directories with no `.gitmodules` entry, no `npm/` package, and no `.github/workflows/ci.yml`.
   - The 18 existing NeverDark submodule repos (`axil-nd` + 17 `nd-*` modules) have unreleased commits on `main`, use explicit secret lists instead of `secrets: inherit`, omit `pacman` and `winget` from `publish_to`, and have no `npm/` packaging (`getModulePath()`).

---

## Remaining Unreleased Repositories & Post-Tag Commits (`2026-11-18` Audit)

### 1. Released Libraries with Functional Commits Ahead of Latest Tag
- **`@tty-pt/stele`** (`external/libstele`, tag `v1.7.1` +3 commits at `4cbeb1d` $\rightarrow$ next target **`v1.8.0`**):
  - `4cbeb1d`: `feat(wq): add isomorphic work queue and skip redundant main-thread worker caps` (`<ttypt/wq.h>`, `src/stele_wq.c`, `stl_wq_*` / `stele_wq_*` APIs, `STL_THREAD_LOCAL`, and worker capability deduplication in `stele-client.js`).
- **`@tty-pt/areole`** (`external/libareole`, tag `v1.1.0` +5 commits at `38b07f7` $\rightarrow$ next target **`v1.2.0`**):
  - `a471a93`: `feat(areole): add row_sampler scanline callback and max_populates_per_frame cap`
  - `b561c4d`: `fix(areole): pass exact span_w coordinates to row_sampler for watertight cross-LOD seam welding`
  - `01f2bf8`: `feat(areole): add ar_cull_3d_fn callback for manifold normal-cone and horizon culling`
  - `38b07f7`: `feat(areole): support async work-queue chunk population` (`struct stl_wq *wq`, `AR_NODE_FLAG_DEFERRED`)
- **`@tty-pt/lamina`** (`external/liblamina`, tag `v1.1.1` +6 commits at `ce8a3f5` $\rightarrow$ next target **`v1.2.0`**):
  - `5cc6c73`: `feat(lamina): add glDrawArraysInstanced and glVertexAttribDivisor for GPU hardware instancing`
  - `6a91d5f`: `feat(lamina): add 3D texture array, glDrawElementsInstanced, and KHR_parallel_shader_compile support`
  - `0046661`: `feat(lamina): sync instancing and 3D texture array bindings to webgl-cap.js and npm header`
  - `84087d4`: `fix(webgl): guard pixel unpack alignment on texImage3D and texSubImage3D`
  - `ce8a3f5`: `fix(webgl): guard offscreen FBO glViewport against resizing canvas swapchain`
- **`@tty-pt/chaff`** (`external/libchaff`, tag `v1.1.0` +3 commits at `9db6f12` $\rightarrow$ next target **`v1.1.1`**):
  - `d910dad`: `perf(noise): add 4-corner cell caching to periodic and rotated FBM noise`
  - `9db6f12`: `fix(chaff): use thread-local storage for noise cell caches` (`CHAFF_TLS` / `_Thread_local`)
- **`@tty-pt/bud`** (`external/libbud`, tag `v1.3.2` +2 commits at `5a5710c` $\rightarrow$ next target **`v1.3.3`**):
  - `5a5710c`: `fix(hydrate): sync DOM value property when patching value attribute on inactive inputs`
- **`@tty-pt/pi-mm`** (`.pi/extensions/pi-mm`, tag `v0.8.0` +4 commits at `00b9901` $\rightarrow$ next target **`v0.9.0`** now that `@tty-pt/mm` `1.0.0` is published):
  - `3aa956e`: `fix(pi-mm): natural remember capture, anchor preservation, and directive recall`
  - `00b9901`: `feat(mm): integrate @tty-pt/mm native N-API addon and update docs for v0.9.0` (`src/mm_native.ts`, `tests/mm_native.test.ts`, in-process RRF/ranking/window/payload/scanner/intent acceleration, and lock/classifier hardening).
- **Metadata-only commits (`docs(npm)` + `docs(changelog)`) since latest tag**:
  - `libcorm`, `libjoint`, `libislet`, `liblemma`, `libsepal`, `libhyle`, `libxylem`, `libhyle-source`, `libhyle-bud`, `libhyle-react`, `libpitch`, `axil`, `axil-auren`, `axil-auth`, `axil-hyle`, `axil-tty` all have 2–3 documentation/changelog commits ahead of their latest tag with no C/JS code changes.

### 2. Pi Extensions, Build System & NeverDark Modules
- **`@tty-pt/pi-quest`** (`.pi/extensions/pi-quest`, published `1.12.0` +3 commits at `c563465` $\rightarrow$ next target **`1.12.1`**):
  - `80231db` (background post-quest reversion/correction checks), `e8dff1a` (pass `-d` for detached `axil`, pad download icon, sync late `onUpdate`), `c563465` (`CHANGELOG.md` consolidation).
- **`@tty-pt/mk`** (`external/mk`, published `0.1.18` / tag `v0.2.0` +4 commits at `c6c28ec` $\rightarrow$ staged at **`0.4.0`**):
  - `44c2461` (centralized WASM toolchain, `wasm_alloc_shim.c`, `<ttypt/stele.h>`, `<ttypt/stele_wasm.h>`, `wasm-audit.sh`, local worktree prefix priority), `bc43bc7` (`fix(iso): probe clang-18 for WASI_CC when available`), and `c6c28ec` (`feat(wasm): allow stele_wq exports in wasm-allowed-exports.lst`).
- **NeverDark C repositories with unreleased commits on `main`**:
  - **`axil-nd`** (`external/axil-nd`, tag `v1.3.0` +2 commits at `b4fc8bb` $\rightarrow$ target **`v1.4.0`**): BCP macro view and sparsity packets (`include/nd/bcp-meta.h`), water map flag, and fallback UI scripts.
  - **`nd-plant`** (`external/nd-plant`, +8 commits at `e92c3c8` $\rightarrow$ target **`v1.0.4`**): 16 botanical species, §7.3 canopy competition, and biologically height-classified (`small`/`medium`/`big`) world-map sprites (`1.png`) and normal maps (`1_n.png`) across desert, swamp, tundra, mediterranean, alpine meadow, and shrubland specimens (`2703491..e92c3c8`).
  - **`nd-stone`** (`+2` commits $\rightarrow$ **`v1.0.3`**), **`nd-biome`** (`+2` commits $\rightarrow$ **`v1.0.3`**), **`nd-core`** (`+2` commits $\rightarrow$ **`v1.0.4`**), and **15 other `external/nd-*` modules** (`+2` commits each).
  - **`pi-instruct`** (`external/pi-instruct`, tag `v1.0.0` +1 commit `f59d8e0`).

### Open work

- Tag and release functional updates for `libstele` (`v1.8.0`), `libareole` (`v1.2.0`), `liblamina` (`v1.2.0`), `libchaff` (`v1.1.1`), `libbud` (`v1.3.3`), `pi-mm` (`v0.9.0`), `pi-quest` (`v1.12.1`), and `mk` (`v0.4.0`), plus integrate `@tty-pt/mm` into `dwm-jarvis` (`~/.local/src/dwm`).
- Stand up a repository and CI pipeline for `libcorolla` (`0.1.0`).
- Add `pacman` and `secrets: inherit` to `axil-nd` (`v1.4.0`) and the 17 `nd-*` modules when cutting the next NeverDark wave.
- `libjoint`: per-timeline state in the WebAssembly build.



