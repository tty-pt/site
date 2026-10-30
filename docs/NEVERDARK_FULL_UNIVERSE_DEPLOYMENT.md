# NeverDark Full Universe Deployment: Single-Port Unified axil, Spacetime Seeder, and Automated E2E Test Suite

## 1. Unified Single-Port Service Architecture

In NeverDark, **all web and game services run on ONE single port in ONE single process**.
There is no separate game daemon and web server.

When `axil` starts:
```bash
./start.sh
# or standalone:
axil -d -A -p 8080 -m "./lib/axil-nd"
```
(`libaxil-nd` automatically loads its engine dependencies — `libnd-region`, `libnd-bus`, `libnd-obj`, `libnd-world`, `libnd-view`, `libnd-mcp`, `libnd-speech`, and `libnd-session` — via internal libxylem `xy_load()` during `xy_install()`, while tenant content modules load via database region modding.)
The exact same port `:8080` handles:
1. **HTTP SSR (`GET /nd`)**: Renders the complete dynamic pure-C Bud virtual DOM tree with embedded `<script id="bud-state">` JSON.
2. **Static Assets**: Serves `/nd.wasm`, `/bud-client.js`, `/styles.css`, and capability bridges directly via `axil_sendfile`.
3. **In-Place WebSocket Upgrade (`Upgrade: websocket` at `/nd`)**: Upgrades the HTTP socket in-place to full-duplex Binary Client Protocol (`#b`) and ANSI text streaming.
4. **Telnet / Raw CLI**: Plain TCP telnet clients connect and authenticate on the same port.

### How to Test the Turn-Key Universe with `-A`

To test the turn-key NeverDark universe directly with operator/anonymous access:
1. **Turnkey Server Launch**:
   ```bash
   ./start.sh
   # or manually with -A:
   axil -d -A -p 8080 -m mods/core/core
   ```
2. **Run Headless E2E Integration Suite**:
   ```bash
   bash tests/e2e/test_full_universe.sh
   ```
3. **Run Playwright Browser Suite**:
   ```bash
   make test-e2e FILE=nd-browser-post-seed.test.ts
   ```
Under the `-A` flag, `axil` auto-authenticates incoming connections as the operator (`pw->pw_name`), enabling immediate full-fidelity terminal and game interactions without manual login.

---

## 2. Spacetime Region & Morton Hierarchy Architecture

NeverDark's cosmos is partitioned into 4D spacetime regions identified by the pair `(id, plen)`, where `id` is a 64-bit Morton-encoded prefix and `plen` is the prefix length (0..64):

### Morton Key Prefix & Bitmask Arithmetic Table

| Prefix Width (`plen`) | High-Bit Mask | Spatial Scale / Scope | Hierarchy & Modder Governance |
|---|---|---|---|
| `plen = 0` | `0x0000000000000000` | Cosmos (Universe) | Root tier. Server administrator / engine core. Global laws & baseline modules. |
| `plen = 11` | `0xFFE0000000000000` | 32-Planet Super-Sector | Sovereign sector governor ($2^5 = 32$ worlds, covering worlds 0..31). Loads baseline gameplay suite once. |
| `plen = 16` | `0xFFFF000000000000` | Single Planet / World | Planetary ruler ($2^{16} = 65,536$ worlds total). Local planetary laws, biomes, and rules. |
| `plen = 24` | `0xFFFFFF0000000000` | Hemisphere / Continent | Continental governor. Regional mechanics, sub-factions. |
| `plen = 32` | `0xFFFFFFFF00000000` | District / Zone / Area | District magistrate / modder. Area-specific custom graphics (e.g. `liblamina` viewport). |
| `plen = 48` | `0xFFFFFFFFFFFF0000` | City / Settlement | Town mayor / settlement builder. Local vendors, town laws. |
| `plen = 64` | `0xFFFFFFFFFFFFFFFF` | Single Room / Cell | Room architect / individual player room. Custom room scripts and fixtures. |

### Hierarchical Modder Attribution
- **Outermost Rulers Cascade Down**: The most powerful modder governs the coarsest prefix (e.g., `plen = 11` for a 32-planet sector). When standard tenant modules (`nd-attr`, `nd-equip`, `nd-fight`, `nd-spell`, etc.) are loaded into this sector once, **all 32 nested worlds automatically inherit and execute them** via XY hierarchical dispatch (`nd_scope_dispatch`).
- **Inner Modders Code Freely**: Subordinate modders can claim sub-regions (e.g. `region 16 world 2` or `region 32 here`) to customize local mechanics, add custom items, or designate custom `liblamina` rendering.
- **Cascading Denies**: An ancestor region's `deny` rule strictly bounds all child regions beneath it.

### Module Topology: Tenant Submodules vs In-Tree Core Subsystems

The NeverDark module ecosystem is strictly divided into two architectural tiers:
1. **19 Decoupled Tenant Submodules (`.gitmodules`):**
   Independently versioned Git submodules under `external/nd-*` (`nd-attr`, `nd-biome`, `nd-class`, `nd-core`, `nd-drink`, `nd-equip`, `nd-fight`, `nd-level`, `nd-mob`, `nd-mortal`, `nd-other`, `nd-plant`, `nd-race`, `nd-seat`, `nd-shop`, `nd-spell`, `nd-stone`, `nd-vanilla`, `nd-wts`). Each contains its own `CHANGELOG.md`, two-header API (`<nd/*-types.h>` and `<nd/*.h>`), declarative DSV data tables or BCP unpackers/UI modals, and builds as an independent `.so`.
