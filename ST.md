# ST.md — spacetime: worlds, regions, delegation and runtime ND modules

Single design + work document for wiring the NeverDark engine (`external/axil-nd`)
into this site and giving every *area* of the ND cosmos its own
runtime-loadable, persistent, hierarchical set of modules, isolated by libxylem
**regions**.

This file is the authoritative plan **and** is self-sufficient: everything needed
to understand the goal, the vocabulary, the mechanics of the dependencies and the
work is stated here. `external/axil-nd/MODS.md` and `~/nd/ND_PORT.md` are kept as
*provenance* for the earlier port work and are not required reading (§13).

---

## 0. Objective

1. The site boots NeverDark as a normal `mods.load` module (Phase 1).
2. Any area of the cosmos — from a whole world down to a single room — can load
   its own modules at runtime (Phase 2).
3. Players write and enable modules from inside the game, so load/unload cannot
   go through `mods.load`; it must be a runtime operation that persists per area
   (Phase 2).
4. Areas are **regions**: the same mechanism must express a big region (a ruler
   governs a planet) and smaller nested regions (two halves, each with its own
   ruler), with the outer ruler able to **impose or limit** the code the inner
   rulers may run, and the inner rulers able to code freely within those limits
   (Phase 3).
5. Persistence is per area: on boot the whole region tree and its module set are
   restored.

Concretely, in priority order:

6. **Establish a planet in-game.** A player runs an in-game command that creates
   the planet's region, records who rules it, and makes it current for that
   player. A **planet is one world**: the complete 16-bit slice of the 4th
   coordinate.
7. **Load its modules in-game.** The modules loaded into a planet's region are a
   **moderation decision, per planet** — planets are not required to run the same
   set, and nothing forces every planet to run everything.
8. **Load/unload at runtime.** Further `load` / `unload` / `release` commands work
   while the server is running, from inside the game.
9. **Persist across reboot.** Ownership and the per-planet module set live in the
   engine's corm store and are restored on the next boot. Nothing about a planet
   depends on `mods.load`, a source checkout, or a hand-edited file.
10. **Full scoping.** A planet's modules only ever see players, objects and
    events inside that planet (and, where delegation is intended, inside regions
    nested within it).

The working test target is **two planets** with *different* module sets, so that
scoping, independence and persistence are all actually observable.

---

## 1. Decisions (locked)

| # | Question | Decision |
|---|---|---|
| 1 | What is a world? | Each value of the 4th dimension coordinate (`pos[3]`) of the libislet 4D (Morton) search. World `N` is the `pos[3] == N` slice of the shared keyspace. But a *region* is any owned Morton prefix at any shift; a world is just the coarsest prefix you designate. Regions may be bigger or smaller and are hierarchical. |
| 2 | Unit of region | **st prefix = region.** A region is any owned Morton prefix at a chosen `shift` (0..64). |
| 3 | Region vs world | Related concepts; regions are specified spatially (Morton / `st`). |
| 4 | Region creation | Add **`xy_claim_at`** to libxylem (create region at a caller-chosen prefix, under the nearest existing ancestor). Dependency edit is allowed. |
| 5 | Region codec | Engine's `pos_morton` (`pos[3]` in the top bits, x/y/z interleaved below). `map.c` keeps libislet's own internal 4D code for storage. |
| 6 | Persistence shape | One corm row per region holding owner + prefix length + module list (§8.2). The old `~/nd` derived-path shape (one `libnd.so` per prefix) is replaced, because a planet needs *many* modules, not one. |
| 7 | Area module ABI | **XY** modules (`XY_DEF` hooks), loaded with `xy_load` into the region. Replaces the legacy `struct nd` vtable injection. |
| 8 | Engine edits | In the submodule: commit there, then bump the site gitlink. |
| 9 | `/nd` assets | Resolution must be site-independent (no assumption of this repo's layout). |
| 10 | What is a *planet*? | **A planet *is* a world**: region id `= (uint64_t)N << 48`, `plen = 16`, `shift = 48`. |
| 11 | Which modules a planet runs | **Per-planet, moderator's choice.** No global requirement that all planets agree. |
| 12 | How modules reach a planet | **Dynamic, at runtime.** Never through `mods.load`. Binaries resolve through the loader's normal search path; the system's installed `libnd-*.so` are acceptable for now (a later chroot will change where they live). |
| 13 | Are a planet's modules scoped to it? | **Yes, fully** — the planet, plus any region deliberately nested inside it. |
| 14 | How does a planet get its region id | Deterministic: `id = st_key.key << st_key.shift`. Idempotent across reboots; nothing to reconcile. |

### 1.1 Delegation

A ruler may govern a large region, split it, and impose limits on the code its
sub-rulers run; sub-rulers code freely inside those limits and may nest further.

The intent was to map this onto libxylem's region tooling: `xy_deny` (block a
hook/module in a subtree), `xy_intercept` (outermost-first middleware to impose
code), `xy_pledge` (exclusive call rights to a hook in a region), per-region state
(`XY_REGION_STATE` / `xy_region_state`), and the claim gate (`xy_require_claim`),
which lets a parent approve/deny/limit the width of sub-regions a child module
requests.

> **Correction, verified against `external/libxylem`:** `xy_intercept` and
> `xy_pledge` **do not exist**. `grep` over `xy.h`, `xy-mod.h` and `src/*.c` finds
> neither. What actually exists, and what delegation therefore uses:
>
> - **`xy_deny(what, XY_DENY_MODULE)`** — a module that may not be loaded inside
>   this region's subtree. This is the "limit what code an inner ruler may run"
>   primitive, and the one that matters for planets.
> - **`xy_deny(hook, XY_DENY_HOOK)`** — a hook no module in this subtree may
>   implement. Checked along the whole ancestor chain before dispatch
>   (`libxylem-dispatch.c:255-267`), so a cosmos-level ruler can forbid an event
>   implementation planet-wide.
> - **`xy_require_claim(handler, ud)`** — closes the region: after this, a
>   `xy_load` into it is permitted only if the module exports `xy_claim`, and the
>   handler decides how wide the child region may be (`granted_bits`). The
>   "approve and bound the sub-region" primitive.
> - **Per-region module state** (`xy_region_state_size` / `XY_REGION_INIT` /
>   `XY_RS`) — each module gets private state per region, which is what a planet's
>   code needs to keep its own data.
> - **The planet record's module list** — the moderator's *allow-list*. `load`
>   refuses anything not on it. The coarse "what code may exist here at all" gate;
>   `xy_deny` is the fine one.
>
> If interception-style middleware (an outer ruler amending a planet's output in
> flight) is wanted, it must be added to libxylem, or it falls out of §8.4's
> dispatch walk, which already runs the ancestor chain coarse-to-fine and therefore
> gives each ruler in the chain a turn at the same hook on the same return buffer.

---

## 2. Key concepts

### 2.1 Positions

From `external/axil-nd/include/uapi/st.h:11-20`:

```c
typedef int16_t  coord_t;
typedef coord_t  point_t[DIM];      /* DIM == 2, the ND play plane */
typedef coord_t  point4D_t[4];
typedef point4D_t pos_t;            /* what the map stores */
typedef uint64_t morton_t;
```

ND's spatial plane is 2D (`include/st.h:23`, `Y_COORD 0` / `X_COORD 1` at
`:16-17`), but positions carry **4 coordinates**: `pos[0..2]` are the spatial
axes and **`pos[3]` is the world axis**. `morton_pos` reads it back with
`OBITS(code) == code >> 48` (`include/st.h:26`, `src/spacetime.c:212`).

### 2.2 A world is a coordinate; a region is a prefix

- `pos_t` is `coord_t[4]`; ND's plane is 2D but positions carry 4 coordinates.
  `pos[3]` is the world axis.
- `pos_morton(pos)` (`src/spacetime.c:137-168`) = `spread3(x) | spread3(y)<<1 |
  spread3(z)<<2 | ((morton_t) pos[3] << 48)`: **the 4th coordinate occupies the
  top 16 bits**, x/y/z interleaved in the low 48. This yields a hierarchy of
  prefixes over the full position, so *world = plen 16 node*, *room = plen 64*,
  *cosmos = plen 0*.
- libislet's own 4D encoding is different: `spread4(x) | spread4(y)<<1 |
  spread4(z)<<2 | spread4(w)<<3` (`external/libislet/include/ttypt/morton.h`, dims
  interleaved every 4th bit). A fixed-`pos[3]` slice is **not** a prefix box, so
  islet's code cannot key hierarchical regions. `map.c` therefore uses islet's
  code for **storage** while regions use `pos_morton` for **scope**.

> Minor pre-existing defect worth fixing opportunistically:
> `morton3_pack_u16` takes a `world` argument and **ignores it**; `pos_morton`
> ORs `p[3] << 48` itself at `:167`. Two places look like they own the world axis
> and only one does.

### 2.3 Region prefix ↔ `shift` alignment

For a position `p` and a chosen `shift` (0..64):

```
region_id = pos_morton(p) & mask(64 - shift)   /* keep the high (64-shift) bits */
plen      = 64 - shift
```

- `shift = 64` → `id 0`, `plen 0` = root (cosmos).
- `shift = 48` → `plen 16` = one world — **a planet** (a single `pos[3]` value).
- `shift = 0`  → `plen 64` = one cell.
- Smaller `shift` = finer region; larger = coarser. Region ancestry is
  libxylem's `(child_id & mask(parent_plen)) == ancestor_id`, which for these ids
  *is* spatial containment. No translation layer is needed.

| `shift` (= 64 − `plen`) | `plen` | region |
|---|---|---|
| 64 | 0 | cosmos / root (id `0`) |
| 48 | 16 | **a world — a planet** (id `N << 48`) |
| 32 | 32 | a super-region inside a planet |
| 16 | 48 | a large district |
| 0 | 64 | a single cell |

Two consequences the design rests on: the top 16 bits of the Morton code are
exactly the world, so the prefix `code & mask(16)` is exactly "world `N`" and is a
genuine bit prefix; and truncating the interleaved part at any depth yields
complete coordinate triples, so every prefix is an axis-aligned box.

### 2.4 `struct st_key` — the persisted spatial key

`include/st.h:59-62` and `:97-104`:

```c
struct st_key { uint64_t key; unsigned shift; } __attribute__((packed));

