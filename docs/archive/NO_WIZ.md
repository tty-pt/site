# NO_WIZ.md — drop `EF_WIZARD`: region ownership replaces the dead wizard gate

**Status: PLANNED, NOT STARTED.** No code has been changed. This file is the
implementation plan; `ST.md` §27.6(1) holds the design decision it executes.

Work happens in the **`external/axil-nd` submodule** (currently `0555d14`, clean).
Baseline: site `9fae912`, submodule `0555d14`. Prior phases are closed — Phase 4
green `120|0` (`ST.md` §31), CP-4 review accepted (§4.5 amended).

## Read these first

| Document | What it gives you |
|---|---|
| `ST.md` §27.6(1) | **The decision.** Wizard grants → region ownership; `wall`/`ban` scope to a region + subregions, default all-my-regions |
| `ST.md` §27.1, §27.3 | Anchor table; why `room <x> <y> <z> <w>` exists |
| `ST.md` §22.1–§22.5 | Region rows, `(id, plen)` discipline, the "gate on ownership, not the module list" rule |
| `external/libxylem/include/ttypt/xy.h:540-571` | `xy_region_at`, and the **prefix-containment identity** this plan leans on |
| `external/libcorm/include/ttypt/corm.h:120-141` | The four key types (`CM_STR`/`CM_U32`/`CM_PTR`/`CM_HNDL`) — there is no Morton key, so the ban key is a registered fixed-width type |
| `FIX.md` §1 | The house gate discipline (red→green, hex/byte proof) |
| **§13, §14 below** | **Implementation findings — corrections found by probing the real engine. Read before continuing.** |
| **§14 below** | **RESOLVED (`70fc37c`). `do_teleport` moved a bystander and any absent ref aborted the daemon. Read §14 for the four root causes — three of them are matcher bugs, not teleport bugs.** |

If you read one thing: §2 below. The nine sites are **not** nine of the same
kind of change, and treating them as one is how this goes wrong.

---

## 1. TL;DR

`EF_WIZARD` (flag bit 8) is **never set by any code path in the port**, so every
`st_is_wiz()` gate is dead code: the commands exist and no player can reach them.
`room`/`deny` were already re-gated onto `st_can_region` (target-region owner or
cosmos ruler) during Phase 3. The decision is not *who to grant wizard to* but to
**remove the concept**: every wizard check becomes a region-ownership check, and
`st_is_wiz` disappears. No global privilege remains to squat; authority is always
scoped to a region someone rules.

The work is three separable pieces, in increasing order of risk:

1. **Pure deletion** — provably behavior-identical (see §3). This is the gate for
   everything else: if the existing suite is green after this step, the
   `EF_WIZARD` reads provably did nothing.
2. **Seven dead commands re-gated onto ownership** — the six registered
   commands that always refused, plus the new ban model.
3. **Two live helpers granted region authority** — `eng_controls` and
   `eng_look_at`. These are **grants, not refusals**, and they are the only place
   where this work can *loosen* existing behavior.

---

## 2. The finding that shapes the plan: three of the nine sites are not dead code

Six of the nine wizard reads guard commands that **always** refuse, so opening
them is additive. Three guard helpers that **execute today**, where the wizard
clause is the branch that would have *allowed*:

| Site | Today's behavior | Consequence of re-gating |
|---|---|---|
| `eng_look_at` `entity.c:163` | `!(flags & EF_WIZARD)` is always true → **nobody** may see inside another entity | ruler-scoped = rulers inspect every entity in their region (**grant**) |
| `eng_controls` `entity.c:82` | strictly `who == what.owner` | ruler-scoped = ruler controls their region; also unlocks `teleport` and **widens name resolution** (§6) (**grant**) |
| `eng_payfor` `entity.c:68` | everyone pays from own `value` | ruler-scoped = rulers pay other people's costs — **dropped instead**, no region equivalent is meaningful |

Plus `EF_WIZARD` is never set *by construction*, not by a check:
`eng_ent_get` (`entity.c:25-31`) declares `ENT ent;`, fills it only `if (__v)`,
and returns it — so `eng_ent_get(x).flags & EF_WIZARD` on a missing row reads
**uninitialized stack memory**. `objects_init` (`object.c:216-222`) seeds a row
per entity at boot, so it *reads* as reliably-false, but it is UB sitting in the
authorization path. Deleting the flag removes it; the `memset` goes in regardless.

---

## 3. Piece 1 — pure deletion (behavior-identical, gated)

`st_is_wiz` is always false, so `!st_can(p,id,plen) && !st_is_wiz(p)` is
**≡ `!st_can(p,id,plen)`**. Deleting the clause cannot change behavior.

**Do not "upgrade" these five to `st_can_region`.** That would hand the cosmos
ruler authority over every planet's `loadmod`/`unloadmod`/`release` — a real
loosening that is **not** in the decision. Delete only.

| File:line | Site | Change |
|---|---|---|
| `include/uapi/object.h:40` | `EF_WIZARD = 8` | delete enumerator |
| `include/nd/xy-types.h:104` | `EF_WIZARD = 8` | delete (modules compile against this copy; the two were verified byte-identical) |
| `src/spacetime.c:1487` | `st_is_wiz` | delete function |
| `src/spacetime.c:1510` | inside `st_can_region` | delete `if (st_is_wiz(...)) return 1;` → becomes `st_can_region` ≡ `st_can(...) \|\| st_can(p, 0, ST_PLEN_ROOT)`, i.e. `do_room`/`do_deny` **keep exactly today's reach** |
| `src/spacetime.c:1620` | `do_planet` reclaim | `... && rec.owner != NOTHING && !st_is_wiz(p)` → drop clause |
| `src/spacetime.c:1631` | `do_planet` new claim | `!have && !st_can(p,0,ROOT) && !st_is_wiz(p)` → drop clause |
| `src/spacetime.c:1929` | `do_loadmod` | `!st_can(...) && !st_is_wiz(...)` → `!st_can(...)` |
| `src/spacetime.c:1985` | `do_unloadmod` | same |
| `src/spacetime.c:2059` | `do_release` | same |
| `src/entity.c:27-30` | `eng_ent_get` | `memset(&ent, 0, sizeof(ent));` before the `if` |
| `src/spacetime.c:1470-1474`, `1499-1504` | comments | rewrite: the "or EF_WIZARD" / "wizard override" rationale is now false |

