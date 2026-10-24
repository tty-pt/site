# Changelog

All notable changes to the site and NeverDark platform will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased] - 2026-10-24

### Added
- **`pi-quest` v1.2.0 Hardening**:
  - Autonomous Foreman Sub-Quest Dispatcher (`src/subquests/foreman.ts`) for sequential backlog execution.
  - Deterministic Zero-Token Semantic Linters (`src/validation/semantic-lint.ts`) checking UX compile-time branching, cross-module `#include "*.c"` barriers, git conflict markers, and forbidden engine symbols.
  - State & Disk Dual-Reconciliation on Stack Pop (`reconcileDocContent`, `reconcileParentDocOnReturn`) synchronizing parent markdown checkboxes and execution receipts.
  - Persistent verification test receipts on `QuestState` surviving transcript compaction and session reloads.
  - Unrestricted worker mode for emergent sub-quests, eliminating the zero-path lockout trap.

## [1.0.0]

- **Turnkey Full-Universe Deployment (`./start.sh`)**:
  - Unified single-port (`:8080`), single-process architecture serving SSR HTML, static WASM bundles, full-duplex WebSocket streams, and raw TCP Telnet CLI on one endpoint.
  - Automated universe seeder (`scripts/seed-nd-universe.sh`) generating 32-planet star sectors with pre-configured citadel hubs, starter equipment, and NPC merchants.
- **Pure-C Isomorphic Bud UI for NeverDark (`mods/nd/ux/nd_ui.c`, `htdocs/nd.wasm`)**:
  - Full-fidelity SSR-to-WASM state hydration through embedded `<script id="bud-state">` JSON.
  - Multi-viewport rendering engine supporting simultaneous 3D hardware-accelerated WebGL (`mods/nd/ux/nd_webgl.c`), 2D Lamina canvas (`mods/nd/ux/nd_lamina.c`), pure-C Bud DOM, and ANSI TTY terminal.
  - Interactive RPG HUD components: 2-column character stats sheet, HP/MP vitality bars, 4x5 paper doll equipment modal, and grimoire spellbook overlay.
- **In-WASM Xylem Hook Dispatcher (`libxylem-wasm.c`)**:
  - Pure-C in-WASM hook registry and listener chaining (`XY_LISTENER_TAG`, `xy_call`, `xy_last`) enabling cross-module event dispatching inside the browser without host OS `dlopen`.
- **Declarative BCP Protocol Engine (`<nd/bcp.h>`, `<nd/bcp-meta.h>`)**:
  - High-throughput binary framing (`#b` magic) and WebSocket channel multiplexing: Stream 0 (Text/TTY), Stream 1 (Binary BCP), Stream 2 (JSON/RPC).
- **Decoupled Architecture & Build Portability**:
  - Complete elimination of `DT_RUNPATH` / `RPATH` and in-tree `LD_LIBRARY_PATH` exports, linking cleanly against system authority (`/usr/lib`).
  - Decoupled 19 independent tenant domain modules on the XY bus, enforcing strict two-header separation (`<nd/*-types.h>` and `<nd/*.h>`).
  - Zero site-specific JavaScript: generic client bus (`axil-bridge.js`, `bud-client.js`, `bud-hydrate.js`) with all application logic residing in pure C and compiled WASM.
