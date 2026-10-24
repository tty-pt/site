# Specification: axil-nd MCP/BCP Protocol Architecture & Modernization

**Status:** Proposed Architecture  
**Target Subsystems:** `external/axil-nd`, `external/nd-mcp`, `external/nd-bus`, `mods/nd/ux`  
**Related Documents:** `docs/ND-VIEWPORTS.md`, `external/axil-nd/IDEAS.md`

---

## 1. Executive Summary

The NeverDark Binary Client Protocol (BCP `#b`) and Mud Client Protocol (MCP) constitute the real-time presentation wire connecting the `axil-nd` MUCK kernel to client interpreters (xterm.js terminal, isomorphic Bud UI, Lamina 2D Canvas, and WebGL).

While the recent architectural refactoring successfully decoupled game-module concepts from core headers and established an isomorphic Bud SSR contract, four critical architectural gaps remain in the MCP/BCP pipeline:
1. **Registration Order Dependency:** Dynamic BCP channels depend on server-side `HD_BCP` registration sequence without dynamic name-to-opcode discovery.
2. **Asymmetric Transport:** Server broadcasts binary `#b` frames downstream, but client inputs remain restricted to raw ASCII string formatting (`send_cmd()`) requiring server-side string tokenization.
3. **Unpacked Environment Streams:** Server emits `BCP_ITEM` and `BCP_ROOM`, but the client parser ignores them, leaving entity awareness trapped in ANSI text streams.
4. **Buffer & VLA Hazards:** Variable-length arrays on the server stack and manual string slicing on the client present safety hazards.

This specification designs the next generation of axil-nd's MCP/BCP subsystem to achieve dynamic channel discovery, upstream protocol symmetry, structured entity presentation, and zero-allocation bounds safety.

---

## 2. Dynamic Channel Discovery (`ND_BCP_CHANNEL_DEF`)

### A. The Registration Sequence Hazard
Currently, modules register dynamic BCP channels into `corm`'s `HD_BCP` map during `xy_install()`:
```c
bcp_equipment = (unsigned)nd_put(HD_BCP, NULL, "equipment");
```
`corm` assigns auto-incrementing integer opcodes starting after static assignments (10=HP, 11=STATS, 12=EQUIPMENT, 13=MP, etc.). If a server loads modules in a different order (e.g. `nd-spell` before `nd-equip`), opcode 12 is assigned to spells instead of equipment, breaking client decoders.

### B. Frame Specification
Introduce standard discovery opcode `ND_BCP_CHANNEL_DEF` (`0x0E` / 14):
```
+---------------+---------------+--------------------+--------------------------+
| Magic ('#')   | Magic ('b')   | Opcode (0x0E)      | Assigned Channel Opcode  |
| 1 byte (0x23) | 1 byte (0x62) | 1 byte             | 1 byte (uint8_t)         |
+---------------+---------------+--------------------+--------------------------+
| Channel Name (null-terminated ASCII string, max 32 bytes)                     |
+-------------------------------------------------------------------------------+
```

### C. Server Emission Triggers
1. **Connection Handshake (`on_auth_success`):**
   Immediately after emitting `ND_BCP_AUTH_SUCCESS`, `nd-mcp` iterates through `HD_BCP` using `corm_next_copy` and emits an `ND_BCP_CHANNEL_DEF` frame for every registered channel.
2. **Dynamic Module Loading (`loadmod` / `xy_install`):**
   When a module is loaded into a live running server, `nd_put(HD_BCP, ...)` triggers an immediate broadcast of `ND_BCP_CHANNEL_DEF` to all connected client sockets.

### D. Client-Side Name-to-Opcode Binding Table
In `include/nd/bcp.h`:
```c
typedef struct {
    char name[32];
    nd_bcp_unpack_fn unpacker;
} nd_bcp_named_channel_t;

#define ND_MAX_NAMED_CHANNELS 32
extern nd_bcp_named_channel_t g_nd_named_channels[ND_MAX_NAMED_CHANNELS];
extern size_t g_nd_named_channel_count;

/* Modules register unpackers by semantic name */
int nd_bcp_register_channel_named(const char *name, nd_bcp_unpack_fn unpacker);

/* Called by nd_bcp_decode when ND_BCP_CHANNEL_DEF (0x0E) arrives */
static inline int nd_bcp_bind_channel(uint8_t opcode, const char *name) {
    for (size_t i = 0; i < g_nd_named_channel_count; i++) {
        if (strcmp(g_nd_named_channels[i].name, name) == 0) {
            nd_bcp_register_channel(opcode, g_nd_named_channels[i].unpacker);
            return 1;
        }
    }
    return 0;
}
```
*Result:* Tenant modules like `nd-equip` register as `nd_bcp_register_channel_named("equipment", nd_equip_bcp_unpack)` and are guaranteed correct dispatch regardless of server load order.