**Gate for piece 1:** `make` clean (0 errors, 0 warnings) and the existing
`./test` green. Green here is the *proof* that the wizard reads were inert —
that evidence is worth more than any argument in §2. Do not proceed until you
have it.

---

## 4. New API (`include/st.h`)

Four functions. All pure additions except `st_can_region`, which stops being
`static`.

```c
/* un-static: the nine sites live in five other files */
int st_can_region(unsigned player_ref, uint64_t id, uint8_t plen);

/* The prefix-containment identity xy.h documents for xy_region_at:
 * (A,plen_A) covers (B,plen_B) iff plen_A <= plen_B and masking B to plen_A
 * yields A. Pure arithmetic, no tree walk, no ancestor-chain API. */
int st_region_covers(uint64_t o_id, uint8_t o_plen, uint64_t i_id, uint8_t i_plen);

/* Is (id,plen) inside the actor's scope: the selected region + its subtree,
 * or (no selection) the union of every region they rule? */
int st_in_scope(unsigned actor, uint64_t id, uint8_t plen);

/* Region of an arbitrary object: walk containment to the first TYPE_ROOM. */
int st_region_of_obj(unsigned ref, uint64_t *id, uint8_t *plen);
```

**`st_can_region` becomes one loop** over candidate ancestor widths
`{0, 16, 32, 48, 64}`, skipping `w > plen`: true if `st_owner(id & mask(w), w)
== player`. That subsumes exact-owner (`w == plen`) and the cosmos clause
(`w == 0`) with no special cases. Depth is 2 today — root `(0,0)` and planets
`(N<<48, 16)`; cells have **no** rows, so `xy_region_at` returns the planet and
"a ruler above it" is already automatic. The loop exists so the decision's
wording stays true if a row ever appears at plen 32/48; without it
`st_can_region` would silently stop seeing the ancestor owner.

**`st_region_of_obj`** walks `what.location` until it hits a `TYPE_ROOM`
(`w_hd` only ever holds rooms — the sole writer is `map_put` from
`st_room_at`), depth-capped at 8 against entity cycles, stopping at ref 0 /
`NOTHING`.

**REFINEMENT (2026-10-04, found during implementation): an unmapped room
resolves to the void, (0,0,0,0), which is in the cosmos — it is *not*
`NOTFOUND`.** §4 first drafted this the other way, citing §27.1's "an unmapped
room is not world 0". That reasoning does not transfer, for two reasons:

1. §27.1 asks a different question. It is about *event anchoring* — "does this
   event have a specific anchor?" — and its answer for an unmapped room is "no",
   which falls through to a **global dispatch from the root**. The root is the
   cosmos. So §27.1's own fallback puts the void at the cosmos, and this function
   answers a second question ("whose authority covers this object") with that
   same region rather than inventing a different one. What §27.1 forbids is
   mistaking the void for a specific **planet**, which this does not do.
2. `NOTFOUND` would be a functional trap, not merely a conservatism. Players
   log in into an unmapped room. Treating that as unresolvable puts every
   not-yet-moved player under *nobody's* authority: `eng_controls` then refuses
   every command that resolves them by name, and a region ruler cannot teleport
   them out. The void would be a prison. It is part of the cosmos and the cosmos
   ruler governs it — which is also what makes the two-player tests in §9
   constructible at all.

`NOTFOUND` is reserved for a genuinely dead-end walk: ref 0 / `NOTHING`, a
location with no OBJ row, or no room within the depth cap. That is the
conservative direction — an object nobody can locate is an object nobody
controls.

**`st_in_scope`** is the default-scope engine. With an explicit selection it is
`st_region_covers`. Without one it is the union over the actor's rows — and note
the elegance: if the actor rules the cosmos, their root row `(0,0)` covers
everything, so "all my regions" *is* world-wide for them with no special case.

---

## 5. The seven dead commands

