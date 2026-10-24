# Next-Generation NeverDark Architecture: Unified Single-Socket WebSocket, Declarative BCP Framework, Reactive Scene Synchronizer, and Full-Fidelity Hydration

## Executive Summary
This document establishes the architecture for the modernized NeverDark MUCK web and WASM runtime. It resolves the architectural debt identified across networking, binary wire protocols, viewport rendering, client hydration, and tenant module developer experience.

---

## 1. Single-Socket `axil` WebSocket Multiplexer

### 1.1 Architectural Rationale & Responsibility
Previously, `external/axil-tty` owned the client WebSocket instance and registered the `"axil"` capability in `window.__wasm_capability_registry`. This inappropriately coupled the networking layer to the terminal emulator.

Under the modernized design:
- `external/axil` owns `htdocs/axil-bridge.js` and `include/ttypt/axil-ws.h`.
- Exactly **one** persistent WebSocket is opened per browser session to `/nd`.
- The single socket multiplexes three logical streams:
  * **Channel 0 (`TEXT` / ANSI)**: Plain text terminal stream routed to terminal surface (`term_write` / `wasm_term_on_input`).
  * **Channel 1 (`BIN` / Magic `#b`)**: Binary Client Protocol frames routed to in-WASM BCP engine (`wasm_ws_on_channel(1, ptr, len)`).
  * **Channel 2 (`RPC` / Bud)**: Live DOM patch operations and interactive events.
- `external/axil-tty` is a pure UI component registering only `"tty"`, consuming the underlying stream via `window.__axil_ws`.

### 1.2 WASM C Interface (`include/ttypt/axil-ws.h`)
```c
/* WASM Host Imports */
__attribute__((import_module("axil"), import_name("ws_connect")))
void ws_connect(const char *url, size_t len);

__attribute__((import_module("axil"), import_name("ws_send")))
void ws_send(const void *buf, size_t len);

__attribute__((import_module("axil"), import_name("ws_send_channel")))
void ws_send_channel(uint8_t channel, const void *buf, size_t len);

__attribute__((import_module("axil"), import_name("ws_status")))
int ws_status(void);

__attribute__((import_module("axil"), import_name("ws_close")))
void ws_close(void);

/* WASM Client Exports */
void wasm_ws_on_open(void);
void wasm_ws_on_message(const uint8_t *buf, size_t len);
void wasm_ws_on_channel(uint8_t channel, const uint8_t *buf, size_t len);
void wasm_ws_on_close(int code);
```

---

## 2. Declarative Zero-Arithmetic BCP/MCP Framework (`nd/bcp-meta.h`)

### 2.1 Eliminating Pointer Arithmetic
Manual byte manipulation (`p += 4; rem -= 4;`) is eliminated. The protocol is structured across two complementary tiers:

### 2.2 Dual-Tier Architecture
1. **Tier 1 (Packed Structs for Fixed Wire Layouts)**:
   Fixed telemetry (HP, MP, TOD, Coordinates) uses memory-mapped packed structs:
   ```c
   #pragma pack(push, 1)
   typedef struct {
       uint8_t  magic[2]; /* '#', 'b' */
       uint8_t  opcode;
       uint16_t current;
       uint16_t maximum;
   } nd_bcp_gauge_wire_t;
   #pragma pack(pop)
   ```
   Decoding requires a single boundary check (`if (len < sizeof(nd_bcp_gauge_wire_t)) return -1;`) and direct pointer cast with zero copies.
