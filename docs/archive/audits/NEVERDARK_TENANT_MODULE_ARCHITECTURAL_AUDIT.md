# NeverDark Tenant Module Architectural Audit & Compliance Review

**Status:** Completed & Verified  
**Scope:** All 27 NeverDark tenant modules (`external/nd-*`) and their integration with the pure-C isomorphic stack.  
**Related Documents:** `docs/ARCHITECTURE.md`, `docs/CONVENTIONS.md`, `docs/NEXT_GEN_WASM_ARCHITECTURAL_PLAN.md`, `docs/AXIL_ND_MCP_BCP_IMPROVEMENT_SPEC.md`.

---

## 1. Executive Summary

This audit evaluates the architectural structure, convention compliance, and presentation decoupling of all 27 NeverDark tenant modules (`external/nd-*`) against the pure-C isomorphic architecture.

**Key Findings:**
1. **Compilation Health:** 100% (27 of 27 modules compile and link cleanly).
2. **Two-Header Rule & `*_IMPL` Guards:** Strict compliance verified (`scripts/check-no-impl-guards.sh` passed with 0 violations).
3. **Xylem Hook Topologies:** Modules cleanly implement 150+ `XY_IMPL` hooks across the canonical 20-event game bus (`external/nd-bus`).
4. **Data Invariants:** All modules operate on `corm` / `nd_get` / `nd_put` with zero direct `var/` access.
5. **WASM & Presentation Decoupling:** The newly established in-WASM Xylem reactor (`libxylem-wasm.c` and `XY_LISTENER_TAG`) successfully decouples `nd-attr` and `nd-equip` into autonomous UI extensions, establishing a proven pattern ready for `nd-spell`, `nd-shop`, and `nd-fight`.

---

## 2. Module Inventory & Functional Tiering

The 27 tenant modules are categorized into four functional tiers:

### Tier 1: Kernel Foundation (Core Primitives)
| Module | Role | Headers | Hooks (Decls / Impls) |
| :--- | :--- | :--- | :--- |
| **`nd-world`** | 4D spacetime, coordinates, maps, teleportation | `nd/world-types.h`, `nd/world.h` | 46 / 11 |
| **`nd-region`** | Governance, region hierarchies, permissions | `nd/region-types.h`, `nd/region.h`, `nd/regions.h` | 12 / 11 |
| **`nd-bus`** | Canonical 20-event reference game bus | `nd/bus-types.h`, `nd/world-events.h`, `nd/hooks.h` | 41 / 0 |
| **`nd-session`**| Axil connection and session lifecycle | `nd/session-types.h`, `nd/session.h` | 2 / 4 |
| **`nd-obj`** | Base object, item, and entity management | `nd/obj-types.h`, `nd/obj.h` | 26 / 20 |
| **`nd-core`** | Core icon decoration and base definitions | `nd/core-types.h`, `nd/core.h` | 1 / 2 |

### Tier 2: Presentation & Transport Layer
| Module | Role | Headers | Hooks (Decls / Impls) |
| :--- | :--- | :--- | :--- |
| **`nd-view`** | Viewports, action registration, VTF flags | `nd/view-types.h`, `nd/view.h` | 1 / 3 |
| **`nd-mcp`** | Binary Client Protocol serialization (`#b`) | `nd/mcp-types.h`, `nd/mcp.h` | 13 / 10 |
| **`mods/nd/ux`**| Isomorphic Bud SSR + in-WASM reactor | `nd/ui-slot.h`, `nd/bcp.h`, `nd_ui.h`, `nd_lamina.h` | 4 / 6 |

### Tier 3: Core RPG Systems
| Module | Role | Headers | Hooks (Decls / Impls) |
| :--- | :--- | :--- | :--- |
| **`nd-attr`** | 6 core attributes, training, modifier chains | `nd/attr-types.h`, `nd/attr.h`, `nd/attr-ui.h` | 9 / 11 |
| **`nd-fight`** | Combat resolution, attacks, weapon weight | `nd/fight-types.h`, `nd/fight.h` | 11 / 14 |
| **`nd-equip`** | Wearable item slots, defense/dodge calculations | `nd/equip-types.h`, `nd/equip.h`, `nd/equip-bcp.h`, `nd/equip-ui.h` | 2 / 6 |
| **`nd-level`** | Experience points and level scaling | `nd/level.h` | 2 / 4 |
| **`nd-mortal`**| Vitality, mortality lifecycle, death/murder | `nd/mortal.h` | 9 / 9 |
| **`nd-race`** | Playable races and racial bonuses | `nd/race.h` | 3 / 7 |
| **`nd-class`** | Character classes and stat scaling | *(leaf module — no public headers)* | 0 / 3 |