Template is `do_room` (`spacetime.c:1765-1777`): authorize the **target**
region (never the caller's location), name the ruler on refusal, `eng_nd_flush`
every path.

| # | Site | Gate |
|---|---|---|
| 1 | `do_wall` `speech.c:54` | selector: `wall world <n> <msg>` / `wall all <msg>` (reuse `st_cmd_region`, `spacetime.c:1577`). Deliver **only** to `TYPE_ENTITY` recipients whose region the selection covers; dedup by first-hit so a player matching two of my regions gets one copy. `argscat` swallows `argv[1..]`, which is why the selector needs an explicit keyword — `wall hello 3` must be one message, not world 3. **SUPERSEDED by CMD_REGION.md §5.4 (implemented 2026-10-05):** the `all`/`world <n>` keywords are gone, replaced by one bare-world-number-or-`cosmos` dialect shared with the other six commands. `wall` parses its own leading token (`argc`-testing was tried and reverted — axil inflates `argc`, so the "something follows" test is on the argv *string*), and a bare `wall` resolves through the shared `st_target_or_position()`. This row is the record as executed at the time; do not rewrite it. |
| 2 | `do_owned` `look.c:85` | no-arg form unchanged (self). `<name>` requires `st_can_region(me, region_of(victim))` **and** printed rows filtered to in-scope objects — otherwise a ruler enumerates world-wide ownership |
| 3 | `do_clone` `object.c:391` | region of the *cloned object* in scope, plus the existing `eng_controls` on the source |
| 4 | `do_create` `object.c:453` | creator's own region. It calls `eng_object_add(..., where_ref = player_ref, ...)`, so the object lands in their inventory — the scope is already honest, no placement question |
| 5 | `do_chown` `object.c:512` | region of the target object; keep the entity-vs-object rule, swap the `wizard` local for the region test |
| 6 | `do_ban` `wiz.c:71` | `ban <player> [world]` — unambiguous (name, then optional world). Model in §7 |

`st_cmd_region` already implements "explicit `<world>`, else the caller's own
region" and rejects non-numeric / `>65535` arguments. Reuse it verbatim; do not
invent a second selector dialect.

---

## 6. The two grants

### 6.1 `eng_controls` — four cases

```
1. who is not an entity            -> who = who.owner        (unchanged: puppets)
2. what.owner != ROOT/NOTHING && what.owner == who
                                  -> 1                      (ownership, incl. puppet)
3. what.owner is a player, != who:
     what.type == TYPE_ENTITY      -> st_can_region(who, region_of(what))
     otherwise                     -> 0
4. what.owner is ROOT or NOTHING  -> st_can_region(who, region_of(what))
```

Three facts make this the only correct shape:

- **`ROOT` is ref 1** (`object.h:7`) and ref 1 **is** the first player. That is
  the entire reason the old `what.owner == ROOT && who_ref == ROOT → 0` guard
  and the wizard clause both existed. Case 4 replaces both: ROOT-sentinel
  ownership stops counting as ownership, so rulership decides. Rooms are
  ROOT-owned (`eng_object_add`, `object.c:113`), so `teleport` becomes usable
  and self-teleport works.
- **`eng_controls` gates absolute-name resolution** (`match.c:157`), so it is
  reached from `get`/`drop`/`examine`/`name`/`recycle`/every `ematch`, not just
  `teleport`. Without case 3's `TYPE_ENTITY` test, a ruler would resolve another
  player's sword by name and `get` it out of their inventory. That is the whole
  reason "ownership of any kind of player still applies" needs to be a *case*,
  not a footnote.
- **`do_teleport` is already boundary-safe**: it requires control of victim,
  destination **and** the victim's location (`wiz.c:47-49`), so all three must
  be in scope. A planet-1 ruler cannot drag anyone into planet 2; only the cosmos
  ruler crosses regions. No extra rule needed — say so in the commit message
  rather than adding one.

### 6.2 `eng_look_at`

```c
-if (loc_ref != player_ref && loc.type == TYPE_ENTITY && !(eplayer.flags & EF_WIZARD))
+if (loc_ref != player_ref && loc.type == TYPE_ENTITY &&
+    !st_can_region(player_ref, st_region_of_obj(loc_ref, &rid, &rplen)))
        return;
```

At login `eng_look_at(player_ref, NOTHING)` resolves to the player's *room*, so
the entity branch does not fire there (`world.c:958`).

---

## 7. Ban: new table, entry-point enforcement

Today `EF_BAN` is one persistent global bit checked **only at login**
(`world.c:925`), with no region storage and no in-session enforcement — and
`do_ban` writes it to the **banmer**, not the victim (`wiz.c:95-96`). So ban is a
feature, not a re-gate.

**Storage** — a separate table, as decided:

```c
struct ban_key {
        uint32_t player;
        uint64_t id;
        uint8_t  plen;
} __attribute__((packed));

ban_key_type = corm_reg(sizeof(struct ban_key));
ban_hd = corm_open(db, "ban", ban_key_type, CM_U32, 0xFFFF, CM_SORTED);
/* value = the banning ruler's ref, so unban can re-check authority */
```

`corm_reg` + fixed width is libxylem's own region-key pattern
(`libxylem-runtime.c:30-45`); corm has no Morton key type, only
`CM_STR`/`CM_U32`/`CM_PTR`/`CM_HNDL`. `CM_SORTED` bytewise order is
`(player, id, plen)`, so a `CM_RANGE` scan from the player's first key
enumerates exactly that player's bans — which is what `unban` needs.

**Enforcement** — one guard in `eng_enter` (`entity.c:42`), the single funnel
for all four arrival paths:

| Path | Call site |
|---|---|
| movement | `spacetime.c:575` |
| `teleport` | `wiz.c:53` |
| module / vim teleport | `spacetime.c:935` |
| `room` | `spacetime.c:1786`, `:1799` |

Probe the destination's morton code masked to each of the 5 widths; any hit
refuses the move, naming the region. Masking *is* the containment test, so no
region lookup is needed at all. Items teleported by a ruler bypass `eng_enter`
(`wiz.c:63`) — correct, only players are excluded.

**Login: entry-only enforcement** (login allowed; the ban bites on arrival).
"Excluded from region R" is meaningless for a login that spawns you in the void,
and a login refusal would turn a cosmos-wide ban into a permanent lockout. *If
you want the opposite, this is the one line to change — flag it in review rather
than deciding it silently.*

**`EF_BAN` removal + migration.** The table supersedes the bit, so remove it, and
add a **one-time boot migration**: any player carrying `EF_BAN` in an existing
store gets a root-wide `(0,0)` ban row. Without it, an upgrade silently drops
every ban ever issued — the kind of quiet data loss that surfaces months later as
"the ban I set in August doesn't hold".

### 7.1 Implementation status, and the store-truncation bug this work uncovered

Implemented as specified except the storage shape (below). `st_ban_*` API
(`st.h`, `spacetime.c`), `eng_enter` guard (`entity.c`), `do_ban`/`do_unban`
(`wiz.c`, `unban` registered in `world.c` `cmds[]`), `EF_BAN` deleted from
both headers (migration reads the retired bit through an `EF_BAN_LEGACY`
literal), login check removed (entry-only), one-time `st_ban_migrate()`
wired after `objects_init()`. S8 covers ban, arrival refusal without moving,
no-sideways-leak, unban restore, reboot persistence, and unban authority.

**Deviation — key shape.** The specified packed-struct key (`corm_reg` +
`CM_SORTED`) is gone; keys are zero-padded strings
`"PPPPPPPPPP:IIIIIIIIIIIIIIII:PP"` in a plain unsorted `CM_STR` table, so
lexicographic order is still `(player, id, plen)` order. Exact-key
put/get/del (all any caller uses) never needed the sort. The table opens at
boot with the other engine tables and is closed like every other table again
(see the bug below for why that is now safe).

#### The real blocker: `close_all()` truncated the store on every shutdown

This is NOT a ban bug and NOT a `mod_load_abort` bug. The failed-loadmod
correlation that pointed there was a coincidence of heap layout.

Mechanism, measured end to end on this tree:

1. libcorm saves every file-backed map from a **library destructor at process
   exit** (`corm.h:181`) -- that save runs no matter what the program does.
2. `close_all()` used to `corm_close()` every map *before* the process exited,
   and then called `corm_save()` itself (`if (!i) corm_save();`).
3. A save with the maps closed is not a no-op: it recomputes the store's size
   from what is left in corm's file cache and rewrites the file at that size.
   With every map closed, the cache holds the file and nothing else, so the
   walk emitted corm's **16-byte header** and truncated the store to it.

The damage shape varied with whatever was still open (16, 1184, 1600, 2000,
2400, 3200 bytes across runs), which is exactly why this read as the
long-standing nondeterministic "~1-in-3 SEGV flake" and why splitting it by
hand kept producing contradictory answers.

Symptom chain, all reproduced by hand and in-suite:

- `save` mid-run wrote a correct 8461-byte image, ban key and both players
  present. The very next SIGTERM rewrote the same file as 3200 bytes of seeds
  with no player, no region and no ban.
- The next boot restored nothing: `st_restore: region id=0x0 plen=0` and
  nothing else, players re-created from scratch.
- In-suite that is `FAIL: boot C lost planet 1's surviving module` (16-byte
  db) and `FAIL: S8 ban did not survive the reboot`.

Why the ban table appeared to be involved: before it existed, `close_all`
closed every map of the file, which removes the file from corm's cache
entirely -- so the trailing save found nothing to write and the destroy was
*silent and harmless*. The deliberately-leaked ban table kept the file in the
cache, so the same save now wrote -- the header -- and truncated the store.
The leak was accidentally load-bearing, which is also why "boot-opened +
never closed" looked like the recipe for ban persistence: those runs were
reading back a file that had been rewritten as header-plus-ban-table and
nothing else. The same reasoning retires the earlier `corm_save()`-before-
closes experiment: correct, but still one destructor save away from the same
truncation.

**Fix** (`src/world.c` `close_all`): save once, after every writer is done,
and **close nothing**. Every path into `close_all` ends in process exit --
`on_axil_exit()` on a clean shutdown, the `SIGSEGV` handler otherwise -- so
freeing maps there bought nothing and cost the store. Our save and libcorm's
exit save now both run with every map intact and write the same image.
`ban_hd` goes back to being closed with everything else.

Verification: the four-way shutdown matrix (`terM.sh` shape: no players, one
player, two players, ban) now shows `post-save` == `post-term` byte-for-byte
in all four cases; before the fix all four lost everything. A three-boot
planet A->B->C flow survives two SIGTERMs with the unload persisted (boot C
restores `libnd-wts` + `libnd-biome` and does NOT restore the unloaded
`libnd-stone`). Full `./test.sh` green, `axil-nd ok`.

#### Two real memory bugs found on the way (ASan, `-fsanitize=address`)

- `src/noise.c` `spread()`: the inner loop ran `p <= nx * CHUNK_M` while each
  iteration writes TWO `CHUNK_SIZE` lines, so the last step started one memcpy
  2432 bytes past the end of `chunks_bio` and overwrote the `bio` pointer and
  whatever globals the linker placed after it. `p < nx * CHUNK_M` makes the
  final write end exactly at the end of the row. Reached on every player
  login (`on_new_player` -> `st_teleport` -> `st_room_at` -> `noise_chunks`).
- `src/view.c` `biome_bg()`: `memcpy(ret, ansi_bg[bg], sizeof(ret))` copied
  16 bytes out of 6-byte literals like `"\033[40m"`, and indexed `ansi_bg[]`
  with an unvalidated `biome_skel->bg` from a row that a missing skel leaves
  uninitialized. Now: skel zero-initialized, colour validated against the
  eight slots, copy bounded by the source string.

Neither caused the truncation (corm state is not in this library's data
segment), but a 2.4 KB write past a global on every login is not something to
leave in place. Reached the corruption by building the engine with
`make CFLAGS-Linux="-fsanitize=address -fno-omit-frame-pointer"` and running
under `LD_PRELOAD=/usr/lib/x86_64-linux-gnu/libasan.so.8`; the flow is clean
now.

#### Killed theories (do not re-chase)

- `mod_load_abort` / failed-loadmod heap corruption: the manual A->B->C flow is
  green with and without the failed loadmod once `close_all` is fixed. The
  earlier "both are necessary" split was heap layout moving a *different* bug.
- Key shape (struct vs string), table open position, boot-open vs lazy-open,
  kill timing, unload presence, reboot presence: each was split-tested; all of
  them were measuring this one shutdown save.
- A corm "late database does not persist" bug: a standalone 40-line corm
  program that opens a second database into an existing file after a save,
  writes, saves and reopens finds both rows present. corm is fine.

#### Lab notes that will save hours

The ground truth is always a hex dump of the db file for exact key bytes
(planet id LE + module-name strings for regions; the full 30-char ban key for
bans) -- never infer file content from suite PASS/FAIL. Ban keys are
`"%010u:%016llx:%02u"`, so grepping for the player's *name* finds nothing:
look for `[0-9]{10}:[0-9a-f]{16}:[0-9]{2}`. Beware `pgrep -f` matching its own
command line, `pkill -f` matching the invoking shell (it kills your own shell
-- use PID-based `kill`), `exec 7<>` fds not surviving across tool calls
(reconnect per call), background launches that die silently (always verify
with a build-output line or a port probe), and `exec 7<>` inside a function
needing `eval`. Leftover daemons on fixed ports waste hours:
`pgrep -a -x axil` after every run.

## 8. Defects fixed in the same pass

Each is *armed* by opening the gate, which is why they belong here rather than in
a follow-up.

| Defect | Site | Note |
|---|---|---|
| Unchecked `argv` indexing | `look.c:88`, `object.c:394`, `object.c:514-516`, `wiz.c:73` | bare `owned`/`clone`/`chown`/`ban` are NULL-deref crashes. Unreachable until the gates open. **`do_teleport` DONE (`70fc37c`)** — and it was not merely a latent crash, see §14.5 |
| No `eng_nd_flush` on any path | all six commands | convention per `do_room`/`do_planet` (§5.3). `axil_flush` runs after every command (`libaxil.c:1136`), so for a *reachable* command this is **convention, not a hang fix** — do not claim otherwise in the commit. **`do_teleport` DONE (`70fc37c`), and there it was neither convention nor cosmetic**: it had no flush at all, and that is the entire reason the command looked mute (§14.5.3) |
| `do_ban` sets the flag on the **banmer** | `wiz.c:95-96` | `eng_ent_set(player_ref, &evictim)`. **DONE (`dd96947`)** — the ban model (§7) replaced the flag with a region-keyed row, so there is no flag to mis-set |
| `do_owned` silent fall-through | `look.c:88-94` | a non-ruler passing a name gets *their own* list plus "N objects found" — misleading, not a refusal. **DONE (`dd96947`)** — S7 leg 6 pins the named refusal |
| `do_wall` writes to rooms and items | `speech.c:71-76` | harmless today (`eng_nd_write` drops `fd < 0`) but semantically wrong. **DONE (`dd96947`)** — the delivery loop filters `TYPE_ENTITY` only |
| `eng_ent_get` returns uninitialized `ENT` | `entity.c:25-31` | UB in the authorization path (§2). **DONE (`dd96947`)** — the callee zeroes the struct on a miss |

**Deliberately out of scope:** the three `fprintf(stderr, ...)` debug artifacts in
the object path — `st_room_at` (`spacetime.c:503`), `eng_object_add`
(`object.c:117`), `eng_object_move` (`object.c:266`). They are noise on stderr
and two of them are the exact lines in the known stale-contents crash signature
(`eng_object_add 4 carrot -> move-to-NOTHING`). But that is the *other* open
item, and mixing the two changes would make the crash's provenance unreadable.

---

## 9. Test plan

`test.sh` (1575 lines) is the gate: raw-telnet fds, `ndcmd`/`ndwait`/`ndsettle`
against a cumulative transcript, `killaxil` (SIGTERM, deliberately — SEGV races
the world tick and truncates the store, §22.6).

**A second identity connects — ANSWERED, empirically.** `world.c:953` is a `WARN`
only, so any name may log in; `test.sh`'s single-connection shape was a fixture
choice, not a limitation. The S6 section is the proof: two fds, two logins, and a
guest placed by the owner. Use its recipe for every two-player assertion
(§14.4). Two harnesses now exist side by side — `ndcmd`/`ndwait`/`ndsettle`
against the cumulative `PLANET_TXT` for the single-connection sections, and S6's
`mpcmd`/`mpwait`/`mpclean` with **one transcript file per fd** for anything where
an *absence* has to be provable.

**RED→GREEN per site.** The owner half is the RED: today the owner *is* refused,
so it fails pre-fix. `do_owned`'s non-owner half is independently red (it prints
your list instead of refusing). `eng_controls` and `eng_look_at` are *grants* —
they need a ruler **and** a non-ruler in the same region to prove the boundary,
since an "owner was refused" assertion cannot fail.