static inline struct st_key st_key_new(uint64_t key, unsigned shift) {
    st_key.key   = key >> shift;   /* a POSITION goes in; the PREFIX comes out */
    st_key.shift = shift;
    return st_key;
}
```

`st_key_new` takes a **position** and stores its **prefix**. A planet:

```c
struct st_key planet_key = st_key_new(pos_morton(pos), 48);  /* key == pos[3] */
uint64_t      planet_id  = (uint64_t)planet_key.key << 48;   /* == pos[3] << 48 */
unsigned      planet_plen = 64 - planet_key.shift;          /* == 16 */
```

> **Defect not to copy.** `src/spacetime.c:1117` and `:1145` shift a value that
> `st_key_new` has *already* shifted — a double shift. `~/nd` has the same bug
> (`~/nd/src/spacetime.c:1077`). The encoding above is the clean one; nothing in
> the new code path may re-shift.

### 2.5 Legacy per-area loading already existed (to be replaced)

`src/spacetime.c:1113 st_open()` dlopens the legacy `struct nd` module for a
spatial prefix, and an `st_run`-style dispatch hands engine symbols to each
owning region per shift. This is the non-region ancestor of the new design. It
is dead in practice (§6.3) and its *mechanism* is replaced, but its *semantics*
— persisted prefix→owner, boot restore, coarse→fine delivery, ownership-bounded
delegation — are exactly what the new design keeps.

---

## 3. How `~/nd` used to do it (the model we are modernising)

### 3.1 Two module tiers

**Tier 1 — global modules (flat, persisted):** `~/nd/src/interface.c`

- `mod_hd` (in-memory path→id) + `mod_id_hd` (persistent qdb `"module_id"`,
  `:690`).
- `_mod_load` (`:320`): `dlopen(RTLD_NOW|RTLD_LOCAL|RTLD_NODELETE)`, inject the
  engine vtable (`struct nd *ind = dlsym(sl,"nd"); if (ind) *ind = nd;`),
  `mod_auto_init`, then `mod_install` (first load) / `mod_open` (already known).
- `mod_load_all()` (`:361`) re-opens every path in the persistent `module_id`
  table at boot. The global set was persisted in the DB, not a file.
- The port replaced this tier with `mods.load` + XY; `mod_hd` / `mod_id_hd`
  survive in `src/world.c` but `struct nd` is gone.

**Tier 2 — spatial modules (the region ancestor):** `~/nd/src/spacetime.c`

- `struct st_key { uint64_t key; unsigned shift; }`;
  `st_key_new(key,shift) = { key>>shift, shift }`. `shift=64` → key 0 = whole
  cosmos; `shift=i` → prefix = top `64-i` bits; smaller `i` = finer. So
  `(key,shift)` is a **Morton prefix** and `owner_hd` is a persisted prefix
  forest.
- `owner_hd` (persistent) prefix→owner; seeded with player 1 owning `(0,0)`.
- `st_put(owner,key,shift)` (`:1102`): `mkdir /var/nd/st/<shift>/<key>>shift/`,
  then `st_open` **dlopens `/var/nd/st/<shift>/<key>>shift>/libnd.so`**, injects
  `struct nd`, stores the handle in `sl_hd[st_key]`.
- `st_init()` (`:1114`) iterates `owner_hd` and `st_open`s every prefix →
  per-area modules restored from persistence at boot.
- `st_run(player,symbol)` (`:1188`): `position = map_mwhere(player.location)`;
  `_st_run(...,0,64)` (cosmos module) then for `i=63..0`
  `_st_run(player,symbol,position,i)` → `dlsym` + call that symbol in the module
  owning `(position,i)`. So an engine symbol is delivered to **every ancestor
  region, coarsest→finest**.
- `do_stchown` (`:1234`) assigns prefix ownership, bounded by `st_high_shift` (the
  coarsest area you own). `do_streload` (`:1259`) is `dlclose`+`st_open` = hot
  reload.
- Boot (`~/nd/src/interface.c:800-805`): fresh DB → `st_put(1,0,64)` +
  `st_run(-1,"mod_init")`; existing DB → `mod_load_all()`.
- The repo ships `~/nd/game/st.db` (the ownership/module store) and `include.mk`
  creates `var/nd/st`. `~/nd/include/config.h` defines `STD_DB "/var/nd/std.db"`.

### 3.2 Mapping onto regions

| old `~/nd` | new |
|---|---|
| `st_key{key,shift}` | region id + `plen = 64 - shift` |
| `owner_hd` | persisted region record (owner + module list), §8.2 |
| `st_open` dlopen `libnd.so` | `xy_with_region(prefix, { xy_load("nd-core") })` |
| `st_run` per-shift `dlsym` | `xy_with_region` + exact-region dispatch, ancestor walk coarse→fine (§8.4) |
| `st_high_shift` | region ancestor chain (`st_high_shift` still guards grants, §6.3) |
| `stchown` | `xy_claim_at` + persist owner (§8.5) |
| `streload` | `xy_unload` + `xy_load` in the region |
| `sl_hd` | libxylem's `mod_by_region` index |
| `mod_load_all` / `module_id` | boot restore from the region records (§8.6) |
| one `libnd.so` per prefix | many XY modules per region (a planet runs a module *set*) |

### 3.3 Known defects to not copy

- `~/nd`'s `st_key_new` stores `key>>shift` and `st_open` shifts by `shift` again
  (double shift) — the new prefix alignment is defined cleanly in §2.3/§2.4.
- The port's `map_mwhere` now returns raw `pos_t` bytes (islet backend), not a
  Morton code, so any region lookup must call `pos_morton(pos)` rather than use
  `map_mwhere` directly.

---

## 4. How libxylem regions work (verified)

Verified against `external/libxylem` (`include/ttypt/xy.h`, `xy-mod.h`,
`src/libxylem.c`, `src/libxylem-module.c`, `src/libxylem-dispatch.c`,
`src/libxylem-internal.h`).

### 4.1 API that exists today

| API | where | meaning |
|---|---|---|
| `xy_load(fname)` | `xy.h:451`, `libxylem-module.c:114` | load a module into the **current** region |
| `xy_unload(fname)` | `xy.h:469` | unload from the current region (cascades to children; refcounted) |
| `xy_reload(fname)` | `xy.h:484` | unload + load in the same region |
| `xy_current_region()` | `libxylem.c:1486` | read the current region id |
| `xy_with_region(id, fn, ud)` | `libxylem.c:1499` | run `fn` with `id` current |
| `xy_region_each(fn, ud)` | `libxylem.c:1463` | enumerate **immediate children** of the current region (ids only) |
| `xy_deny(what, type)` | `libxylem.c:1403` | deny a hook (`XY_DENY_HOOK`) or module (`XY_DENY_MODULE`) in this subtree |
| `xy_require_claim(fn, ud)` | `libxylem.c:1256` | open/close the claim gate on the current region |
| `xy_ctx.region_id` | `xy.h:529` | the region assigned to this module at load |
| `xy_ctx.region_state` | `xy.h:541` | this module's state for the current region |
| `XY_REGION_STATE` / `XY_REGION_INIT` / `XY_RS` | `xy-mod.h:136-157` | declare per-region state from a module |
| `XY_REGION_ROOT` / `XY_REGION_INVALID` | `xy.h:357` / `:360` | `0` / `~0ULL` |

There is **no** public region-creation call, no region destructor, no `plen`
getter, no "region containing a point" lookup, and no dispatch-scope selector.
§4.5 lists the four additions this project needs.

### 4.2 Loading into a region

`_mod_load` (`libxylem-module.c:47-112`) begins with

```c
xy_load_txn_t tx = { .inherited_region_id = xy_current_region_id };
```

so a module is loaded into **the region that is current at the call**. The normal
path is therefore:

```c
xy_with_region(planet_id, (xy_scope_fn_t *) { xy_load("nd-shop"); }, NULL);
```

There is one exception. If the module exports
`XY_MODULE_API uint8_t xy_claim` **and** the current region has `require_claim`
set, `_xy_claim_for_load` (`libxylem.c:1330-1397`) runs instead: it calls the
region's claim handler, takes `granted_bits`, allocates a child region, re-keys
the module into it, and makes it current. Consequences:

- **A planet region must *not* set `require_claim`** if its content modules are
  meant to sit in the planet itself. The claim gate is for regions whose
  occupants each deserve their own sub-region (a sub-ruler claiming land).
- `granted_bits` can be smaller than requested, and `XY_ERR_EPERM` if the handler
  declines — exactly the "limit an inner ruler's footprint" behaviour.

### 4.3 How a child region id is built

`region_alloc_slot` (`libxylem.c:1291-1317`):

```c
cplen      = parent->plen + bits;
slot_shift = 64 - cplen;
candidate  = parent->id | (s << slot_shift);   /* s = lowest free slot, from 0 */
```

Three things follow:

1. **A child may jump any number of bits.** `bits` is bounded only by
   `cplen <= 64`, so a `plen 16` child of the `plen 0` root is legal — which is
   what a planet needs.
2. **libxylem's own allocator yields canonical prefixes** (`s` starts at 0, and
   `s == 0` always collides with the parent, so the lowest set bit of a child's id
   sits at `64 - cplen`). A spatially chosen id need not be canonical: planet
   `N = 2` has id `2 << 48`, whose lowest set bit is bit 49, which would imply
   `plen 15` if inferred. **Nothing in libxylem infers `plen` from an id** —
   `plen` lives in `xy_region_entry_t` (`xy.h:96`, `libxylem.c:1293`), lookups are
   plain `corm_get(region_hd, &id)` (`libxylem.c:181`), ancestry walks parent
   pointers (`region_ancestor_chain`, `libxylem-internal.h:139`), and
   `REGION_MASK 0x7FFF` is a corm *table* mask like `MOD_MASK`, not a key mask. So
   non-canonical spatial ids are safe. This gets a test (§9, Phase 0) because it is
   the kind of assumption that would be expensive to discover later.
3. **A new region needs bits the parent does not already have.** If the requested
   id equals the nearest existing ancestor's id, there is nothing to create and
   the call must fail rather than silently alias.

### 4.4 Dispatch semantics — the important asymmetry

`xy_call` dispatches to the current region's **subtree**:
`region_rebuild_hook_dispatch` (`libxylem-dispatch.c:130-147`) walks
`re->subtree_mods[]`, which `region_collect_subtree_mods`
(`libxylem.c:333-347`) fills in DFS order — **the region's own modules first,
then its children's, recursively.**

Consequences:

- Dispatching from the **root** reaches every module everywhere.
- Dispatching from a **planet** reaches that planet and everything nested in it —
  and *nothing else*. Ancestor-region modules do **not** run.
- Deny, by contrast, is evaluated over the **ancestor chain**:
  `libxylem-dispatch.c:255-267` walks `anc_chain` and fails the hook with
  `XY_ERR_EPERM` if any ancestor denied it. So *control* flows upward and
  *content* flows downward.

There is currently no way to say "run only the modules of region R itself, not its
subtree". That is what §8.4 needs, and it is addition #4 in §4.5.

A second dispatch hazard is already measured and recorded upstream
(`external/axil-nd/MODS.md` §12.2): **`xy.last()` cannot be read mid-dispatch.**
`xy_last_ran` is zeroed before the module loop and set only afterwards
(`libxylem-dispatch.c:271` before, `:338` after), so any handler calling
`nd_last()` sees `XY_ERR_NOTFOUND`; and a handler that triggers a *nested*
dispatch leaves the flag set, so a later sibling can read a nested hook's result
and mistake it for its predecessor's. Five module-side call sites depend on
`sic_last()`-style chaining (`class`, `race`, `seat`, `equip`, `spell`) and are
therefore unproven. §8.4's coarse-to-fine walk is incidentally the *right* place
for that chaining to become observable, because each region gets its own dispatch.

### 4.5 Additions libxylem needs (Phase 0)

Four, all additive:

1. **`xy_claim_at(uint64_t id, uint8_t plen, xy_claim_handler_fn_t *fn, void *ud)`**
   Create the region `(id, plen)`.
   - Find the nearest existing ancestor `A` = the region with the largest
     `plen_A < plen` such that `(id & mask(plen_A)) == A->id`. The root always
     qualifies.
   - If a region with exactly this `id` and `plen` already exists: succeed
     idempotently, make it current, return it.
   - Reject `plen > 64`, `(id & mask(plen)) != id` (misaligned), and
     `id == A->id` (no new bits).
   - Otherwise allocate the entry (`depth = A->depth + 1`, `owner_path = NULL`),
     wire it into `A->children_head`, `corm_put(region_hd, …)`,
     `region_mark_subtree_dirty(A)`, `region_dispatch_gen_bump(A)`, optionally
     install `fn`/`ud` as the child's claim handler, and make it current.
   - Does **not** create intermediate ancestors: §4.3(1) makes a multi-bit jump
     legal, so the region is created directly under `A`.
2. **`xy_region_plen(uint64_t id)`** → `uint8_t` (or `XY_ERR_*`). Needed because
   `xy_region_each` hands out ids with no widths, and the engine must be able to
   re-derive a region key.
3. **`xy_region_at(uint64_t prefix_id, uint8_t plen)`** → id of the **deepest**
   existing region whose prefix covers `(prefix_id, plen)`, else
   `XY_REGION_INVALID`. This is the engine's "which region is this point in?"
   primitive.
4. **A self-scope dispatch.** Smallest form: `xy_call_self()` alongside
   `xy_call()`, backed by a dispatch rebuild that uses the region's *own* modules
   only. Alternative form: an `xy_scope_t` parameter threaded through
   `xy_with_region`. Either lets §8.4 walk the ancestor chain coarse-to-fine and
   land one turn in each region instead of re-dispatching the whole subtree.

Surface all four in `xy.h`, `xy_t`, `xy-mod.h`; document in `docs/api.md`.

### 4.6 Region lifetime

Region entries are **never destroyed**; `region_entry_free` exists but is not on
the unload path. So "releasing" a planet means: unload its modules, drop its
ownership, drop its corm row — and leave an inert region entry in the tree. For
this project that is acceptable and is stated rather than hidden. If a planet is
later re-established with the same world number, `xy_claim_at` finds the existing
entry and reuses it.

---

## 5. The nd-\* modules

### 5.1 Inventory and dependency order

Nineteen ported modules, each its own repository at `~/axil-nd-<name>`, built and
installed as `libnd-<name>.so` plus the unprefixed `nd-<name>.so` soname link, and
loaded with `xy_load("nd-<name>")`. `axil-nd-testprobe` is a test fixture, not a
module:

```
attr  biome  class  core  drink  equip  fight  level  mob  mortal
other  plant  race  seat  shop  spell  stone  vanilla  wts
```

Dependencies, measured from `#include <nd/…>` (`A -> B` means *A depends on B*):