### Tier 4: World & Content Mechanics
| Module | Role | Headers | Hooks (Decls / Impls) |
| :--- | :--- | :--- | :--- |
| **`nd-spell`** | Spells, magic effects, and healing | *(leaf module — no public headers)* | 0 / 9 |
| **`nd-shop`** | NPC merchants, buy/sell commands | *(leaf module — no public headers)* | 0 / 0 |
| **`nd-drink`** | Consumable potions, food, and water | `nd/drink-types.h`, `nd/drink.h` | 1 / 2 |
| **`nd-plant`** | Flora, farming, and harvestable nodes | `nd/plant.h` | 0 / 5 |
| **`nd-stone`** | Mineral nodes and mining | *(leaf module — no public headers)* | 0 / 2 |
| **`nd-seat`** | Seating and furniture | `nd/seat-types.h`, `nd/seat.h` | 1 / 5 |
| **`nd-speech`**| Name matching, look commands, and speech | `nd/speech-types.h`, `nd/speech.h` | 11 / 8 |
| **`nd-biome`** | Environmental biomes | *(leaf module — no public headers)* | 0 / 0 |
| **`nd-mob`** | Mobile NPC / monster spawns | *(leaf module — no public headers)* | 0 / 1 |
| **`nd-other`** | Miscellaneous world objects | *(leaf module — no public headers)* | 0 / 1 |
| **`nd-vanilla`**| Default player location bootstrap | *(leaf module — no public headers)* | 0 / 1 |
| **`nd-wts`** | Words-to-speech / text inflection | *(leaf module — no public headers)* | 0 / 0 |

---

## 3. Compliance Evaluation Against Core Invariants

### A. The Two-Header Rule (`M-types.h` vs `M.h`)
- **Status:** **PASS** (100% compliant).
- **Audit Verification:** `scripts/check-no-impl-guards.sh` verifies zero `#ifndef *_IMPL` guards across all module headers.
- **Header Structure:**
  - Shared types, enums, and constants live in `nd/*-types.h` with zero `XY_DECL`s.
  - Caller-facing headers `nd/*.h` declare hooks unconditionally with `XY_DECL`s.
  - Implementers include only `*-types.h`, preventing redefinition and symbol collisions.

### B. Hook Topology & Multi-Listener Chaining
- **Hook Chaining:** Core game mechanics use canonical `xy_last()` chaining:
  - `effect`: Chained across `nd-attr`, `nd-equip`, and `nd-spell` to calculate cumulative bonuses.
  - `fighter_wt`: Chained across `nd-fight`, `nd-equip`, and `nd-race` to determine attack speed and encumbrance.
  - `hp_max`: Chained across `nd-attr` and `nd-class` for character vitality scaling.
- **Event Bus Compliance:** Modules connect directly to the 20 canonical game events in `nd-bus` (`on_add`, `on_status`, `on_examine`, `on_enter`, `on_leave`, `on_will_attack`, `on_mortal_life`).

### C. In-WASM Xylem Reactor Parity
- **Status:** **PASS**.
- With `libxylem-wasm.c` and `XY_LISTENER_TAG` in `external/libxylem`, client modules in WASM use the exact same hook dispatch semantics as native server `.so` modules.
- `scripts/check-wasm-imports.sh` confirms that WASM bundles contain zero undefined native symbol leaks.

---

## 4. Identified Gaps & Technical Debt

### Gap 1: Legacy `include/uapi/` Directory Remnants
- **Observation:** In `nd-obj`, `nd-region`, `nd-speech`, `nd-view`, and `nd-world`, legacy `include/uapi/` subdirectories (`uapi/object.h`, `uapi/entity.h`, `uapi/skel.h`, `uapi/azoth.h`, `uapi/st.h`, `uapi/map.h`, `uapi/match.h`, `uapi/type.h`, `uapi/mcp.h`) persist from the pre-modularization codebase.
- **Impact:** While modern code includes `<nd/obj-types.h>` and `<nd/region.h>`, some internal implementation files still `#include "uapi/*.h"`, creating redundant header paths.
- **Recommendation:** Deprecate `uapi/` headers by folding their struct definitions into the corresponding `nd/*-types.h` headers.