**Ban** needs RED/GREEN at `eng_enter` on a cross-region move, a same-region
*negative* (the ban must not leak sideways), and a reboot check that the row
survived.

**Then:** the full `test.sh`, and the site suite `make test`
(`sh scripts/gc-picker-junk.sh .` first, `DENO_JOBS=4`).

---

## 10. Order of work

1. ~~Probe second-player login (§9).~~ **DONE** — a second identity connects.
2. ~~**Piece 1 in full** (§3) → `make` + `./test` green.~~ **DONE** — `bea2721`.
3. **The matcher blockers (§14)** — had to come before the gates, because §6.1's
   grant is observable *only* through `teleport`. **DONE** — `70fc37c`, S6.
4. ~~`st.h` primitives (§4).~~ **DONE** — `7d30179`.
5. **The seven command gates (§5)** + their named-ruler refusals. **DONE** —
   `dd96947`, S7.
6. **The two grants (§6)** — `eng_controls` four cases, `eng_look_at`. Note
   §14.3: `eng_look_at` must be reached with `look #<ref>`, not a name.
   **DONE** — `dd96947`, S7.
7. **Ban table + `eng_enter` guard + `unban` + boot migration (§7).**
   **DONE** — `dd96947`, S8.
8. Full `test.sh`, then site `make test`. **DONE** — `./test.sh` green 3x,
   site `DENO_JOBS=4 make test` green (`120 passed | 0 failed`).