```
attr   -> level
class  -> attr, level
race   -> attr
mortal -> attr
drink  -> mortal
fight  -> level, attr, mortal
equip  -> attr, fight
seat   -> fight
plant  -> drink
mob    -> fight, plant
spell  -> attr, equip, fight, mortal, seat
leaves (no module deps): stone  biome  wts  shop
```

Build/load order satisfying the graph:

```
wts biome stone shop | attr race mortal class drink fight | equip seat plant mob spell
```

`attr` is depended on by six modules directly and ten transitively, so it goes
first in its wave and its header shape (`enum attribute`, `ATTR_MAX`, eight
exported services) is treated as frozen.

`nd-core` is separate from that graph and load-bearing for all of it: it is the
**sole owner of `on_icon`** and exposes `core_icon_decorate()` so that `shop`,
`drink`, `fight` and `plant` register an amend function instead of
co-implementing the hook. That restores the old engine's `sic_call` semantics
(iterate a table in registration order, thread the running value through) inside
the XY model rather than beside it. Any planet that loads `shop` must also have
`nd-core` present.

### 5.2 Provenance — three copies of the modules, only one is the port

| where | what it is |
|---|---|
| `~/axil-nd-<name>` | **the ports.** One local git repo per module, branch `alt`, remotes set to the old `git@github.com:tty-pt/nd-<name>.git`. Nothing pushed. All nineteen build and have a `.so`. |
| `site/external/nd-*` | nineteen **git submodules** pinned to the old upstream `main`/`v1.0.x` commits — i.e. *pre-port* code (e.g. `external/nd-core/main.c`, not the ported `src/libnd-core.c`). They are the old SIC modules, not the XY ones. |
| `/lib/libnd-*.so` | the **installed** sonames. Present for all nineteen. Usable now, per decision §1.12. |

This mismatch is the biggest trap in the integration: building `external/nd-*`
produces the *old* modules. The current plan loads by soname from the search path
(§1.12), so the stale submodules stay off the module search path; reconciling them
is a separate cleanup (§11).

### 5.3 Known module-side gaps (not blockers for planets, do not regress them)

- **`xy.last()` mid-dispatch** (§4.4) — five `sic_last()`-style sites unproven.
- **`eng_nd_flush` sweep** — `eng_nd_write` is a history+dedup filter, not an
  append buffer, so any `do_*` handler that fires an event whose handler writes
  must end with `eng_nd_flush(player_ref)`. Fixed for `do_status` and
  `do_connect`; the rest of the command table is unswept and untested.
- **Seven non-events** (`on_birth`, `on_death`, `on_murder`, `on_will_attack`,
  `on_mortal_life`, `on_mortal_survival`, `on_mortal_update`) have no firing site
  in the engine at all and never had one. They ship commented out, deliberately.

---

## 6. Current state of the port

### 6.1 Engine — `external/axil-nd` (submodule, v1.0.0 `a8abdea`)

- Builds `lib/libaxil-nd.so`; links `-laxil -lcorm -lxylem -lislet -lqsys
  -laxil-tty`; `-include ./../mk/include.mk`; `-L../axil-tty/lib
  -I../axil-tty/include`.
- **Single-TU libxylem module**: `src/libaxil-nd.c` `#include`s `nd_xy.c`,
  `nd_events.c`, `nd_api.c`. Required, not stylistic: `xy-mod.h` declares **one**
  `static struct xy_ctx xy` and one weak `get_xy_ptr()` per TU, and the host
  injects into exactly one. Two TUs ⇒ the other one's `XY_CALL`/`xy_load` call a
  `NULL` function pointer.
- `xy_install()` (`src/libaxil-nd.c:360-381`) boots the world, registers `GET:/nd`
  and `GET:/tty`, reads a module list, `xy_load`s content modules.
- `GET:/nd`: a WebSocket upgrade goes to `axil_ws_upgrade`; anything else is sent
  `htdocs/index.html` via `axil_sendfile` (`src/libaxil-nd.c:338-357`).
- Client assets: `package.json` builds a **gitignored** `htdocs/` (tailwind
  `app.css`, bundled `index.js`); the engine's `serve.allow` maps `htdocs /nd/*`
  and `art /nd/art/*`. `AXIL_HTDOCS`/`AXIL_PREFIX` exist but are unused. `make`
  prints `find: './htdocs': No such file or directory` — harmless.
- The engine's `on_axil_*` hooks are **not gated** to `/nd`: they fire for every
  connection on the port.

**The global module tier (exists, works).** `nd_mods_load()`
(`src/nd_xy.c:130-205`) reads the engine's own `mods.load`, one module per line,
`#` comments and blank lines skipped. Three forms:

1. a bare name **with** `mods/<n>/<n>.c` present → in-tree `mods/<n>/<n>`;
2. a bare name **without** one → an **installed soname** (`xy_load` appends the
   suffix, `dlopen` resolves through the search path);
3. anything containing `/` → taken **verbatim**, which is how a sibling checkout
   (`../axil-nd-level/level`) is named.

Lines name the **stem**, never `*.so`. The Makefile's `mods:` target applies the
identical test, so build and load cannot disagree about what a bare name means.
The tracked `mods.load` currently lists six entries (`demo`, `nd-core`, `other`,
`level`, `vanilla`, `shop`) — of nineteen. This tier **stays**, and it is
deliberately *not* how planets get modules (§1.12).

### 6.2 Engine persistence

`world_db()` = `$AXIL_ND_DB`, else `"std.db"` cwd-relative (axil `chdir`s into `-C`
before modules load). Tables are `corm_open(world_db(), …)` for persisted state
and `corm_open(NULL, …)` for transient ones; corm supports many logical databases
in one file. `nd_world_init` seeds Room Zero, six elements, a skeleton, a biome
and an owner row (`st_put(1, 0, 64)`) **only when `obj_hd` was empty**.

