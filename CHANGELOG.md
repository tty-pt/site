# Changelog

All notable changes to the site and NeverDark platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-10-24

- **Turnkey Full-Universe Deployment (`./start.sh`, `scripts/seed-nd-universe.sh`, `tests/e2e/test_full_universe.sh`)**:
  - Unified single-port (`:8080`), single-process architecture serving SSR HTML, static WASM bundles, full-duplex WebSocket streams, and raw TCP Telnet CLI on one endpoint.
  - Automated universe seeder (`scripts/seed-nd-universe.sh`) generating 32-planet star sectors with pre-configured citadel hubs, starter equipment, and NPC merchants.
  - Headless full-universe integration test harness (`tests/e2e/test_full_universe.sh`) and browser post-seed E2E suite (`tests/e2e/nd-browser-post-seed.test.ts`).
- **Pure-C Isomorphic Bud UI for NeverDark (`mods/nd/ux/nd_ui.c`, `htdocs/nd.wasm`)**:
  - Full-fidelity SSR-to-WASM state hydration through embedded `<script id="bud-state">` JSON and instant WASM initialization.
  - Multi-viewport rendering engine supporting simultaneous 3D hardware-accelerated WebGL (`mods/nd/ux/nd_webgl.c`), 2D Lamina canvas (`mods/nd/ux/nd_lamina.c`), pure-C Bud DOM, and ANSI TTY terminal.
  - Interactive RPG HUD components: 2-column character stats sheet, HP/MP vitality bars, 4x5 paper doll equipment modal, and grimoire spellbook overlay.
  - Planet 2 (Aethelgard) showcase featuring perspective 3D room boxes, cutaway exit archways, stylized 3D avatar prisms, target halos, and dynamic Time-of-Day sun/sky lighting.
- **Web HAL 60fps Frame Loop Contract (`wasm_tick`)**:
  - Implemented `wasm_tick` continuous rendering contract across `libbud`, `htdocs/bud-client.js`, and `mods/nd/ux/nd_ui.c`.
  - Dynamic viewport invalidation and delta animation frames driving the 3D WebGL renderer and 2D canvas without DOM thrashing.
- **Universal `bud_slot` & `bud_state` Keyed Hydration**:
  - Standardized `BUD_SLOT_DEFINE` and tenant hook contributions (`BUD_SLOT_CONTRIBUTE`) across NeverDark modules (`nd-attr`, `nd-equip`, `nd-fight`, `nd-mortal`, `nd-spell`), song detail (`mods/song/ux/detail.c`), and site shell (`mods/site_chrome/ux/chrome.c`).
  - Clean pre-link boundary verification and strict module encapsulation.
- **In-WASM Xylem Hook Dispatcher (`libxylem-wasm.c`)**:
  - Freestanding pure-C in-WASM hook registry and listener chaining (`XY_LISTENER_TAG`, `xy_call`, `xy_last`) enabling cross-module event dispatching inside the browser without host OS `dlopen`.
- **Declarative Zero-Arithmetic BCP Protocol & Axil WebSocket Multiplexing**:
  - High-throughput binary framing (`#b` magic) and WebSocket channel multiplexing: Stream 0 (Text/TTY), Stream 1 (Binary BCP), Stream 2 (JSON/RPC).
  - Zero-arithmetic packed memory-mapped structs with X-macro schema descriptors in `nd/bcp-meta.h`.
- **Build Portability, Linker Purity & DT_RUNPATH Elimination**:
  - Complete elimination of `DT_RUNPATH` / `RPATH` and in-tree `LD_LIBRARY_PATH` exports, linking cleanly against system authority (`/usr/lib`).
  - Decoupled 19 independent tenant domain modules on the XY bus, enforcing strict two-header separation (`<nd/*-types.h>` and `<nd/*.h>`) and eliminating all `*_IMPL` guards.
  - Zero site-specific JavaScript: generic client bus (`htdocs/axil-bridge.js`, `htdocs/bud-client.js`, `htdocs/bud-hydrate.js`) with all application logic residing in pure C and compiled WASM.
- **Unified Reusable Isomorphic Engine (`external/mk/iso.mk`, `<ttypt/iso.h>`)**:
  - Complete elimination of `-Wl,--export-all` across all WebAssembly binaries via reusable `external/mk/iso.mk`.
  - Standardized universal `ISO_EXPORT`, `ISO_EXPORT_AS`, and `ISO_IMPORT` macros in `<ttypt/iso.h>`, eliminating all `#ifdef __wasm__` branching from feature UX code.
  - Enabled `-fvisibility=hidden` and `-Wl,--gc-sections` dead-code stripping, shrinking client WASM binaries by 30-40% and bounding export tables to 17-23 intentional symbols.
  - Added integrated export allowlist audit (`external/mk/scripts/wasm-audit.sh`) wired into `make boundary-check`.
- **Web HAL Specification & Comprehensive Release Wave Documentation**:
  - Published master Web Hardware Abstraction Layer specification (`docs/WEB_HAL_SPECIFICATION.md`) and implementation roadmap (`docs/WEB_HAL_IMPLEMENTATION_PLAN.md`).
  - Standardized documentation, READMEs, and architectural reviews across the 39-target ecosystem (`docs/RELEASES_PLAN.md`).