---

## 3. Upstream Protocol Symmetry (`ND_BCP_ACTION_INVOKE`)

### A. The Performance & Ergonomic Defect
Currently, clicking an action button in the client invokes `send_cmd("fight 42\n")` or `send_cmd("equip 105\n")`:
- Client must format string commands.
- Kernel must pass bytes to `cmd_parse()`, tokenize words, match verb names, and convert ASCII numbers back to integers.

### B. Upstream Binary Action Frame Specification
Define client-to-server opcode `ND_BCP_ACTION_INVOKE` (`0x81` / 129):
```
+---------------+---------------+--------------------+--------------------------+
| Magic ('#')   | Magic ('b')   | Opcode (0x81)      | Action ID                |
| 1 byte (0x23) | 1 byte (0x62) | 1 byte             | 4 bytes (uint32_t, LE)   |
+---------------+---------------+--------------------+--------------------------+
| Target Ref                    | Optional Payload (uint8_t count + data bytes) |
| 4 bytes (uint32_t, LE)        | variable length                           |
+-------------------------------+-----------------------------------------------+
```

### C. Kernel Interception in `on_axil_parse` (`law_io.c`)
In `src/law_io.c`:
```c
if (input[0] == '#' && input[1] == 'b' && nread >= 11) {
    uint8_t iden = input[2];
    if (iden == ND_BCP_ACTION_INVOKE) {
        uint32_t action_id = nd_bcp_read_u32(input + 3);
        uint32_t target_ref = nd_bcp_read_u32(input + 7);
        const uint8_t *payload = (nread > 11) ? input + 11 : NULL;
        size_t payload_len = (nread > 11) ? nread - 11 : 0;

        action_dispatch(player_ref, action_id, target_ref, payload, payload_len);
        return -1; /* Consumed directly; skip cmd_parse */
    }
}
```
*Result:* Fast, unambiguous O(1) binary dispatch for action buttons, inventory interactions, and combat targets.

---

## 4. Structured Environment & Entity Frame Streaming

### A. Alignment with `nd-bus` 20-Event Lifecycle
The reference universe fires 20 canonical game events in `external/nd-bus/include/nd/world-events.h`:
- `on_enter(player_ref, loc_ref)` $\rightarrow$ triggers `ND_BCP_ROOM` broadcast.
- `on_add(ref, type, v)`, `on_move(ref)`, `on_get(player_ref, ref)` $\rightarrow$ triggers `ND_BCP_ITEM` broadcast.
- `on_del(ref, type)`, `on_leave(player_ref, loc_ref)` $\rightarrow$ triggers `ND_BCP_ITEM_DEL` broadcast.

### B. Client Entity Data Model in `include/nd/bcp.h`
```c
typedef struct {
    uint32_t ref;
    uint32_t location;
    uint8_t  dynflags;
    uint16_t type;
    uint32_t actions;
    char     name[32];
    char     unparse[32];
    uint16_t fg;
    uint16_t flags;
    char     ch;
    char     art[64];
} nd_entity_desc_t;

typedef struct {
    uint32_t ref;
    char     name[64];
    char     desc[256];
    uint32_t exits;
} nd_room_desc_t;

#define ND_MAX_ENTITIES 64

/* Integrated into nd_client_state_t */
typedef struct {
    /* ... auth and bar state ... */
    nd_room_desc_t   room;
    nd_entity_desc_t entities[ND_MAX_ENTITIES];
    size_t           entity_count;
} nd_client_state_t;
```

### C. Presentation Layer Integration
1. **Lamina 2D Canvas (`nd_lamina.c`):** Renders room ground tiles from `room.ref` and draws entity sprites at spatial coordinates using `entities[i].art` and `entities[i].ch`.
2. **WebGL Viewport:** Mounts 3D meshes or textured billboards matching entity types.
3. **Modular HUD Overlay Slot:** Renders a floating target HUD over the viewport when `st->target_ref` matches an active entity in `st->entities[]`.