A prior libcorm audit found that a graceful shutdown used to zero the store; that
was fixed in libcorm (`corm_save_file` early-return on `file->size == 0`, a
corrected `_corm_load` skip, and a rewritten `corm_load_file` that scans by stored
block size) and verified with a two-boot probe. The system-wide
`/lib/libcorm.so` may predate those fixes; `make && make install PREFIX=/` in
`external/libcorm` is a one-shot deployment note, not a code change.

### 6.3 Engine legacy spatial tier (present, dead)

`src/spacetime.c`:

| symbol | line | role |
|---|---|---|
| `owner_hd` / `sl_hd` | 32 | persisted `st_key -> owner`, transient `st_key -> dlopen handle` |
| `st_dir()` | ~1100 | the data directory (dirname of `world_db()`) |
| `st_open()` | 1113 | `dlopen("<dbdir>/st/<shift>/<key>>shift>/libnd.so")`, inject the legacy `struct nd`, store the handle in `sl_hd` |
| `st_put()` | 1139 | `mkdir` the two directory levels, then `st_open` |
| `st_init()` | 1151 | iterate `owner_hd`, `st_open` every prefix — the boot restore |
| `st_dlclose()` | 1164 | `dlclose` everything |
| `st_get()` | 1180 | owner of a prefix |
| `_st_can()` | 1193 | does `ref` own this prefix |
| `st_high_shift()` | 1206 | **the coarsest (largest) `shift`** the player owns, `64` for the cosmos |
| `_st_run()` / `eng_st_run()` | 1221 / 1240 | `dlsym(symbol)` in each owning module, `64` then `63..0` — an engine symbol delivered to **every ancestor region, coarsest → finest** |
| `do_stchown()` | 1262 | `stchown <player> [shift] [position]` |
| `do_streload()` | 1308 | `streload [shift] [position]` = `dlclose` + `st_open` |

`do_stchown` enforces `high_shift >= shift` — a ruler may grant only regions as
coarse or coarser than the coarsest region it owns, i.e. it delegates finer land.
Owner ref `1` is root.

Why it is dead: it `dlopen`s a single hard-coded `libnd.so` per prefix from
`<dbdir>/st/<shift>/<key>>shift/`, no such files exist, it injects the legacy
`struct nd` vtable, and it dispatches by `dlsym` rather than by XY hook. It also
carries the double-shift defect (§2.4).

### 6.4 Engine legacy vtable and commands

`shared_init()` (`src/world.c:262-386`) still fills `nd.hds[]` and the whole
`struct nd` service surface so the two agree, with the comment that the vtable
goes away in Phase 3. `mod_hd` / `mod_id_hd` / `mod_load_all` also survive in
`world.c`.

`nd_register_commands` (`src/world.c:717`) registers a table via `axil_register`,
deliberately **skipping `GET`/`POST`/`PRI`**, which stay reserved to axil's own
HTTP routing:

```
connect (CF_NOAUTH)  save (root only)  look  examine  view  say  status  pose
inventory  get  drop  select  teleport  wall  create  clone  name  recycle
avatar  bio  ban  chown  owned  man/help  stchown  streload
```

`stchown` / `streload` are the only spatial commands and are the natural
predecessors of the planet commands (§8.5).

### 6.5 Site

- Boot is `-m mods/core/core` (`start.sh`); core loads `i18n`, `common` and
  `source` hardcoded, then reads `mods.load` (`mods/core/core.c:111-141`).
- `mods.load` is stem-only and the loader hardcodes `mods/%s/%s`
  (`mods/core/core.c:26`) — no installed-soname form, no `/`-path form.
- `Makefile:92` sets `MODS != cat mods.load` and the test target does
  `for d in $(MODS); do (cd mods/$$d && ./test.sh)` — so a `/`-containing entry
  in `mods.load` breaks the test list unless filtered.
- The site does **not** build `axil-tty`/`axil-nd`; `start.sh` /
  `scripts/run-with-server.sh` do not add their `lib/` to `LD_LIBRARY_PATH`
  (which currently covers qsys, xylem, axil, axil-auth, axil-hyle, hyle, transp,
  hyle-bud, hyle-source, bud, corm, stoma — **missing libislet, axil-tty,
  axil-nd**), and `run-with-server.sh` creates `var/{poem,song,gig,grp,…}` but not
  `var/nd`.
- No `htdocs/index.html`; `serve.allow` = `htdocs /*.css`, `htdocs /*.js`,
  `htdocs /*.wasm` (no `/nd`).
- Submodules: `external/axil-nd` plus nineteen `external/nd-*` (§5.2).

**Phase 1 work from this state:**

