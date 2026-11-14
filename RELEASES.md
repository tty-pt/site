# Releases

A running record of release waves: what went out, how, what broke, and what is left.
Times are UTC.

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

Notes:
- `@tty-pt/joint` 1.7.1 and `@tty-pt/hyle` 1.7.1 were tagged but never reached npm (1.7.0 went to
  1.7.2).
- Tags moved and re-pushed so CI would rebuild with a fixed workflow: `libjoint` v1.7.3, `liblemma`
  v1.3.2, `libhyle-source` v1.4.0 (twice), `liblamina` v1.1.0. Each republished every package format
  over the previous one.
- `@tty-pt/hyle-bud` 1.2.0 was published by CI at 00:12 UTC on 2026-11-14 (`+ @tty-pt/hyle-bud@1.2.0`; the tag run
  published every format and the verified tarball).
- `axil-auren` v1.5.0 was tagged but **failed in its `test` job and nothing was published**: see
  "Problems met" (it needs a new `axil` release first).
  - Its npm package had its own number (1.2.0, never published). It now carries the repository
    version (1.5.0), because the npm workflow requires the package version to equal the tag.

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
- The npm package is detected from `npm/package.json`. Nothing is declared in the caller.
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
  same fix was applied pre-emptively to `libhyle` and `libxylem`.
- **`npm publish pkg/x.tgz`** is read by npm as a GitHub `user/repo`. The path must start with `./`.
- **Registry lag.** New versions took 1 to 5 minutes to show in `npm view`; a brand-new package
  longer. Check the registry API (`curl https://registry.npmjs.org/@tty-pt%2f<name>`) before
  concluding a publish failed.
- **Intermittent `ssh` exit 255 in `publish` jobs** (`libbud` openbsd, `libislet` openbsd,
  `libjoint` winget). It happened when two releases published at the same time. Tags were then
  pushed several minutes apart, and failed jobs were re-run. The cause is not confirmed; see
  `PLAN-publish-traffic-and-locks.md` in `tty-pt/ci`.
- **Re-running a run does not pick up workflow changes.** It reuses the caller workflow from the
  tagged commit, so a fix to `ci.yml` needs a new tag (or a moved one).
- **`axil-auren` v1.5.0 cannot build against the released `axil`.** Its `test` job compiles against
  the `axil` package installed from the tty.pt repository, whose header
  `/usr/include/ttypt/axil.h` still includes `<ttypt/qsys.h>`. The repository's `axil` (1.8.0,
  unreleased; last tag v1.7.1) includes `<ttypt/stele.h>`. `axil` has to be released first. Its own
  `ci.yml` also needs `secrets: inherit` and `mingw-w64-x86_64-xxhash` in `deps_winget` before that.
- **`libjoint` WebAssembly state.** In WebAssembly mode every `JointTimeline` shared one
  interval buffer. The constructor and `close()` now clear it. A per-timeline handle in the C code
  would be the proper fix.

### Open work

- Release `axil` 1.8.0 (fix its `ci.yml` first: `secrets: inherit`, xxhash for winget), then
  move `axil-auren`'s `v1.5.0` tag to re-run it. `@tty-pt/auren` 1.5.0 and `@tty-pt/axil` 1.8.0 are
  not on npm yet.
- `libpitch`, `libchaff` and `libareole` have no `ci.yml`, so they have no pipeline.
- `axil`, `axil-auth`, `axil-hyle`, `axil-nd` and `axil-tty` still use an explicit secrets list
  without `NPM_TOKEN`. Switch them to `secrets: inherit` before releasing them.
- `libcorm`'s winget package does not carry or declare `libxxhash.dll`, so every Windows package that
  depends on it has to install xxhash itself.
- Native addons need `linux-arm64` and `linux-musl` prebuilds (the `@tty-pt/stele` loader has no
  `-musl` key yet).
- `libjoint`: per-timeline state in the WebAssembly build.
- The `pi-mm` and `pi-quest` minimum engine versions are not verified, and at least one is wrong.
  See `.pi/extensions/pi-mm/docs/NATIVE_ENGINES_TODO.md`.
- Not yet pushed or committed at the time of writing: `site` commit `3aafe71` (libhyle-react as a
  submodule), `duna`'s `package.json` and `package-lock.json`, and the `pi-mm` document.
