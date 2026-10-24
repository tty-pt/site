# axil-nd-region

`nd-region` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Governs world regions, land ownership, administrative authority, and dynamic module hotloading (`loadmod` / `unloadmod`).

## Role & Subsystem

`nd-region` partitions the game universe into sovereign zones. Region owners can grant or deny building permissions, manage player bans, and dynamically attach or detach gameplay modules per region.

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `claim` | 10 | Claim an unowned room/area for your faction or player |
| `unclaim` | 10 | Relinquish ownership of a region |
| `grant` | 10 | Delegate building/administrative authority to another player |
| `deny` | 10 | Revoke granted permissions from a player |
| `ban` / `unban` | 10 | Prohibit or permit players from entering a region |
| `loadmod` | 10 | Dynamically load an `nd-*` module into the region (`xy_load`) |
| `unloadmod` | 10 | Detach a module from the region |
| `modlist` | 10 | List all active modules in the current region |
| `here` | 10 | Inspect region boundaries and current zone properties |
| `planets` / `planet` | 10 | Global celestial bodies and zone hierarchy |
| `target` | 10 | Select active region target for administrative operations |

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-region.so` and in-game man pages under `share/axil-nd/man/`.