- Build `external/axil-tty` and `external/axil-nd` from the site `Makefile`
  (axil-tty is already verified to build cleanly against the site's externals).
- `LD_LIBRARY_PATH` += `external/libislet/lib`, `external/axil-tty/lib`,
  `external/axil-nd/lib`; set `AXIL_ND_DB=var/nd/std.db`; `mkdir -p var/nd`.
- Generalise `mods/core/core.c` to the same **three forms** the engine uses
  (in-tree / installed soname / slash path), so one line can name `axil-nd` as an
  installed soname. Add `nd` to the site's `mods.load`. Filter `/` entries out of
  the `Makefile` test list.
- **Separate the two module lists.** The engine must never read the site's
  `mods.load`. Give the engine's global tier its own name/override
  (`AXIL_ND_GLOBAL_MODS`, engine-owned default), and make a missing list fail
  loudly rather than silently loading the demo.
- Portable `/nd` asset resolution — compiled-in prefix plus a **process
  environment** override, copied from `axil-tty`'s `serve_htdocs()`, never read
  from the per-request environment (which is client-populated). Plus a host-side
  `serve.allow` route so `/nd/*` and `/nd/art/*` resolve.
- Gate the engine's `on_axil_*` hooks to `/nd` and `/tty` for the **HTTP/WS**
  path. The **raw-telnet** path must stay ungated: a raw TCP guest has no
  `DOCUMENT_URI`, and `connect` is `CF_NOAUTH` by design. Gate on "is this an HTTP
  request line, and is it for /nd or /tty", not on `DOCUMENT_URI` alone.

---

## 7. The planet model

### 7.1 The shape of a planet

```
planet N          region (id = N<<48, plen = 16)   one per world
  └ district      region (plen 24..48)             optional, moderator-created
      └ room      region (plen 64)                 optional
cosmos            root region (id = 0, plen = 0)   engine's global tier
```

Each region row carries: an **owner** (an `OBJ` player ref, `NOTHING` if
unclaimed), the **prefix length**, and the **module list** (soname stems).

### 7.2 Persistence — the chosen shape

One corm row per region. Key is the existing `struct st_key`; the value becomes a
record instead of a bare `unsigned` owner:

```c
#define ND_ST_MAX_MODS 32
#define ND_ST_MOD_NAME 24        /* "nd-core", "nd-shop", … */

struct st_rec {
    uint32_t owner;             /* OBJ ref of the ruler; NOTHING = unclaimed */
    uint8_t  plen;              /* 0..64; redundant with st_key.shift, kept so a
                                 *   row is self-describing */
    uint8_t  nmods;
    uint16_t flags;             /* reserved */
    char     mods[ND_ST_MAX_MODS][ND_ST_MOD_NAME];
};
```

`st_get` / `_st_can` / `st_high_shift` become one-field reads of `->owner`.

**Why one row, not more tables:**

- **Boot restore is a single pass.** Iterate `st`, create the region, load its
  modules. Ownership and the module set are read from the same place, so they
  cannot disagree — the failure mode a two-table design invites.
- corm records are fixed-size. The one table shape it is never asked for in this
  engine is *multivalue of strings* (it uses `CM_U32` multivalue — `adrop`,
  `contents`, `obs`, `awts` — and single-valued `CM_STR` — `bcp`, `wts`). A fixed
  array avoids inventing that.
- A filesystem layout (the old `<dbdir>/st/<shift>/<key>/` with symlinked `.so`s)
  would make *where the code happens to be* the source of truth for *what is
  enabled*, which is precisely the coupling the region model exists to remove.

**Ordering rule.** `corm_iter` order is unspecified, and a child row may be
restored before its parent row. Since `xy_claim_at` creates a region directly
under the nearest existing ancestor with an arbitrary bit jump (§4.3), an
implicitly created ancestor might not be the parent the parent's own row wants.
So: **snapshot all rows, sort by `plen` ascending, then claim and load.** The set
is small, so this is free and deterministic.

**Revisit if** per-module load arguments are ever needed — then the module list
becomes pairs, or a second table keyed by `(st_key, name)`.

### 7.3 Position → region

```
uint64_t code = pos_morton(player_pos);
uint64_t rid   = deepest region containing (code, 64)      /* xy_region_at */
```

A player with no region yet is in the cosmos (`id 0`) and sees only the global
tier.

### 7.4 Scoped dispatch — how a planet's modules run

For an event whose **anchor** is a position (the player's position, or an
object's location when the event carries no player ref):

```
chain = ancestor chain of the deepest region containing the anchor,
        ordered coarsest → finest   (root first)

for R in chain:
    xy_with_region(R):
        nd_evt_x(...)          /* dispatches to R's OWN modules only */
```

This needs **exact-region dispatch** (§4.5 #4). Without it, dispatching from the
cosmos reaches every planet on the server, and dispatching from the planet never
reaches its ancestors. Walking coarse-to-fine is not decoration: it is the legacy
`eng_st_run` semantics (§6.3), and it is what makes delegation legible — each
ruler in the chain gets a turn at the hook, in order, on the same return buffer,
so `xy.last()` becomes meaningful *between* regions even though it is broken
*within* one (§4.4).

Two consequences to state plainly:

- The **engine** is the caller. It is not in the dispatch slot list; it needs no
  region membership to fire an event.
- Anchoring matters. Most events carry `player_ref`; `on_add` does not. A
  positionless event has no scoped answer and must either be scoped by its
  payload's location or be declared global.

### 7.5 In-game commands

Registered like the rest (`axil_register`, `src/world.c:626-711`). Proposed set,
all moderator-gated:

| command | effect |
|---|---|
| `planet <N>` | establish/reclaim world `N` for the caller; create the region; make it current for the caller; persist the row. Rejects if already owned by another player without the override. |
| `planets` | list persisted planets: id, `plen`, owner name, module count |
| `here` | report the caller's current region (world, `plen`, owner) |
| `loadmod <name>` | load `name` into the caller's current region; refuses if not on the region's moderator allow-list or if `xy_deny`ed |
| `unloadmod <name>` | unload it from the current region; persists the removal |
| `modlist` | what is loaded in the current region |
| `release <N>` | unload every module, drop ownership, delete the row |

Ownership bounds are inherited from `st_high_shift` semantics (§6.3): a ruler may
only act inside regions it owns, and may only grant regions as coarse or coarser
than the coarsest it owns.

### 7.6 Boot restore

```
snapshot every row from st
sort by plen ascending
for each row:
    id = st_key.key << st_key.shift
    xy_claim_at(id, 64 - st_key.shift, NULL, NULL)       /* idempotent */
    for each name in row.mods:
        xy_with_region(id): xy_load(name)                 /* soname via search path */
        on failure: log, and do NOT clear the row — a missing binary is not a
        reason to forget the intent
```

The engine's own `mods.load` is *not* consulted. That is what makes planets
survive a reboot without a hand-maintained file, and it is why §1.12 says modules
never come from `mods.load`.

---

## 8. Implementation plan

### Phase 0 — libxylem: region-at-prefix (dependency change)

`xy_claim_at`, `xy_region_plen`, `xy_region_at`, exact-region dispatch
(`xy_call_self` or a scope parameter). Surface in `xy.h`, `xy_t`, `xy-mod.h`;
document in `docs/api.md` + README. Tests (`external/libxylem/tests/`):

- `test_claim_at.c` — nested prefixes at 0/16/32/48/64; a jump that skips levels;
  idempotent re-claim; misaligned id rejected; `plen > 64` rejected;
  `id == nearest ancestor` rejected.
- **non-canonical id** — planet `N = 2` is id `2<<48`, whose lowest set bit
  implies a different width. Assert it is found, nested into, and that
  `xy_region_plen` reports 16 (§4.3(2)).
- dispatch scope — `xy_call` from a planet reaches the planet and its children
  but **not** the root and **not** a sibling planet; `xy_call_self` reaches only
  the region's own modules; a coarse-to-fine walk visits each ancestor once.
- deny — a root-level `xy_deny` on a hook fails the dispatch from a planet, with
  `XY_ERR_EPERM`.

Gate: `make` + the existing libxylem suite green.

### Phase 1 — boot ND in the site

Everything in §6.5. Verify: clean build, `/nd` client, WS `connect`/`say`
round-trip on the site port, site `make test` green,
`scripts/check-module-boundaries.sh` and `scripts/check-wasm-imports.sh` pass.

### Phase 2 — persistence & runtime load/unload

- Keep the `st` table as the persisted registry; change its value to
  `struct st_rec` (§7.2); update `st_get`/`_st_can`/`st_high_shift`.
- In-game, ownership-checked commands to claim/release a prefix and to
  load/unload/reload its XY modules at runtime (§7.5). Bounded by
  `st_high_shift` = the coarsest prefix the actor owns.
- On boot, `st_init` becomes the region restore of §7.6, recreating the region
  tree and reloading every area module.
- Gate: establish a planet, load modules, **reboot, assert the set is restored**;
  unload one, reboot, assert it stays gone; a failed load does not erase intent.

### Phase 3 — region tree & delegation

- `st_open` → `xy_claim_at(prefix, plen)` (under nearest existing ancestor) then
  `xy_with_region(prefix, { xy_load(module) })`.
- `st_run` → compute `pos_morton(player pos)`, dispatch through the region tree
  (exact-region, ancestor walk coarse→finest; §7.4).
- Delegation: an outer ruler can `xy_deny` hooks/modules over its subtree, and gate
  child regions via `xy_require_claim` (limiting requested width); inner rulers
  code within those limits and can nest further.
- Retire the legacy `struct nd` / `st_open` dlopen path.
- Gate: **two planets with different module sets** — a module in planet A never
  fires for an event anchored in planet B; both survive a reboot with their own
  sets; unloading from one leaves the other untouched.

### Phase 4 — verification gates

- libxylem: `make` + suite + `test_claim_at`.
- axil-nd: `make`, `./test`, and a live claim/load/unload round-trip that
  persists across restart.
- site: `make`, `make test`, `scripts/check-module-boundaries.sh`,
  `scripts/check-wasm-imports.sh`.

---

## 9. Verification details

| gate | command |
|---|---|
| libxylem | `make`, `make test`, plus the new Phase 0 tests |
| axil-nd | `make`, then `./test` (note: `make test` only *bakes* `test.sh` into a `test` artifact — it does not run it) |
| site | `make`, `make test`, `scripts/check-module-boundaries.sh`, `scripts/check-wasm-imports.sh` |
| end-to-end | the two-planet scenario of Phase 3 |

Known environment quirks so they are not mistaken for regressions: `make` prints
`find: './htdocs': No such file or directory` (harmless, both `htdocs/` trees are
gitignored build artifacts); the engine's persistence two-boot case has
historically flaked ~1 run in 3; `pgrep -x axil` between runs to catch stray
daemons on fixed ports; launch is
`( LD_LIBRARY_PATH=$PWD/lib axil -d -A -p $PORT -m lib/axil-nd & )` with the
SONAME stem (the loader appends `.so`).

---

## 10. Open questions

1. **Codec confirmation** — regions use `pos_morton`; islet keeps its own 4D code
   for storage. Revisit only if the map must share the region codec.
2. **`xy_claim_at` surface** — engine-driven creation at a prefix (planned) vs
   extending the claim handler with an id-out for module-driven claims.
3. **`/nd` asset contract** — env var vs install-relative path vs host handler.
4. **Module list location/default name** — `AXIL_ND_GLOBAL_MODS` vs the engine's
   own file, and whether the global tier is used at all.
5. **Whether to keep or delete the `stchown`/`streload` names** once
   region-backed.
6. **Reconcile the module copies** (§5.2) — push the `alt` branches and bump the
   `external/nd-*` submodules, or delete them and rely on installed sonames, or
   keep pointing at `~/axil-nd-*`. Today the submodules are pre-port and must
   stay off the module search path.
7. **Per-module load arguments** — the record stores bare stems; a module needing
   arguments at load time forces §7.2's shape to change.
8. **`xy.last()`** (§4.4) — fix upstream in libxylem (per-module update plus
   nested-dispatch scoping, which must land together), or keep working around it
   module-side. Five module sites affected.
9. **Region destruction** (§4.6) — inert entries left behind on release. Fine at
   this scale; would need a `xy_region_free` if planets churn.
10. **Anchorless events** — what a positionless event does under a scoped walk
    (§7.4) is undecided: fall back to the global tier, or refuse.

---

## 11. Status

- [x] design captured here (single doc)
- [x] decisions locked
- [x] `external/axil-tty` builds against the site's externals
- [ ] `external/axil-nd` builds against the site's externals
- [ ] Phase 0 libxylem: `xy_claim_at` + `xy_region_plen` + `xy_region_at` +
  exact-region dispatch
- [ ] Phase 1 wired + verified
- [ ] Phase 2 persistence + runtime load/unload
- [ ] Phase 3 region tree + delegation
- [ ] Phase 4 gates
- [ ] archived + `npm --prefix .pi/extensions/pi-quest run zip`

---

## 12. Superseded

The separate quest file `.pi/quest/future/AXILND-ST-WORLDS.md` is folded into this
document and should be deleted; its content is §0, §1, §7 and §8 here.

---

## 13. References

Region API: `external/libxylem/include/ttypt/xy.h:96,125-129,357,360,368-491,517-541`,
`xy-mod.h:136-157`,
`external/libxylem/src/libxylem.c:86,181,289-347,1256-1515`,
`libxylem-module.c:20-149`, `libxylem-dispatch.c:130-147,205-345`,
`libxylem-internal.h:24-26,139`,
`external/libxylem/docs/api.md:189-315`, `README.md` (Region walkthrough),
`tests/test_region_state.c`, `tests/mods/mod_claim_simple.c`.

Engine positions/`st`: `external/axil-nd/include/uapi/st.h:11-20`,
`include/st.h:16-17,23,26,32,59-62,72-73,89-93,97-104`,
`src/spacetime.c:129-213,1100-1344`.

Engine module loading: `src/nd_xy.c:130-205`,
`src/world.c:262-386,387-575,615-727`, `src/libaxil-nd.c:30-31,338-357,360-381`,
`include/papi/nd-hooks.h`, `include/papi/nd-xy.h`.

Site loader: `mods/core/core.c:7-34,111-141`; `Makefile:92-101`; `start.sh`;
`scripts/run-with-server.sh`.

Legacy model: `~/nd/src/spacetime.c:1077-1300`,
`~/nd/src/interface.c:320-368,690-691,800-805`, `~/nd/include/st.h`,
`~/nd/include/config.h` (`STD_DB`), `~/nd/include.mk`, `~/nd/game/st.db`.

libislet 4D/Morton: `external/libislet/include/ttypt/islet.h:52-55,270-329`,
`morton.h`, `point.h`, `pointcfg.h`.

Prior work: `.pi/quest/future/axilnd-modcap-xy.md`,
`.pi/quest/future/axilnd-data-defaults.md` (CLOSED),
`.pi/quest/future/axilnd-raw-guest.md`,
`.pi/quest/archive/axilnd-map-libislet.zip`, `external/axil-nd/MODS.md`,
`~/nd/ND_PORT.md`.

---

## 14. Verification addendum (2026-10-01)

Sections 1–13 are unchanged and remain the design of record. **§§14–19 are an
append**: dated evidence and disambiguation, never a rewrite. Where an addendum
disagrees with an earlier section, the addendum wins, and it names the section it
amends so the conflict is traceable rather than silent.

Everything below was re-checked against the trees on disk, at these commits:

| tree | commit | state |
|---|---|---|
| `external/libxylem`, `~/libxylem` | `4372b3b` (`v1.4.1`) | `~/libxylem` has one **staged** change, §15.5 |
| `external/axil-nd` | `a8abdea` (`v1.0.0`) | clean |
| `external/libcorm` | `78bb2554` | **uninitialized** in `git submodule status`, §19.3 |
| site | `e2de0f6` | `ST.md` is `AM`; last two commits add submodules only, §19.5 |

Sources read: `external/libxylem/{include/ttypt/xy.h,xy-mod.h,src/libxylem.c,
src/libxylem-dispatch.c,src/libxylem-internal.h,src/papi.h,docs/api.md,README.md,
CHANGELOG.md}`, `external/libcorm/{include/ttypt/corm.h,src/libcorm.c}`,
`start.sh`, `scripts/run-with-server.sh`, `mods.load`, `git submodule status`,
`git ls-remote origin` in `~/libxylem`.

`git ls-remote` reports `refs/heads/main = 4372b3b`, identical to the pinned
submodule, and `git log --all -S xy_claim_at` is empty across every local and
remote-tracking branch. **There is no upstream implementation of §4.5 to pull** —
the Phase 0 work does not exist anywhere but here.

---

## 15. Disambiguation

Points that earlier sections left implicit, under-specified, or self-contradicting,
each pinned to one reading. Each bullet names the sections it amends.

- **A region's `id` is half its key, not its key — identity is `(id, plen)`.**
  §2.3 defines `region_id = pos_morton(p) & mask(64 - shift)`, so a region at
  `plen = L` has its low `64 - L` bits zero. Its two halves at `plen = L + 1`
  are then

  ```
  { id ,  id | (1 << (63 - L)) }
  ```

  **The left half's id is exactly its parent's id.** Since one shift is half the
  cosmos (§2.3), *every* level of the hierarchy contains a node whose numeric id
  equals its parent's — so id is non-unique across the whole tree, not only on the
  all-zeros path. On that path the ambiguity is total: cosmos `(0, 0)`, world 0
  `(0, 16)`, its left half `(0, 17)` … cell `(0, 64)` all have id `0`.
  Consequently `plen` is recoverable from trailing zeros *everywhere except* that
  path, which is precisely why the id looks unique and is not.
  libxylem cannot represent this today: `region_lookup` is
  `corm_get(region_hd, &id)` — id alone (`src/libxylem.c:181-184`) — and
  `region_alloc_slot` begins at `s = 0` and rejects on id collision
  (`:1306-1314`), so a same-id child is structurally impossible and the left half
  of every region is unrepresentable. §4.3(2)'s conclusion still holds (nothing
  infers `plen` from an id) but for the wrong-looking reason: the id is not a key
  at all, so there is nothing to infer from.
  *Resolution:* region identity throughout this project is `(id, plen)`. §1's
  decision 10 becomes `(N << 48, 16)`, not `N << 48`. §2.3's `region_id` column
  stays as the **prefix value** and is half the key. Amends §1.10, §2.3, §2.4,
  §4.3, §4.5; §7.6 already carries the `plen` and loses nothing.

- **§4.5's "Four, all additive" understates Phase 0.** The four calls are additive
  *once a region can be named*. Because identity must become `(id, plen)` first,
  the dependency edit reaches `region_lookup`'s key, the region corm table's key
  width, `region_ancestor_chain`, `xy_region_each`'s output, and every path that
  resolves a region from an id alone (`xy_deny`, `xy_require_claim`, `xy_call`'s
  `caller_region_entry`, `_xy_claim_for_load`). Ordering inside Phase 0: identity
  change, then the four functions, then the tests. Amends §4.5, §8 Phase 0.

- **Outer rulers impose structurally; the running value is the inner ruler's
  baseline.** §7.4's prose ("same return buffer", `xy.last()` meaningful *between*
  regions) and its pseudo-code disagreed, and the difference decides whether
  §0.4's "impose or limit" is expressible. The walk is coarse → fine, so an outer
  ruler's turn runs **first** and cannot amend an inner result by reading it.
  §0.4's words are "limit the code the inner rulers **may run**" — that is
  `xy_deny` (a hook or module no descendant may implement, refused at
  `src/libxylem-dispatch.c:256-267` with `XY_ERR_EPERM`) and the claim gate
  bounding a child's width (`xy.h:397-417`, handler signature `xy.h:388-395`).
  The inner ruler's lever is the return: it receives the value the outer left and
  may amend it, which is what a shared buffer means.
  *Resolution:* one model, two levers — outer imposes **structurally** and sets
  the **baseline**; inner amends and observes what it was given. Amends §0.4,
  §1.1, §7.4. Consequence: the §15.5 fix is a prerequisite, not optional.

- **`xy_intercept` and `xy_pledge` are a restore, not new design.** §1.1
  correctly reports them absent from `xy.h`, `xy-mod.h` and `src/*.c`, and
  concludes that interception "must be added to libxylem". Their specification
  survives intact: `docs/api.md:49` (`int xy_pledge(const char *)`),
  `docs/api.md:80` (`int xy_intercept(const char *, xy_interceptor_fn_t *, void *)`),
  worked examples in `README.md:263-409,512`, and — importantly — `xy_t` still
  reserves both slots at `docs/api.md:173,177`. The 1.1.2 CHANGELOG entry still
  advertises them.
  *Resolution:* treat reimplementation as recovering a written contract with a
  reserved ABI slot, not inventing middleware. Phase 0 edits `docs/api.md` and
  `README.md` anyway (§4.5), so it must also strike or restore the stale entries
  in the same pass — otherwise the next reader concludes from the README that the
  primitives exist. Amends §1.1; bears on §10 Q2, §10 Q8.

- **`xy.last()` mid-dispatch is already implemented, staged, and untested.**
  §4.4 and §10 Q8 record it as broken with five unproven module sites; §11 does
  not list it. `~/libxylem/src/libxylem-dispatch.c` carries a **staged** change
  (+44/−2) adding `xy_last_publish(ran, retp, reg)`, called after each
  `dispatch_call` with `ran` incremented: `xy_last_ran` becomes the number of
  modules that have *completed*, and the adapter plus `retp` are re-asserted.
  That yields the intended semantics (first module sees `0` → `XY_ERR_NOTFOUND`;
  each later one reads its predecessor's return; after the loop the count is
  total, so post-dispatch reads are unchanged) **and** fixes a second, independent
  bug — a nested `xy_call` from a handler left the flag set, so a sibling could
  read a nested hook's result as its predecessor's. There is no test for it (the
  `xy_last` coverage in `tests/{test_main,test_errors,test_ptr_args}.c` predates
  it) and no CHANGELOG entry.
  *Resolution:* adopt it as part of Phase 0, add a test, add a CHANGELOG line.
  Per the previous bullet it is a **prerequisite** of the §7.4 walk. Amends §4.4,
  §5.3, §8 Phase 0 gates, §9, §10 Q8, §11.

- **§4.2's `xy_with_region` snippet cannot compile as written.**
  `xy_scope_fn_t` is `int(void *ud)` (`xy.h:132`) and `xy_with_region_t` is
  `int(uint64_t, xy_scope_fn_t *, void *)` (`xy.h:430`); `xy_load` is a *variable*
  of type `int(char *)` (`xy.h:451-452`). The snippet

  ```c
  xy_with_region(planet_id, (xy_scope_fn_t *){ xy_load("nd-shop") }, NULL);
  ```

  casts a function-pointer *object* to a different function-pointer type and
  calls it with no argument — undefined behaviour that in practice calls
  `xy_load(NULL)`. §7.4's per-region `nd_evt_x(...)` has the same problem in a
  form that will be copied: the event and its anchor must reach the callee through
  a per-event thunk or one TLS slot, since `xy_with_region` takes only `void *ud`.
  *Resolution:* treat §4.2's line as pseudocode; Phase 0 adds a documented
  trampoline (or the TLS event slot) and §16.1 pins the pattern. Amends §4.2,
  §7.4.

- **Ancestry is a parent-pointer walk, and `plen` must never be inferred from an
  id.** Two places will push an implementer the other way: `src/papi.h:39`
  documents "Ancestry check: mask child_id to ancestor->plen significant bits and
  compare", and the 1.1.2 CHANGELOG advertises "prefix-encoded paths for O(1)
  ancestry checks". The live code is `region_ancestor_chain`
  (`src/libxylem.c:229-245`) — a pure `e->parent` walk, reversed so root is first —
  and `->plen` is only ever written or used in allocation arithmetic
  (`src/libxylem.c:375,1293,1374`). `src/papi.h:34-40` is right that plen is "not
  packed into the ID", which is exactly why it must be part of the key (previous
  bullet).
  *Resolution:* `xy_claim_at`'s nearest-ancestor search is the **first** place
  mask-based containment is used in this codebase. It may compare masked ids
  against candidate ancestors; it must never derive a width from an id. Both
  stale doc strings should be corrected in the same pass. Amends §4.1, §4.3,
  §4.5.