2. **Tier 2 (Safe Cursor Reader/Writer for Compound Messages)**:
   Compound messages (Room, Entity, Equipment) use `nd_bcp_reader_t` and `nd_bcp_writer_t`:
   ```c
   int nd_bcp_unpack_room_msg(const uint8_t *buf, size_t len, nd_room_desc_t *out)
   {
       nd_bcp_reader_t r;
       nd_bcp_reader_init(&r, buf + 3, len - 3);
       out->ref = nd_bcp_read_u32_c(&r);
       nd_bcp_read_str_c(&r, out->name, sizeof(out->name));
       nd_bcp_read_str_c(&r, out->desc, sizeof(out->desc));
       out->exits = nd_bcp_reader_rem(&r) >= 4 ? nd_bcp_read_u32_c(&r) : 0;
       return nd_bcp_reader_ok(&r) ? 1 : -1;
   }
   ```
   All bounds checking and string terminations are handled internally.

---

## 3. Reactive 2D/3D Scene Graph & Viewport Synchronizer

### 3.1 The Canonical Scene Graph (`nd_scene.h`)
Network updates are decoupled from rendering pipelines via `nd_scene_t`:
```c
typedef struct {
    nd_scene_room_t room;
    nd_scene_entity_t entities[ND_MAX_ENTITIES];
    size_t entity_count;
    uint32_t player_ref;
    uint32_t target_ref;
    float time_of_day; /* 0.0 to 1.0 */
    struct { nd_vec3_t pos; float pitch, yaw, zoom; } camera;
} nd_scene_t;
```

### 3.2 Viewport Drivers
- **Lamina 2D (`nd_lamina_render_scene`)**: Renders tilemaps directly from `scene->room.tiles` with dynamic exits (N, S, E, W), draws each active entity with character glyph, color, health bar, and target highlight ring.
- **WebGL 3D (`on_frame_webgl`)**: Computes sky and ambient lighting from `scene->time_of_day`, clears depth and color buffers, and executes 3D perspective room rendering.
- **TTY Driver**: Streams ANSI text into terminal surface.

---

## 4. Full-Fidelity SSR-to-WASM State Hydration

### 4.1 State Snapshot Serialization (`nd_state_serialize`)
Serializes complete world state into `<script type="application/json" id="bud-state">`:
```json
{
  "me": 999,
  "auth_failed": 0,
  "hp": {"val": 88, "max": 110},
  "mp": {"val": 42, "max": 60},
  "active_modal": "spell",
  "active_viewport": 1,
  "tod": 43200,
  "room": {
    "ref": 1234,
    "name": "Grand Cathedral",
    "desc": "Sunlight streams through the high windows.",
    "exits": 7
  },
  "entities": [
    {"ref": 999, "name": "Paladin", "ch": "@", "fg": 15, "loc": 1234},
    {"ref": 2002, "name": "Cathedral Abbot", "ch": "A", "fg": 14, "loc": 1234}
  ]
}
```

### 4.2 Instant Rehydration (`wasm_init`)
When `wasm_init` executes:
1. Rehydrates player credentials, vitals, active modal, active viewport, and room info.
2. Runs a compact JSON scanner (`parse_json_entities`) to repopulate the entity table.
3. Immediately calls `nd_scene_sync(&g_scene, &client_state)` before the initial frame is rendered.
4. **Result**: Zero UI flicker, zero viewport reset, and zero state drop across initial page load or reconnect.

---

## 5. Tenant Module Developer Experience (`scripts/new-nd-module.sh`)

New modules can be scaffolded instantly via:
```bash
sh scripts/new-nd-module.sh nd-trade "Merchant Trading and Barter Engine"
```
Generates a 100% compliant module skeleton:
- `include/nd/trade-types.h` (Strict two-header rule, pure structs, 0 `*_IMPL` guards).
- `include/nd/trade.h` (Public XY_DECLs).
- `include/nd/trade-bcp.h` (Declarative BCP channel definitions with cursor pack/unpack).
- `include/nd/trade-ui.h` (Isomorphic UI slot integration: trading modal & action dock button).
- `src/libnd-trade.c` (Server XY_IMPL implementation).
- `Makefile` & `test.sh` (Standard build against `/usr/include/ttypt`, dual `.so` output).
- `README.md` (Standardized documentation with man-src and wire format).
EOF