9. ST.md: append the four decisions taken in review (§11) to §27.6(1) — they
   are **not recorded yet**; only the storage choice is settled. Update the §11
   open-work note. Commit the **submodule first**, then the superproject.
   **DONE** — ST.md §27.6(1) carries the review decisions; committed as
   `external/axil-nd@dd96947` + site `a3c3a52`.

**Commits so far, all with `./test.sh` green:** `bea2721` (drop `EF_WIZARD`),
`7d30179` (region primitives), `70fc37c` (matcher + `do_teleport`, S6).

---

## 11. Decisions taken in review (2026-10-04) — to be written into ST.md §27.6(1)

1. **`eng_payfor`**: drop the wizard clause entirely. Everyone pays from their
   own `value`. No region equivalent is meaningful.
2. **`eng_controls`**: four-case rule (§6.1). Ownership always applies — a
   player's property is never overridden by rulership, **except** that a region
   ruler may move a *player* standing inside their region. Rulership *is* the
   authority over unowned/ROOT-owned things (rooms, unclaimed items), which is
   what makes `teleport` usable.
3. **`eng_look_at`**: a ruler may inspect entities in their region.
4. **Ban storage**: a separate `ban` corm table keyed `(player, region id, plen)`,
   **not** a widened `struct st_rec`. Enforced at `eng_enter`; login allowed.
   `EF_BAN` removed with a one-time root-wide migration for existing rows.