### Gap 2: Single-Header Modules Lacking `*-types.h`
- **Observation:** Four modules (`nd-level`, `nd-mortal`, `nd-plant`, `nd-race`) maintain only a single header file:
  - `nd-level.h`: contains only `XY_DECL`s.
  - `nd-mortal.h`: contains only `XY_DECL`s.
  - `nd-race.h`: contains only `XY_DECL`s.
  - `nd-plant.h`: contains only types (`plant_tile_t`).
- **Impact:** While fully compliant with `check-no-impl-guards.sh` today because they don't mix types with declarations, splitting them into `*-types.h` and `*.h` pairs will future-proof them against co-implementor collisions as game mechanics expand.

---

## 5. Modernization Roadmap: Extending UI Slots to Content Modules

The successful decoupled UI extension architecture demonstrated in `nd-attr` (`attr-ui.h`, `nd_attr_ui.c`) and `nd-equip` (`equip-ui.h`, `nd_equip_ui.c`) establishes a plug-and-play pattern for the remaining tenant modules:

```
  +-----------------------------------------------------------------------------+
  |                   NeverDark Modular Presentation Layer                      |
  |                                                                             |
  |   [Action Dock #g-actions]                     [Modals Container]           |
  |   • 📋 Character Sheet (nd-attr)  ──click──►   • Character Sheet Modal      |
  |   • 👕 Equipment (nd-equip)       ──click──►   • Equipment Doll Modal       |
  |   • 📜 Spells (nd-spell) [READY]  ──click──►   • Spellbook Modal [READY]    |
  |   • 💰 Shop (nd-shop)   [READY]   ──click──►   • Merchant Modal [READY]     |
  |                                                                             |
  |   [HUD Overlays Slot]                                                       |
  |   • Target HUD & Combat Log (nd-fight) [READY]                              |
  +-----------------------------------------------------------------------------+
```

### A. `nd-spell` UI Extension (`spell-ui.h`)
- Register action button: `{"spell", "📜", "Spellbook", 30}` into `ND_UI_SLOT_ACTIONS`.
- Implement `XY_LISTENER_TAG(bud_node *, on_nd_ui_render_modal, spell, ...)` to render a spellbook modal listing memorized spells and mana costs.
- Hook into `on_bcp_channel` for channel `bcp_mp` (opcode 13).

### B. `nd-shop` UI Extension (`shop-ui.h`)
- Server already calls `action_register("shop", "💰")`.
- Implement `XY_LISTENER_TAG(bud_node *, on_nd_ui_render_modal, shop, ...)` to render an interactive buy/sell grid with prices and item inventory when in a room with a shopkeeper.

### C. `nd-fight` Combat HUD Overlay (`fight-ui.h`)
- Implement `XY_LISTENER_TAG(bud_node *, on_nd_ui_render_slot, fight, ...)` for `ND_UI_SLOT_HUD_OVERLAYS`.
- Renders active target frame above the viewport displaying the enemy's name, vitality percentage, and combat stance.

---

## 6. Audit Verdict

| Category | Assessment | Notes |
| :--- | :--- | :--- |
| **Compilation Integrity** | **100% (27/27 Pass)** | All tenant modules build with zero errors. |
| **Two-Header Separation** | **100% (Pass)** | Zero `*_IMPL` guards; complete separation. |
| **Xylem Hook Topologies** | **100% (Pass)** | 150+ hooks cleanly dispatched; reference bus intact. |
| **Data Invariants** | **100% (Pass)** | Zero direct `var/` access; `corm`/`hyle` compliant. |
| **In-WASM Modularity** | **100% (Pass)** | Freestanding Xylem reactor active; zero native leaks. |
| **UI Presentation Decoupling** | **Proven & Ready** | `nd-attr` and `nd-equip` fully unified; roadmap established for remaining modules. |

The NeverDark module suite is in an exceptionally sound architectural state, fully aligned with the isomorphic pure-C stack.