---

## 5. Bounds-Safe Zero-Allocation Frame Primitives

### A. Zero-Allocation Packet Builder (`nd_bcp_builder_t`)
Eliminates all stack variable-length arrays (`VLA`) in server code:
```c
typedef struct {
    uint8_t *buf;
    size_t   cap;
    size_t   len;
    int      error;
} nd_bcp_builder_t;

static inline void nd_bcp_builder_init(nd_bcp_builder_t *b, uint8_t *buf, size_t cap, uint8_t iden) {
    b->buf = buf;
    b->cap = cap;
    b->len = 0;
    b->error = 0;
    if (cap < 3) { b->error = 1; return; }
    b->buf[0] = '#';
    b->buf[1] = 'b';
    b->buf[2] = iden;
    b->len = 3;
}

static inline int nd_bcp_build_u16(nd_bcp_builder_t *b, uint16_t val) {
    if (b->len + 2 > b->cap) { b->error = 1; return 0; }
    b->buf[b->len++] = (uint8_t)(val & 0xFF);
    b->buf[b->len++] = (uint8_t)((val >> 8) & 0xFF);
    return 1;
}

static inline int nd_bcp_build_u32(nd_bcp_builder_t *b, uint32_t val) {
    if (b->len + 4 > b->cap) { b->error = 1; return 0; }
    b->buf[b->len++] = (uint8_t)(val & 0xFF);
    b->buf[b->len++] = (uint8_t)((val >> 8) & 0xFF);
    b->buf[b->len++] = (uint8_t)((val >> 16) & 0xFF);
    b->buf[b->len++] = (uint8_t)((val >> 24) & 0xFF);
    return 1;
}

static inline int nd_bcp_build_str(nd_bcp_builder_t *b, const char *s) {
    if (!s) s = "";
    size_t slen = strlen(s) + 1;
    if (b->len + slen > b->cap) { b->error = 1; return 0; }
    memcpy(b->buf + b->len, s, slen);
    b->len += slen;
    return 1;
}
```

### B. Safe String Extraction Helper (`nd_bcp_read_str`)
Guarantees null-termination and bounds protection against malformed frames:
```c
static inline int nd_bcp_read_str(const uint8_t **p, size_t *rem, char *dst, size_t dst_cap) {
    if (!p || !*p || !rem || !dst || dst_cap == 0) return 0;
    size_t len = 0;
    const uint8_t *cur = *p;
    while (len < *rem && cur[len] != '\0')
        len++;
    if (len >= *rem) return 0; /* Unterminated string */
    
    size_t copy_len = (len < dst_cap - 1) ? len : dst_cap - 1;
    memcpy(dst, cur, copy_len);
    dst[copy_len] = '\0';
    *p += (len + 1);
    *rem -= (len + 1);
    return 1;
}
```

---

## 6. Phased Implementation Strategy

```
  +─────────────────────────────────────────────────────────────────────────────+
  │ Phase 1: Primitives & Discovery                                             │
  │ • Add nd_bcp_builder_t and nd_bcp_read_str to include/nd/bcp.h              │
  │ • Implement ND_BCP_CHANNEL_DEF in external/nd-mcp and include/nd/bcp.h      │
  │ • Switch tenant modules (nd-equip) to nd_bcp_register_channel_named()       │
  +─────────────────────────────────────────────────────────────────────────────+
                                       │
                                       ▼
  +─────────────────────────────────────────────────────────────────────────────+
  │ Phase 2: Protocol Symmetry                                                  │
  │ • Add ND_BCP_ACTION_INVOKE upstream frame parsing in law_io.c / on_axil_parse│
  │ • Wire client buttons in nd_ui.c to emit binary action frames               │
  +─────────────────────────────────────────────────────────────────────────────+
                                       │
                                       ▼
  +─────────────────────────────────────────────────────────────────────────────+
  │ Phase 3: Structured Presentation                                            │
  │ • Unpack ND_BCP_ITEM and ND_BCP_ROOM into nd_client_state_t                 │
  │ • Feed entity table into nd_lamina.c 2D tile rendering & WebGL billboards    │
  │ • Connect target HUD overlay to active entity selections                    │
  +─────────────────────────────────────────────────────────────────────────────+
```

This specification transforms axil-nd's BCP into a modern, symmetric, bounds-safe client protocol supporting rich multimodal presentation while strictly preserving pure-C isomorphic performance.
