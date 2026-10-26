# axil-nd-region

`nd-region` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Governs world regions, land ownership, administrative authority, and dynamic module hotloading (`loadmod` / `unloadmod`).

## Role & Subsystem

`nd-region` partitions the game universe into hierarchical 4D spacetime regions identified by the pair `(id, plen)`. Region owners can claim territories, grant or deny building permissions, manage player bans, and dynamically attach or detach gameplay modules per region (`loadmod` / `unloadmod`).

## Morton Key Prefix & Bitmask Arithmetic

Every region in `nd-region` is an arbitrary prefix of the 64-bit Morton spacetime keyspace. Bit depth controls spatial domain and modder authority:

| Prefix Width (`plen`) | High-Bit Mask | Spatial Scale / Scope | Hierarchy & Modder Governance |
|---|---|---|---|
| `plen = 0` | `0x0000000000000000` | Cosmos (Universe) | Root tier. Server administrator / engine core. Global laws & baseline modules. |
| `plen = 11` | `0xFFE0000000000000` | 32-Planet Super-Sector | Sovereign sector governor ($2^5 = 32$ worlds, covering worlds 0..31). Loads baseline gameplay suite once. |
| `plen = 16` | `0xFFFF000000000000` | Single Planet / World | Planetary ruler ($2^{16} = 65,536$ worlds total). Local planetary laws, biomes, and rules. |
| `plen = 24` | `0xFFFFFF0000000000` | Hemisphere / Continent | Continental governor. Regional mechanics, sub-factions. |
| `plen = 32` | `0xFFFFFFFF00000000` | District / Zone / Area | District magistrate / modder. Area-specific custom graphics (e.g. `liblamina` viewport). |
| `plen = 48` | `0xFFFFFFFFFFFF0000` | City / Settlement | Town mayor / settlement builder. Local vendors, town laws. |
| `plen = 64` | `0xFFFFFFFFFFFFFFFF` | Single Room / Cell | Room architect / individual player room. Custom room scripts and fixtures. |

**Hierarchical Modder Attribution**:
The outermost modder claims a coarse prefix (e.g. `11` bits for a 32-planet sector) and loads baseline modules once. All nested worlds automatically inherit and execute these modules via XY hierarchical dispatch. Inner modders claim narrower prefixes (`16`, `32`, `64`) to customize their local space. Ancestor `deny` rules strictly cascade downward.

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `region` | 10 | Claim an arbitrary Morton prefix region: `region <width> [here \| world <N> \| cosmos \| <x> <y> <z> <w>]` |
| `regions` | 10 | List all claimed regions in the universe |
| `planet` / `planets` | 10 | Convenience backward-compatible alias for `region 16 world <N>` and `regions` |
| `grant` | 10 | Delegate building/administrative authority over any prefix: `grant <player> [<width> <point>]` |
| `release` | 10 | Relinquish ownership of a region: `release [<world> \| <width> <point>]` |
| `deny` | 10 | Forbid a hook or module in a region and its subtree: `deny <hook\|module> <name> [<world> \| <width> <point>]` |
| `ban` / `unban` | 10 | Prohibit or permit players from entering a region |
| `loadmod` | 10 | Dynamically load an `nd-*` module into any region: `loadmod <module> [<world> \| <width> <point>]` |
| `unloadmod` | 10 | Detach a module from a region: `unloadmod <module> [<world> \| <width> <point>]` |
| `modlist` | 10 | List all active modules in a region: `modlist [<world> \| <width> <point>]` |
| `here` | 10 | Inspect region boundaries and current zone properties |
| `target` | 10 | Select default region target for administrative operations: `target [<world> \| cosmos \| here \| none \| <width> <point>]` |

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-region.so` and in-game man pages under `share/axil-nd/man/`.