- **`xy_region_each` hands out ids with no widths, so its output is unusable on
  its own.** It walks `parent->children_head` / `sibling_next` and yields `e->id`
  (`src/libxylem.c:1463-1483`); `child_bits` exists to identify children
  (`src/papi.h:52-53`) but is not exposed. §4.1 already notes the missing `plen`
  getter; the sharpened form is that after the §15.1 change a bare id is ambiguous,
  so `xy_region_each` must yield `(id, plen)` or become unusable for persistence.
  *Resolution:* `xy_region_plen` (§4.5 #2) is a *prerequisite* of iterating the
  tree, not a convenience. Amends §4.1, §4.5.

- **The root tier is cosmos-wide by construction.** §6.1 keeps six entries
  (`demo`, `nd-core`, `other`, `level`, `vanilla`, `shop`) loading into the root
  region, and §7.4's chain always begins at the root — so those modules fire for
  every planet on the server, permanently, and no planet's moderation decision
  reaches them. §0.10's "a planet's modules only ever see players, objects and
  events inside that planet" is therefore true of the *planet* tier and false of
  the tree as a whole.
  *Resolution:* state it plainly — root-tier modules are cosmos-wide by
  construction and are outside every planet's allow-list. §8's Phase 3 gate must
  therefore test with a **planet-only** module, or "a module in planet A never
  fires for planet B" passes vacuously. Amends §0.10, §6.1, §8 Phase 3 gate.

- **A region row stores `plen` twice and is self-describing but not
  self-validating.** §7.2 keeps `st_rec.plen` alongside `st_key.shift` "so a row
  is self-describing", while §7.6 restores with `64 - st_key.shift`. Nothing says
  which wins if they disagree.
  *Resolution:* `st_key.shift` is authoritative (it is in the key);
  `st_rec.plen` is validated against it on restore, and a disagreeing row is
  **refused and logged**, never silently reinterpreted. Amends §7.2, §7.6.

- **The module list holds stems only, and the 24-byte field proves it by
  luck.** §6.1's third load form takes a `/`-containing path verbatim, and
  `../axil-nd-level/level` is 22 characters against `ND_ST_MOD_NAME 24`. §7.2
  revises if per-module arguments are ever needed; the cheaper invariant is to
  forbid `/` in a persisted entry at write time, so the record can never become
  the thing that breaks a path-shaped load.
  *Resolution:* `loadmod` rejects any name containing `/`; persistence stores
  stems. Amends §7.2, §7.5.

- **Two libxylem checkouts exist, and the pending work is not in the submodule.**
  `site/external/libxylem` is clean at `4372b3b`; `~/libxylem` is the same commit
  with the §15.5 change staged, plus untracked `bin-test_*` binaries in its root.
  §1's decision 8 says engine edits are committed "in the submodule, then bump the
  site gitlink", which does not describe where the work currently is.
  *Resolution:* name one canonical checkout for Phase 0, land the commit there,
  then bump the site gitlink per decision 8. Do not edit both. Amends §1.8,
  §11.

- **The two-boot persistence proof only covers one corm.** §6.2 flags that
  "the system-wide `/lib/libcorm.so` may predate those fixes" and calls it a
  deployment note. It is more than that: `start.sh:6` and
  `scripts/run-with-server.sh:32` put `external/libcorm/lib` ahead of the system
  default, so the *site* exercises the submodule's corm, while `~/axil-nd` builds
  against `/lib/libcorm.so` — the path where the shutdown-zeroing defect may still
  live. §8's Phase 2 gate is therefore proven on one path and not the other.
  *Resolution:* make "both paths, two boots each" an explicit gate item rather
  than a note. Amends §6.2, §8 Phase 2 gate, §9.

- **Phase 1 has not started.** Verified, not inferred: `mods.load` contains
  `i18n, poem, song, grp, gig` and no `nd`; neither launch script sets
  `AXIL_ND_DB` or creates `var/nd`; neither adds `external/libislet/lib`,
  `external/axil-tty/lib` or `external/axil-nd/lib` to `LD_LIBRARY_PATH`; the
  last two site commits add only submodules and `AGENTS.md`. §6.5's inventory is
  accurate and §11's unchecked boxes are correct. Amends nothing — recorded so
  §11 is not re-derived.

- **Verified clean — do not re-audit these.** §6.5's missing `LD_LIBRARY_PATH`
  entries and `AXIL_ND_DB`/`var/nd`: confirmed absent. §4.3(2)'s non-canonical-id
  safety: confirmed, no id→`plen` inference exists. §1.1's absence of
  `xy_intercept`/`xy_pledge` from code: confirmed across `include/`, `src/`,
  `tests/` (they survive only in `docs/api.md`, `README.md` and `CHANGELOG.md`).
  §2.3's `shift`↔`plen` table and §2.4's `st_key_new` semantics: taken from §13's
  citations as researched, not re-measured here.

---

## 16. Phase 0 mechanics that constrain the implementation

Cross-cutting detail behind §15. Each item is a constraint, not a decision.

- **Region entry layout.** Identity is `(id, plen)`, so `xy_region_entry_t`
  (`src/papi.h:52-60`) gains no field — `plen` already exists — but the *key* of
  the region corm table must carry both. `region_lookup` (`:181-184`) takes a
  single `uint64_t`; it needs a `(id, plen)` form, and every call site
  (`:369`, `:1169`, `:1262`, `:1311`, `:1339`, `:1365`, `:1409`, `:1470`,
  `:1511`, `src/libxylem-dispatch.c:253`) must be audited for whether it means
  "the region with this id" — previously unambiguous, now a query.
- **`region_alloc_slot` must stop refusing same-id children.** The `s = 0`
  iteration (`:1306-1314`) exists to avoid an id collision that will no longer be
  a collision. Left alone it will keep rejecting the leftmost half of every
  region, which is the case §15.1 is about.
- **`xy_claim_at` rejection rules, restated for `(id, plen)`.** Reject `plen > 64`;
  reject `(id & mask(plen)) != id` as misaligned; reject `plen == plen_A` with
  `id == A->id` as "no new bits". Accept an exact `(id, plen)` match
  idempotently and make it current. Do **not** create intermediate ancestors —
  §4.3(1)'s multi-bit jump stays legal. Create under the nearest existing
  ancestor `A` = largest `plen_A < plen` with `(id & mask(plen_A)) == A->id`, which
  is the codebase's first mask-based containment (§15).
- **Deny invariants, as testable statements.** A module's own region is never
  blocked by its own deny (`xy.h:371-374`); a deny applies to descendants; a
  dispatch is refused with `XY_ERR_EPERM` if **any** ancestor in the call chain
  denied that hook (`src/libxylem-dispatch.c:256-267`). §1.1 relies on the third;
  the Phase 0 test list should assert all three.
- **Exact-region dispatch.** `xy_call` dispatches the current region's *subtree*
  (`region_rebuild_hook_dispatch` → `re->subtree_mods[]` →
  `region_collect_subtree_mods`, `src/libxylem.c:333-347`). A self-scope variant
  needs either a second module array per region or a filtered walk; whichever is
  chosen, the §15.5 `xy_last` semantics must be re-established for it, since the
  walk's whole value is that each region gets one observable turn.
- **`xy_with_region` entry points.** Both `xy_claim_at`-then-`xy_load` and §7.4's
  per-region dispatch go through `xy_with_region(id, fn, ud)`
  (`xy.h:430,439`). Supply one documented trampoline shape and use it everywhere;
  see §15's sixth bullet for why §4.2's inline cast must not be copied.
- **Region lifetime.** Unchanged and still accepted: entries are never destroyed,
  `region_entry_free` is not on the unload path, so `release` leaves an inert
  entry and `xy_claim_at` reuses it. §4.6 stands; §10 Q9 stands.

---

## 17. Persistence refinements

- **Name the corm shape.** §7.2 argues about fixed-size records but not which
  corm API. Both fit `sizeof(struct st_rec)` (~776 B): a raw blob, or the
  record-aware map — `corm_record_register` + `CM_RECORD` with
  `corm_record_field_t` (`external/libcorm/include/ttypt/corm.h:833-844`, worked
  example `:805-830`), bounded by `CORM_MAX_RECORD_FIELDS 32`
  (`external/libcorm/src/libcorm.c:207`) and `CORM_POOL_MAX 4096`
  (`src/libcorm.c:27`). The record-aware form is the better fit because
  `corm_put(hd, key, &row)` stores the whole struct and
  `corm_get(hd, "key:owner")` reads one field, which is how ownership reads stay
  one-field reads (§7.2's requirement for `st_get` / `_st_can` /
  `st_high_shift`).
- **`nmods` is authoritative for iteration.** `char mods[32][24]` has no
  cross-element NUL guarantee, so every read of the list must be bounded by
  `nmods`, never by scanning for a terminator, and a short entry must be
  individually NUL-terminated.
- **`plen` is validated, not trusted.** See §15's eleventh bullet.
- **Stems only.** See §15's twelfth bullet.
- **The sort-before-restore rule stands** (§7.2): `corm_iter` order is unspecified
  and a child row may precede its parent, so snapshot, sort by `plen` ascending,
  then claim and load. With `(id, plen)` identity this is no longer merely
  tidy — it is what keeps an implicitly created ancestor from being the parent a
  parent's own row expects.

---

## 18. Scoping, restated against the root tier

*(Removed as redundant. Every bullet restated material already in the document:
§15's ninth bullet carries the two-planet gate point, §7.5 already states the
`st_high_shift` ownership bounds, and §10 Q10 already carries the anchorless-event
question. Numbering is retained as a tombstone so §19.3's cross-references from
§14 and §15 stay valid.)*

- The one forward-looking item worth keeping, restated: the anchorless-event
  question (§10 Q10) blocks Phase 3's dispatch walk rather than merely Phase 2,
  because every hook needs an anchor rule before §7.4 can run, and §7.4 already
  names the concrete case (`on_add` carries no player ref).

---

## 19. Environment and bookkeeping

- **One canonical libxylem checkout.** See §15's thirteenth bullet.
- **libcorm path split is a gate.** See §15's fourteenth bullet and §9.
- **`external/libcorm` is uninitialized.** `git submodule status` reports
  `-78bb2554…` while `external/libcorm/` has content and
  `external/libcorm/lib/libcorm.so` exists. A fresh `git submodule update --init`
  may therefore change which corm the site links. Worth settling before any
  persistence gate is believed.
- **The folded quest file still exists.** §12 says
  `.pi/quest/future/AXILND-ST-WORLDS.md` should be deleted; it is present, and its
  status block (lines 57-59) contradicts §11 — it lists `axil-tty` and `axil-nd`
  as unbuilt and Phase 1 as unwired, where §11 marks `axil-tty` done. Delete it,
  or reconcile it, so §11 is not left with a live dissenting copy.
- **Status delta for §11.** Two items are not in §11's list and should be, per
  §15.5 and §15.2: the `xy.last()` fix, and the promotion of region
  identity from `id` to `(id, plen)` — the latter changes what Phase 0 has to
  build, so §11's unchecked Phase 0 line is now under-specified rather than merely
  pending.

---

## 20. Supersession log (2026-10-01, second pass)

Recorded rather than edited: §§14–19 stand as written, and the statements listed
here are superseded by later commits. Each names what replaces it.

- **§14's libxylem row and its closing `ls-remote` paragraph are superseded.**
  `~/libxylem` was staged when §14 was written; it is now clean at
  `3a2d32e` **`xy.last fix`**, tagged **`v1.4.2`**, and `origin/main` has moved
  from `4372b3b` to `3a2d32e`. The fix is **published upstream**, not pending.
  §14's claim that `refs/heads/main` equalled the pinned submodule is no longer
  true; its *conclusion* survives, re-verified at the new head —
  `git grep -n 'xy_claim_at|xy_region_plen|xy_region_at|xy_call_self' 3a2d32e`
  returns nothing, so **§4.5 still has no upstream implementation to pull**.
- **§15.5's headline ("already implemented, *staged*, and untested") is
  superseded in its first clause only.** The substance stands and is sharper:
  commit `3a2d32e` touches `src/libxylem-dispatch.c` **alone** — so the published
  fix still ships with **no test and no CHANGELOG entry**, exactly as §15.5
  recorded. Its status is now "published, untagged work released as `v1.4.2`,
  untested" rather than "staged".
- **§15's thirteenth bullet and §19's first bullet are superseded: the two
  libxylem clones have converged.** `~/libxylem` and `site/external/libxylem` are
  both at `3a2d32e` (`v1.4.2`), both clean — decision §1.8's workflow (commit in
  the submodule, bump the site gitlink) was executed as written, and the §15.5
  prerequisite is satisfied. What remains is only the convention for *future*
  commits, and it is unchanged.