### Still open

- Whether a ban should also refuse **login** (§7). Default: no.
- Whether freed flag bit 8 is deleted (planned) or reserved for a future
  global flag. Planned: deleted outright.

---

## 12. Hard rules (learned the painful way)

- `make` then `./test`. **`make test` only *bakes* `test.sh` into a `test`
  artifact — it does not run it.**
- Back up `var/nd/std.db` before destructive experiments; it is live user data
  and a poisoned store crashes the next boot.
- Never a broad kill loop (one once killed the agent host). Kill by verified
  PID; `pgrep -a -x axil` to check strays.
- Bound every long run with `timeout`, log to a file. Deno `Conn.read` blocks
  forever, so every test wait needs a deadline that **fails** rather than hangs.
- Verify red→green for every regression test; never trust a test that never
  failed.
- When a failure disagrees with your mechanism, **re-derive the mechanism**.
  Three "server bugs" in this tree turned out to be test bugs, all caught by hex
  dumps and byte counts — not by reasoning.
- No passwordless `sudo`: `/usr/lib/libaxil-nd.so` predates `0555d14` and cannot
  be refreshed. Validate against the **in-tree** build only (`test.sh` already
  binds in-tree `PATH`/`LD_LIBRARY_PATH`).
- Commit the submodule, then the superproject. Never commit scratch files —
  the working tree already carries untracked scratch (`ANALYSIS.md`, `probe`,
  `std.db`, `external/axil-nd-testprobe/`, …); leave it.

---

## 13. Implementation findings (2026-10-04) — corrections from probing the engine

Found by probing the real engine (`/tmp/opencode/probe3.sh`) rather than by
reading alone. Each of these changes the plan above; §1–§12 are as originally
drafted except where noted.

### 13.1 Object refs are NOT stable — never assert on one

The guest player was **ref 3** in one probe and **ref 4** in another against the
same build. `eng_object_add` interleaves NPC spawns, so a ref shifts with
whatever else the boot created. Every assertion in §9 must go through a name
(`player_get`) or a room coordinate (`here`), never a hardcoded number. Rooms
too: `room 0 0 0 1` produced `room 4` in one run and would produce something
else in another.

### 13.2 A bare command does not reliably have `argc < 2`

`create` with no arguments answered "You can't do that." — the
`if (argc < 2) Usage: ...` guard never fired, so axil delivered a bare command
with `argc >= 2` and an empty `argv[1]`. **Consequence for §8:** `argc < 2` is
not a usable guard on its own. Every newly reachable command must test
`!argv[1] || !*argv[1]`, which is what the `do_room` template already does. The
`argc < 2` checks that exist today are decoration.

### 13.3 Refusals are silent for five of the six commands — fix that first

Probed answers with the intermediate build:

| command | answer today |
|---|---|
| `wall <msg>` | `You can't do that.` |
| `create sword 1` | `You can't do that.` |
| `owned <name>` | **prints YOUR OWN object list**, then `7 objects found.` |
| `clone <name>` | *nothing at all* |
| `chown <name> me` | *nothing at all* |
| `ban <name>` | *nothing at all* |
| `owned` (bare) | the caller's own list — correct |

So the RED shape for `clone`/`chown`/`ban` would be "absence of output became
presence of output", which is a weak assertion: it passes for any implementation
that merely prints *something*.

**Fix, and it is the §5 template rule stated more firmly than §5 did:** every
refusal path must name the region and its ruler, exactly as `do_room` does
(`st_owner_name`). Then the RED→GREEN pair is a *message* change — `You can't do
that.` → `Only the ruler of region 0x… (world N, <name>) may <verb>.` — which
is a real behavioural difference and cannot pass by accident. `do_owned`'s
silent fall-through (13.4) is the same bug wearing a working disguise.

### 13.4 `do_owned`'s fall-through is confirmed live, not theoretical

`owned <guest>` as the owner listed the owner's own seven objects and reported
`7 objects found.` — the named branch is unreachable today, so the command
silently answers a different question than the one asked. Already in §8; now
measured.

