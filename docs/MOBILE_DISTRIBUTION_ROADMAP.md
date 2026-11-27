# Mobile & Cross-Platform Distribution Roadmap (Android, iOS, Desktop, `~/ci`)

Offline-first native distribution of the pure-C + isomorphic WASM stack (`axil`, `libxylem`, `hyle`, `corm`, `bud`, `d-axil`, `.business`, `pyxis`, `calyx`) across Android, iOS, Linux, macOS, and Windows, orchestrated by `~/ci`.

---

## Phase 1: Core Runtime & `libxylem` Mobile Readiness
1. **Static `libxylem` Module Registry (`XY_STATIC`):**
   - Add a compile-time static registration mode in `libxylem` so `xy_load()` resolves `mods/*` from an in-binary symbol table (`dlsym(RTLD_DEFAULT, ...)` or generated `xy_static_mods[]`) when `dlopen()` is unavailable/disallowed (iOS App Store).
   - Keep `XY_DECL` / `XY_IMPL` and `xy_install()` DAGs unchanged.
2. **In-Process `axil` Entry Point:**
   - Expose `int app_start(const char *data_dir, const char *asset_dir, int port)` (and `app_stop()`), running `axil` on a background `pthread` bound to `127.0.0.1:0` (ephemeral port) instead of `fork()`/daemonizing.
3. **Path & Storage Injection:**
   - Replace all hardcoded `var/`, `/tmp/`, and CWD assumptions in `corm`, `hyle`, `d-axil`, `.business`, `pyxis`, and `calyx` with injected `data_dir` and `asset_dir`.
   - Audit for desktop-only syscalls (X11/Wayland/TTY) and guard/stub behind a thin platform HAL.

## Phase 2: Cross-Compilation & Packaging (`external/*` + `pyxis` + `calyx`)
1. **Android NDK (`aarch64-linux-android`, `armv7a-linux-androideabi`, `x86_64-linux-android`):**
   - Cross-compile `external/*`, `mods/*`, `d-axil`, `.business`, `pyxis`, `calyx` into `libapp.so` (or `jniLibs/<abi>/*.so`).
   - Package as a reusable Android `.aar` with a minimal JNI bridge (`nativeStart(dataDir, assetDir)`).
2. **iOS / Apple (`arm64-apple-ios`, `arm64-apple-ios-simulator`):**
   - Cross-compile all C targets as static `.a` libraries using `xcrun --sdk iphoneos` / `iphonesimulator`.
   - Bundle headers + static libs into `AppCore.xcframework`.
3. **WASM & Static Assets:**
   - Bundle `htdocs/*.wasm`, `htdocs/*.js`, `htdocs/*.css`, and seed DBs into app assets (`assets/` on Android, bundle resources on iOS) extracted to `data_dir` on first launch.

## Phase 3: Thin Native Shells (Offline-First)
1. **Android Shell (Kotlin):**
   - `MainActivity` copies/verifies seed assets into `Context.getFilesDir()`, calls `nativeStart(...)`, and loads `http://127.0.0.1:<port>/` in a `WebView` (with file-picker/audio/notification hooks as needed).
2. **iOS Shell (Swift):**
   - `AppDelegate` / `ContentView` copies seed assets into `NSApplicationSupportDirectory`, calls `app_start(...)`, and loads `http://127.0.0.1:<port>/` in `WKWebView` (with `NSAppTransportSecurity` allowing `127.0.0.1` or a `WKURLSchemeHandler`).

## Phase 4: `~/ci` Automation & Release Pipeline
1. **Unified Build Matrix in `~/ci`:**
   - Linux (`x86_64`, `aarch64`), macOS (`arm64`, `x86_64`), Windows (MinGW/Clang), Android (`.aar` / `.apk` / `.aab`), iOS (`.xcframework` / `.ipa`), and WASM (`htdocs/*.wasm`).
2. **Automated Gates:**
   - Boundary checks (`check-module-boundaries.sh`, `check-wasm-imports.sh`, `check-no-site-specific-js.sh`).
   - Unit & integration suites (`make test-fast`, `make test-integration`).
3. **Signing & Distribution:**
   - Android keystore signing (`.aab` -> Play Store / direct `.apk`).
   - Apple Developer provisioning & notarization (`.ipa` -> TestFlight / App Store, macOS `.dmg`).
   - Artifact publishing (NPM packages per `docs/CI_NPM_DISTRIBUTION.md`, GitHub Releases, self-hosted).