- **§14's axil-nd row is superseded: the submodule has moved `a8abdea` → `6f1edb7`**
  (`fix CI`, then `mods`), so the site now tracks the same commit as the sibling
  `~/axil-nd`. Consequence: **§6.1, §6.3 and §6.4's line citations were all
  measured at `a8abdea` and may have drifted two commits.** Re-verify them before
  editing against them in Phase 1 or Phase 3 — this is now a live risk rather
  than a theoretical one, and it is the one place where the addendum's evidence
  base has aged.
- **The §19 status delta is superseded in its first item.** The `xy.last()` fix
  is published and the gitlink is bumped, so it drops off §11's outstanding list.
  The second item — region identity becoming `(id, plen)` — remains outstanding
  and is now the *only* Phase 0 item §11 needs to gain.

---

## 21. Checkpoint ladder

The phased plan in §8 has no stopping points of its own, and §9 lists gates
without ordering them. This is the order that fails fastest on the riskiest thing
first, with each checkpoint independently demonstrable and leaving the tree green.

| CP | Content | Gate |
|---|---|---|
| **CP-0** | `external/axil-nd` (`6f1edb7`) builds against the site's externals | clean build; site `all:` gains `axil-tty-lib` / `axil-nd-lib` targets; nothing else changed |
| **CP-1** ✅ | close out `v1.4.2`: mid-dispatch `xy.last` test + CHANGELOG entry | **done** — new test asserts each later module sees its predecessor's return and a nested dispatch does not leak; verified red against `4372b3b`, green against `3a2d32e`. Pre-existing, unrelated libxylem suite failures found — see §24, not a CP-1 blocker |
| **CP-2** | Phase 1 wiring (§6.5) | **`/nd` client + WS `connect`/`say` round-trip on the site port**; site `make test` plus `check-module-boundaries.sh` and `check-wasm-imports.sh` green |
| **CP-3** | region identity → `(id, plen)`; `xy_region_plen`; `xy_region_each` yields plen (§15.1, §16) | `test_region_identity.c`: cosmos `(0,0)` → `(0,16)` → `(0,17)` → `(0,64)` coexist and are individually addressable; existing suite green |
| **CP-4** | `xy_claim_at` + `xy_region_at` + exact-region dispatch (§4.5) | §8's Phase 0 test list, including the non-canonical-id and deny cases |