### 13.5 Two things the probe cleared up

- **The void really is the cosmos.** The boot log prints
  `nd_scope_step: nd_scope: on_enter region id=0x0000000000000000 plen=0 ran=1`
  for a player standing in `void(#2)`. This is independent confirmation of the
  §4 REFINEMENT: the void resolves to region `(0,0)`, so the cosmos ruler keeps
  authority over an unmapped room and the two-player tests are constructible.
- **NPCs share the void.** `koifish(#7)` and `dolphin(#8)` were standing there.
  A `wall` assertion must not claim "only the guest received this" while testing
  in the void; scope the delivery assertions to the `room 0 0 0 N` rooms, which
  are empty when created.

### 13.6 One open question the probe raised — SETTLED, and it was a red herring

`teleport <guest> void` printed **nothing** — no `Teleported`, no refusal — and
this was recorded as "possibly `eng_controls` refusing silently; if a rule that
evaluates true is still silent, the bug is in `do_teleport`". **It was the
latter, but not for the reason guessed**: `do_teleport` was writing correctly all
along and never flushing its buffer, so *every* outcome looked identical to a
gate refusal. Resolved in `70fc37c`; see §14.5.

The generalisable lesson, and the reason this is kept: **a command that prints
nothing is not a command that did nothing.** Before concluding anything from
silence, confirm the output path actually flushes.

### 13.7 What is NOT constructible, stated plainly so no one wastes time

§9 wanted "a planet-1 ruler cannot act in planet 2". **That test cannot be
built.** There is no way to create a non-cosmos region owner:

- `do_planet` requires cosmos ownership for a *new* claim, and
- `do_release` **deletes** the row (`st_row_del`) rather than clearing its owner,
  so a released planet is simply unclaimed again and still needs the cosmos,
- and no `stchown` exists (st.h:89 refers to legacy commands that were dropped).

In a fresh store the first player is the seeded cosmos ruler (ref 1), and ref 1
is the only possible region owner. Since the cosmos row `(0,0)` covers the whole
address space, **the first player is world-wide and every other player is
powerless.** The authority boundary that *is* observable is therefore
"owner yes / guest no", and the *scope* boundary is observable only through
selector-driven delivery and enforcement:

- `wall world 1 <msg>` reaches a player in world 1 and not one in world 2.
- `wall all <msg>` and bare `wall <msg>` reach both.
- `ban <guest> 1` stops the guest entering world 1 while world 2 still works.

Those are the §27.6(1) requirements that matter and they are all testable. A
cross-planet refusal assertion would have been theatre.

---

## 14. RESOLVED: `do_teleport` moved a bystander, and killed the daemon

Found while building the §9 two-player gate, and **fixed in `70fc37c`**.
**It was never a teleport bug.** Three of the four causes are in the
matcher, and the fourth is that the command never flushed its output
buffer. `teleport #<valid ref> here` does now move its victim, and
`teleport #<absent ref>` refuses instead of aborting the daemon — both
covered by the new S6 section in `test.sh`.

The original symptom list below is kept verbatim because it is what sent
the search in the wrong direction twice: nothing was wrong with name
resolution, and nothing was wrong with `argv`. **A command that
"prints nothing" is not a command that did nothing** — see 14.5.

### 14.1 Symptom: `do_teleport` appeared to write nothing, ever

Every form tried, over two sockets, with drains long enough that a late reply
would have landed (three × 0.6 s plus a 2–3 s pump):

| form | answered |
|---|---|
| `teleport void` | *nothing* |
| `teleport void void` | *nothing* |
| `teleport <guest-name> void` | *nothing* |
| `teleport #<valid-ref>` | *nothing* |
| `teleport #<valid-ref> here` | *nothing* |
| `teleport zzz` | *nothing* |

Not `NOMATCH`, not `CANTDO_MESSAGE`, not `Teleported`. The daemon stays up and
the same socket keeps answering `here`/`status` afterwards, so the session is
intact. Every early-return path in `do_teleport` writes something, so silence
means one of:

1. it is never reached (but `teleport` is *recognised* — an unknown verb
   answers `I don't know what you mean.`, which is what bare `chown` does), or
2. `player_ref = eng_fd_player(fd)` is unusable and `nd_writef` goes nowhere
   (but the same lookup works for every other verb on the same fd), or
3. it reaches `default:` in the type switch, where the only statement is a
   silent `eng_object_move` — i.e. it moved something that did not visibly
   change. **Not excluded.** In one earlier probe the log showed
   `eng_object_move 6 dolphin -> 4` immediately after a teleport, which says the
   victim resolved to an **NPC**, not the intended player.

**Root cause, in hindsight — see 14.5.** All three hypotheses above were wrong.
It writes; it just never flushes. Meanwhile the dolphin observation in (3) was
the *real* bug all along, not a red herring.

### 14.2 Two concrete defects inside that, both real (both now fixed)

- **`char *arg2 = argv[2]` is dereferenced with no guard** (`wiz.c:17-18`), then
  `if (*arg2 == '\0')` (`wiz.c:22`). For a two-argument `teleport` this is a
  NULL dereference *unless* axil terminates `argv` with an empty string. It
  survives today, so axil must be doing the latter — but nothing states that,
  and §13.2 already proved axil's argument delivery is not what the code
  assumes elsewhere (`argc < 2` never fires). This is §8's `do_teleport` item.
- **`teleport #<out-of-range>` aborts the daemon** — measured, not inferred:
  `teleport #1823110` produced `Aborted` (SIGABRT: heap corruption, not
  SIGSEGV, which is why `signal(SIGSEGV, close_all)` does not catch it and the
  process really does die). The cause is visible in
  `eng_ematch_absolute` (`match.c:157`): `unsigned match = parse_unsigned(...)`
  then `if (match < 0 || ...)` — **the `< 0` test is dead on an unsigned**, so
  the only bound is `eng_obj_exists`, which does not reject a large ref. The
  caller then `corm_get_copy`s a row that does not exist. Any ref a player can
  type reaches this.

