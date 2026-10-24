# axil-nd-obj

`nd-obj` for [axil-nd](../axil-nd), ported from SIC to libxylem.

First-party bootstrap world module: core object model, entities, `HD_*` tables, object lifecycle, and item manipulation commands.

## Role & Subsystem

`nd-obj` manages the fundamental object hierarchy of NeverDark. Everything from carried inventory items to world fixtures, containers, and entity instances derives from `OBJ` and `ENT`.

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `get` | 10 | Pick up an item from the room or take from a container |
| `drop` | 10 | Place an item onto the floor of the current room |
| `put` | 10 | Store an item inside a container object |
| `give` | 10 | Transfer an item from your inventory to another player |
| `look` | 10 | Inspect an object, player, or ambient environment |
| `name` | 10 | Rename an object or entity you own |
| `chown` | 10 | Transfer ownership of an object to another player |
| `create` | 10 | Instantiate a new object or container |
| `clone` | 10 | Duplicate an existing object template |
| `recycle` | 10 | Destroy and reclaim database storage for an object |
| `diff` | 10 | Compare properties between object instances |
| `status` | 10 | Display metadata, flags, and storage properties |
| `select` | 10 | Set target object context for subsequent operations |

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-obj.so` and in-game man pages under `share/axil-nd/man/`.