Ordering notes:

- **CP-0 first** because `external/axil-nd/lib` does not exist while every input
  its Makefile names is already built (`-L../axil-tty/lib`, `-laxil -lcorm
  -lxylem -lislet -lqsys -laxil-tty`). It is pure mechanics and it is the only
  checkpoint that can invalidate Phase 1–3 planning outright. Settle §19.3
  (uninitialized `external/libcorm`) *before* trusting the build.
- **CP-2 needs no libxylem change** — Phase 1 uses only the existing root-tier
  region API — so CP-2 and CP-3 are independent and may run in either order.
  CP-2 is the first *demonstrable* checkpoint; CP-3 is the first that de-risks the
  design itself.
- **CP-3 is what makes CP-4 possible.** §15.1 and §16's first two bullets are the
  reason: identity must change before the four calls can be written, and
  `region_alloc_slot` must stop refusing same-id children or the leftmost half of
  every region stays unrepresentable.
- **CP-1 is independent and small**, and per §15.2 it is a prerequisite of §7.4's
  walk rather than an optional extra.

---

## 22. CP-0 — lib source decision (locked 2026-10-01)

**Single source of truth is the site submodules**, not `/usr/lib`. Considered:
site submodules / true system-only / literal hybrid.

- All six libs are installed system-wide and the installed headers match the
  submodules', so "just use the system ones" looks attractive. It is the *worse*
  choice: `start.sh:6` puts `external/*/lib` first in `LD_LIBRARY_PATH`, so the
  site loads the submodule copy at runtime regardless of what a module linked
  against. System-at-link + submodule-at-runtime is two sets plus a shadowing
  rule. See §23 for the byte-level split.
- Two targets were added to the site `Makefile` (`axil-tty-lib`, `axil-nd-lib`),
  dependencies taken verbatim from each submodule's `LDLIBS`. No edits inside
  `external/axil-nd` were needed; it already resolves axil-tty from the sibling
  tree (`-L../axil-tty/lib`, `-I../axil-tty/include`).
- **CP-0 was scoped to compile-and-link only.** `LD_LIBRARY_PATH`,
  `AXIL_ND_DB`, `var/nd`, the `core.c` loader, `mods.load`, `/nd` assets and
  hook gating are all §6.5 and all CP-2 — see the CP-2 prerequisites in §23.

---

## 23. CP-0 result (2026-10-02) — PASS

`external/axil-nd` builds via `make axil-nd-lib` and loads into axil
(`nd_world_init: Done.`); `xy_install`, the seven `on_axil_*` hooks and
`nd_register_commands` are all exported. **CP-1 is next** per §21.

> Byte-level split behind §22's decision: of the six libs, only `islet` and
> `axil-tty` differ from `/usr/lib`, and **the site build is newer in both**. Do
> not "simplify" by linking against `/usr/lib`; you would get the older pair.

### Traps for whoever runs the tests next

- **`make test` is not idempotent.** A repeat run fails ~6 e2e on accumulated
  picker junk. Run `sh scripts/gc-picker-junk.sh` first (backs up to
  `/tmp/song.types-<stamp>.tgz`). Symptom if you skip it:
  `Picker option "entry" ... not found`, `112 passed / 6 failed`.
- `is_server_up()` (`run-with-server.sh:16-18`) allows only **5s**. A cold full
  rebuild can lose that race and fail `unit-tests` with an *empty*
  `/tmp/axil_transient.log`. Retry once before diagnosing; a genuinely dead
  server instead aborts with `axil_bind: bind` (SIGABRT).
- A `-` prefix from `git submodule status` may be only a missing
  `submodule.<name>.url` in `.git/config`, not a content problem. Check the
  submodule's own HEAD before running any checkout.

### CP-2 prerequisites found while building

- **`libaxil-nd.so` has no `-Wl,-rpath`**, so `ldd` sends all six deps to
  `/lib/lib*.so`. Runtime selection is entirely `LD_LIBRARY_PATH`'s job.
- **`start.sh:6` and `run-with-server.sh:32` omit `external/libislet/lib`** (and
  have no axil-tty path). Since axil-nd links `-lislet` and `-laxil-tty`, it
  would load the **older** `/usr/lib` copies of precisely the two libraries where
  site and system differ. Both paths must be added in CP-2.
- **`-m` takes the stem, not the filename** — the loader appends `.so`, so
  `-m .../libaxil-nd` is correct and `-m .../libaxil-nd.so` fails on
  `libaxil-nd.so.so`. The submodule already creates the
  `axil-nd.so` -> `libaxil-nd.so` SONAME link.

---

## 24. CP-1 result (2026-10-02, in `~/libxylem`) — PASS

New test `tests/test_xy_last_dispatch.c` (+ 4 fixture modules
`tests/mods/mod_xylast_{inner,first,second,third}.c`) targets commit `3a2d32e`
precisely: a 3-listener dispatch where the 2nd and 3rd listener each call
`xy_last()` mid-dispatch, and the 2nd nest-calls an unrelated hook before
returning (the leak case). **Verified red** against the pre-fix dispatch.c
(`4372b3b`: aborts on the 2nd listener's read) **and green** against `3a2d32e`.
`CHANGELOG.md` gained a `## 1.4.2` entry. Not committed — nothing here was
explicitly requested to be committed.

> **Pre-existing, unrelated breakage found, not fixed, not CP-1's problem:**
> `make test-build`/`test` in `~/libxylem` were already broken before today —
> 19 `Makefile`-referenced source files never existed in git history (Rust
> bindings, game/claim fixtures, `mod_region_worker`/`mod_region_moderator`).
> Three already-building tests also fail independently of the xy.last fix
> (`test_core`, `test_errors`: `xy_errno() == XY_OK` asserted after a
> zero-listener dispatch, which correctly returns `NOTFOUND`; `test_unload`:
> unrelated `test_reload` assertion). Confirmed pre-existing by diffing
> `src/libxylem-dispatch.c` back to byte-identical with `3a2d32e` before
> re-testing. **CP-1 did not attempt to fix any of this** — do not assume `make
> test` is a usable gate here without scoping down to specific binaries first.

> **`.gitignore` bug found and fixed:** `/tests` + `!/tests/**/*.c` only kept
> *already-tracked* test files visible — git's "cannot re-include inside an
> excluded directory" rule meant genuinely new files under `tests/` (and one
> level worse, under `tests/mods/`) were silently ignored, so a plain `git add
> tests/` would have dropped the 5 new files above with no error. Fixed to
> `/tests/*` + `!/tests/mods/` + the two extension negations; verified no other
> untracked `.c`/`.h` existed under `tests/` before the fix (so nothing
> unexpected was swept in) and that `*.so`/`*.o` build output is still ignored
> after it.
> Also removed 11 stray root-level `bin-test_*` files (dated Sep 30, not from
> this session): byte-identical copies of `tests/test_*` binaries, referenced
> by nothing. Added `/bin-test_*` to `.gitignore` so they don't recur.