### 14.3 There is no name lookup in the command path at all

`eng_ematch_absolute` handles **only** `#<number>`. Every other form returns
`NOTHING`. This part of §14 is **still true** and still constrains §6.2:

- `teleport <room-name>` cannot work as written. Room names are auto-generated
  (`room 4 at 0 0 0 1`), and `eng_ematch_near`/`_mine` only match names *inside*
  the caller's own room or inventory.
- **`do_look_at` cannot resolve another player by name** (`entity.c:206-224`):
  its chain is absolute / here / me / near / mine — there is no
  `eng_ematch_player`. Probed: `look <guest>` prints the *caller's own* header
  and `look <owner>` prints the *guest's own*, i.e. both silently fall back to
  the player. **§6.2's `eng_look_at` test must use `look #<ref>`.**
- `player_get(name)` does exist and works — it is what `ban` uses. Player *names*
  are resolvable; player *objects* are not, through `ematch`.

### 14.4 What this means for §9, concretely — SETTLED

**S6 in `test.sh` is the worked answer.** Every two-player test needs the guest
standing somewhere while the owner is elsewhere, and the recipe that section
established is the one to reuse:

1. **The owner places the guest with `teleport #<guest-ref> here`** — which now
   works (§14.5). This is the only player-to-player move in the engine, so §6.1's
   `eng_controls` grant is observable only through it. The owner's own location
   comes from `room <x> <y> <z> <w>`, which creates-or-finds *and enters*
   (`do_room`, `spacetime.c:1946`).
2. **Parse every ref in-band from `status`.** `<name> (<ref>) type 1 ... at <ref>`
   gives both a ref and a location on the socket that owns them. Never `look`:
   a room's *name* is biome-derived, so a carved world room can render as
   `void(#4)` and a `look`-based assertion silently tests the void.
3. **Give each connection its own transcript file**, and check daemon liveness
   (`kill -0`) *before* asserting on a message, so a crash reports as a crash
   instead of as a missing line.
4. **Do not hardcode a ref.** A room carved after login can get a *lower* ref
   than a player (`room 1 0 0 1` returned ref 2 in one run, 3 in the next).
5. **Never hand-edit the database to place a player.** It proves nothing about
   the code under test.

Two placements that were considered and rejected, so nobody retries them:
`room` cannot move the *guest* (it authorises with `st_can_region` against the
target position, and the guest rules nothing — correct behaviour, useless as a
placement tool); and a probe module calling `nd.st_teleport` would need a
firing site it can invoke per-player, and the obvious one (`on_new_player`)
fires for the wrong subject.

### 14.5 Root cause: four defects, none of them where the symptom pointed

`70fc37c`. All four had to be fixed together; fixing any one alone leaves
`teleport #<ref> here` silently wrong.

1. **`eng_obj_exists()` was inverted** (`object.c:25`). It returned
   `corm_get(...) == NULL` — true for an **absent** row. This is the root cause
   and it is in the wrong function: its only internal caller is
   `eng_ematch_absolute()`, so *every* branch of that function behaved
   backwards. A real ref tested false and was discarded as `NOTHING`; an absent
   ref tested true and was returned as a match.

   That single sign error produced both headline symptoms. Valid-ref lookups
   failed, fell through to the contents scan, and hit defect 2. Absent refs
   "existed", reached `corm_get_copy` on a missing row, and aborted the daemon.

2. **`eng_ematch_at()` returned an unmatched object** (`match.c:171-188`). The
   loop assigned `tmp_ref` on *every* iteration and returned it unconditionally,
   so exhausting the scan without a match reported "the last object in the room"
   as a hit. That is the dolphin. It also reassigned `where_ref` from the
   iteration while `corm_iter` was still walking it (`where_ref` **is** the range
   key), and it leaked the iterator whenever the scan exhausted without a break
   (`corm_fin` ran only on the two `break` arms).

3. **`do_teleport()` never flushed** (`wiz.c`). `nd_writef` buffers per fd and
   the buffer is pushed by the *next* command, so every refusal and the victim's
   confirmation sat unflushed until the player typed again. **This, and only
   this, is why the command "wrote nothing"** — all six return paths now call
   `eng_nd_flush`, matching `do_room`.

4. **`argv[2]` was read unguarded** (`wiz.c:17-18`). axil does set
   `argv[argc] = ""` (`libaxil.c` `cmd_proc`), which is what has been carrying
   this, but that is an off-by-one nothing depended on. The guard is now on the
   string — `!argv[1] || !*argv[1]` — which is also the only form that works,
   because §13.2 measured `argc < 2` as never firing.

Two incidental findings worth keeping: `parse_unsigned` is unsigned and already
maps a bad parse to `NOTHING`, so the old `if (match < 0 || ...)` in
`eng_ematch_absolute` was dead code — removing it retires one `-Wtype-limits`
warning (33 → 32) and is a genuine simplification, not warning suppression. And
`do_room` prints `room <ref> at <x> <y> <z> <w>`, not `Teleported`; assert on its
own line.

### 14.6 Still true, and still worth writing down

`st_room_at` creates rooms with `exits = 0`, so **there is no `go` command and
no walkable adjacency** — movement is `teleport` and `room`, nothing else. Also
worth remembering for the harness: `here` prints a *region* banner
(`[id=0x... plen=0 owner=...]`), **not** the room you are standing in. `status`'s
`at <ref>` is the location assertion.