2. **8 In-Tree Engine Subsystems (Host-Tracked Directories):**
   Unversioned internal engine subsystems tracked directly in the site repository under `external/` (`nd-bus`, `nd-mcp`, `nd-obj`, `nd-region`, `nd-session`, `nd-speech`, `nd-view`, `nd-world`). These provide foundational spacetime coordinates, object skeletons, and session queues loaded automatically by `libaxil-nd.so`.

---

## 3. The Universe Seeder (`scripts/seed-nd-universe.sh`)

Automates initial universe generation:
```bash
bash scripts/seed-nd-universe.sh [target_db_path] [num_planets]
```
- Defaults to `var/nd/universe.db` with 1 planet ("Sector Prime").
- Passing `32` creates a complete 32-planet frontier star sector.
- **32-Planet Super-Region**: Claims `region 11 world 0` and loads all 18 tenant content modules into the sector **once**. All nested planets inherit them automatically.
- Pre-seeds Planet 1 fixtures:
  * Spawn Hall: "The Citadel Common Hall"
  * Equipment Fixtures: Steel Broadsword, Leather Tunic, Iron Helm
  * NPCs & Mobs: Shopkeeper Vaelin (`nd-shop`), Training Golem (`nd-fight`, `nd-mob`)
- Pre-seeds Planet 2 as the **Lamina Graphical Showcase Area** (`region 16 world 2`) with crystal sanctum, observatory, and sun prism fixtures.

---

## 4. Headless Automated E2E Multi-Module Test Suite (`tests/e2e/test_full_universe.sh`)

Verifies the full system from a clean ephemeral start to clean shutdown:
1. **Ephemeral Seed**: Creates an isolated `/tmp/nd-e2e-XXXXXX/universe.db` seeded with Planet 1.
2. **Single-Port Boot**: Boots `axil -d -A -p $PORT` with full module set.
3. **Phase A (HTTP SSR & Static WASM)**:
   - Asserts `GET /nd` returns HTTP 200 with `#bud-state` JSON.
   - Asserts `/nd.wasm` returns HTTP 200 with valid WASM magic bytes (`\0asm`).
4. **Phase B (WebSocket Upgrade & BCP Handshake)**:
   - Upgrades socket in-place to WebSocket (`HTTP 101 Switching Protocols`).
   - Receives initial `#b` binary frames (HP, MP, Room 1001).
5. **Phase C (Interactive Commands)**:
   - Tests `look`, `stats` (`nd-attr`), `help`.
   - Sends WebSocket close frame (opcode 0x88) and verifies graceful disconnection.
6. **Phase D (Teardown & Cleanup)**:
   - Sends SIGTERM to `axil`, verifies clean Corm database flush, removes temporary files, asserting zero leftover background processes.

---

---

## 5. Browser E2E Test Suites (`tests/e2e/`)

Automated end-to-end browser and WebSocket test suites ensure full-stack integrity:

1. **`tests/e2e/nd-ws-roundtrip.test.ts`**:
   - Upgrades `/nd` HTTP connection in-place to WebSocket with an active session cookie.
   - Asserts authenticated player resolution, BCP packet exchange, and command round-trip (`say pong`).
   - Asserts unauthenticated upgrades receive immediate `AUTH_FAILURE` rejection and closure.

2. **`tests/e2e/nd-browser-post-seed.test.ts`** (Playwright Chromium):
   - Authenticates test user via registration and login.
   - Navigates to `/nd` and validates `<script id="bud-state">` JSON integrity.
   - Asserts client-side Bud WASM hydration (`body[data-wasm-loaded]`) with zero DOM mismatches.
   - Verifies `#term[data-bud-terminal="1"]` mounts local vendored `@xterm/xterm` without external CDN dependencies.
   - **Interactive Terminal Command Execution**: Simulates real keyboard input into xterm (`look`, `say`, and module commands like `inventory` / `stats`), asserting response text in the terminal buffer.
   - **Viewport Switching**: Cycles viewports between Terminal and 2D Lamina tilemap (`canvas#viewport`), verifying non-blank rendered pixels.
   - Verifies directional keypad navigation dispatches commands over the WebSocket bus.

---

## 6. Multi-Instance Port Isolation

Running the NeverDark platform alongside other axil instances (such as Auren on `:4242`) is fully isolated:
- **Port-Scoped Cookies**: `libaxil-auth` scopes session cookies by port (`QSESSION_<port>`), avoiding cookie collision on `localhost`. Standard ports (80/443/8080) use `QSESSION` for reverse-proxy compatibility.
- **Namespaced WebSocket Bus**: In `htdocs/axil-ws-bridge.js`, client-side WebSocket buses are namespaced by origin host and port.

---

## 7. Turnkey Developer Commands (`Makefile`)

| Command | Action |
| :--- | :--- |
| **`./start.sh`** | Primary turnkey runner: auto-seeds `var/nd/universe.db` if missing, sets library paths, boots on `:8080`. |
| **`make test`** | Full test suite gate: boundaries, unit tests, pages, integration, and E2E suites. |
| **`make test-e2e`** | Runs all Playwright browser E2E test suites with transient server. |
| **`make test-universe`** | Executes the automated headless E2E test suite from clean startup to clean cleanup. |
| **`make seed-universe`** | Re-seeds `var/nd/universe.db` with Planet 1 (or `ND_PLANETS=32`). |
| **`make install-libs`** | Installs all core libraries and `external/nd-*` modules into `/usr/lib` and `/usr/include/nd/`. |
EOF