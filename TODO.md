# TODO — NeverDark encapsulation & data/behavior/presentation split

Status of the boundary and decoupling work across NeverDark. Rule being enforced:
**`external/axil-nd` knows nothing about any `nd-*` tenant module, and the
`mods/nd` host shell only talks to modules through generic hooks/registries.**

## Completed

### 1. Engine & Shell Encapsulation (1xKHVP & 1xKMrp)
- `nd_client_state_t` carries **generic named extension slots** (`nd_client_ext` / `nd_client_ext_get`, `bcp-types.h`).
- Opcodes, types, and unpackers live with their owning modules (`nd-spell/spell-bcp.h`, `nd-equip/equip-bcp.h`, `nd-mortal/mortal-bcp.h`, `nd-attr/attr-bcp.h`).
- Global singletons eliminated (`g_nd_equipment` removed; state is strictly per-session).
- Core `axil-nd` client is module-agnostic: attr/vitals types, `ND_ATTR_*`, `nd_stats_state_t`, `ND_BCP_HP/STATS/MP` removed from core and replaced by generic named gauges (`nd_client_gauge_get/set`, `nd_bcp_decode_gauge`) and extension slots.
- Generic UI glue registry (`ND_UI_GLUE`, `nd_ui_glue_*` in `ui-slot-types.h`, implemented in `nd_ui.c`) for contribute, render-slot, state-default, state-serialize, and scene-sync. Works identically in native SSR and in-WASM hydration.
- SSR fallback in `external/axil-nd/src/http.c` stripped of hardcoded gauges, inventory/look dock, and Citadel room.
- Relocated tenant UI glue files out of `mods/nd/ux/` into their respective module source trees (`external/nd-attr/ux/attr_ui.c`, `external/nd-equip/ux/equip_ui.c`, `external/nd-spell/ux/spell_ui.c`, `external/nd-fight/ux/fight_ui.c`, `external/nd-mortal/ux/mortal_ui.c`).
- Decoupled `mods/nd/nd.c` initial state seed: loaded from declarative `universe_seed.dsv` (`mods/nd/data/universe_seed.dsv`, copied to `var/nd/universe_seed.dsv`).
- Automated encapsulation gate: `scripts/check-nd-encapsulation.sh` enforces zero module concept leaks into `external/axil-nd` and zero module UI header includes in `mods/nd`.

### 2. Presentation & Viewport Parity
- **Equipment Slot Descriptors:** `nd_equip_slot_desc_t` and `nd_equip_slots[]` in `nd-equip/include/nd/equip-bcp.h` define presentation-neutral layout coordinates (`row`, `col`), socket keys, and unequip command letters. `equip-ui.h` dynamically constructs the paper doll grid using these descriptors.
- **Plain-Text TTY Formatter:** `nd_equip_format_text()` renders equipment as clean ASCII tables. `do_equip()` in `libnd-equip.c` executes this when typing `equip` in the TTY terminal.
- **TTY Grimoire Command:** `do_spells()` in `libnd-spell.c` renders an ASCII table of learned spells, MP costs, and elements for the terminal.
- **Dynamic Combat Target HUD:** `fight-ui.h` computes bar width and text percentage dynamically from `ND_CLIENT_TARGET_GAUGE` via `snprintf(w_buf, "width: %u%%;", pct)`.
- **Target Vitality Wire Stream:** `libnd-mortal.c` pushes target vitality to attacker over opcode 18 (`ND_BCP_TARGET_HP`), decoded into `target_ref` and `ND_CLIENT_TARGET_GAUGE`.
- **Scene Graph Extension & Viewport FX:**
  - `nd_scene_t` updated with skeletal socket attachment slots (`attach[ND_SCENE_MAX_ATTACH]`) on entities and spell burst FX slots (`fx[ND_SCENE_MAX_FX]`).
  - `equip_scene_sync()` maps worn items to named skeleton sockets (`head`, `rhand`, `neck`, etc.).
  - `spell_scene_sync()` maps spell cast events into elemental visual effects.
  - `nd_lamina.c` renders 2D attachment socket pips and expanding elemental particle rings for spell casts.
  - `nd_webgl.c` attaches 3D geometry to skeletal socket bones and emits 3D orbiting elemental particle bursts.
- **Cast Acknowledgement & Reactive Refresh:**
  - `mcp_cast_ack()` broadcasts opcode 17 (`ND_BCP_SPELL_CAST`) with slot, element, and target ref upon spell execution.
  - `mcp_spells()` refreshes spell list after casting.
  - Client grimoire dynamically disables unaffordable spells (`data-disabled="1"`, red MP text, unclickable).

### 3. Declarative Data Externalization (All 8 Content Schemas)
- Added `nd_table_load()` engine service in `external/axil-nd` (`law.h`, `law-types.h`, `law_data.c`) supporting pipe-delimited DSV files (`#` comments, blank lines, CRLF, ragged columns, error reporting).
- Shipped declarative tables in `external/nd-*/data/*.dsv` and `var/nd/*.dsv`:
  - `external/nd-spell/data/spells.dsv`: 11 spells (name, element, ms, ra, y, affect flags).
  - `external/nd-class/data/classes.dsv`: 4 character classes (name, life dice).
  - `external/nd-race/data/races.dsv`: 5 character races (name, stat bonuses, weapon word).
  - `external/nd-biome/data/biomes.dsv`: 19 planetary biomes (name, background color).
  - `external/nd-mob/data/mobs.dsv`: 14 mob species with sprite variance (`max_art`), element, biomes, and level/flags.
  - `external/nd-plant/data/plants.dsv`: 10 plant species with art variants, color, flags, temperature/rain ranges, and drops.
  - `external/nd-stone/data/stones.dsv`: mineral types and max art variants.
  - `external/nd-wts/data/wts.dsv`: 12 combat vocabulary words.
- Configurable currency symbol in `nd-shop` via `AXIL_ND_CURRENCY` (defaulting to `"P"`).
- Shipped modules load from DSV on install with compiled fallbacks.
- Unit test `tests/unit/nd_table_test.c` verifies parsing, whitespace trimming, comments, error aborts, and integrity of all 8 shipped DSV tables.

### 4. Verification & Hardening
- `tests/unit/nd_ext_state_test.c`: Hardened against truncated packets, empty frames, oversized slot counts, null pointers, corrupted wire frames, out-of-bounds slot IDs, and asserts multi-session isolation.
- `tests/unit/test_scene_sync.c`: Asserts attachment mapping, elemental FX recording, target vitality binding, and buffer bounds.
- `tests/unit/test_ssr_hydration.c`: Asserts production serialize/deserialize, paper doll descriptor count, dynamic fight HUD width (30%), grimoire generation, and multi-viewport scene attachments.
- `tests/e2e/nd-browser-post-seed.test.ts`: Automated browser e2e test asserting full reactive round-trip:
  - Grimoire rendering from pushed BCP spell list.
  - Disabled unaffordable spell rows with no cast commands.
  - Clicking affordable spell row dispatches `c0`.
  - Server answers with MP deduction (`50/50 -> 47/50 -> 10/50`).
  - Target vitality HUD bar rendering dynamic 30% width and clearing on dismiss.
  - Viewport hardware switching cycling cleanly between TTY (`#term`), 2D Lamina (`canvas#viewport`), 3D WebGL (`canvas#viewport-gl`), and back to TTY with consistent state.

---

### 6. Commit Consolidation & Release Wave 2026.11
- Consolidated commits since `old-main` across the main repository and all 34 submodules into exactly 1 atomic release commit per repository.
- Synchronized README.md and CHANGELOG.md across all 10 core infrastructure libraries (`axil`, `axil-auth`, `axil-tty`, `libbud`, `libcorm`, `libhyle`, `libhyle-source`, `libjoint`, `libsepal`, `libxylem`).
- Verified all 37 `CHANGELOG.md` files comply with the `tty-pt/ci` bracketed awk extraction contract.
- Executed comprehensive diff review against old-main and validated zero regressions across unit, boundary, and full-universe E2E test suites.
- Packaged <ttypt/iso.h> into external/libqsys for ecosystem-wide packaging CI, providing universal ISO_EXPORT macros across native and WASM targets.

## Remaining Backlog for Future Exploration

- Formula and balance scaling parameters (`nd-level` XP curves, `nd-fight` combat factors, `nd-mortal` hunger/thirst thresholds).
- Noise octaves and cosmological parameters in `nd-world`.





