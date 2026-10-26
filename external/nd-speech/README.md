# axil-nd-speech

`nd-speech` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Provides communication mechanics, player poses/emotes, global broadcast channels (`wall`), and object inspection (`examine` / `inventory`).

## Role & Subsystem

`nd-speech` handles verbal and non-verbal interactions between players in the same room or across zones.

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `say` | 10 | Speak to everyone in the current room |
| `pose` | 10 | Emote an action or expression (`:waves hello`) |
| `wall` | 10 | Broadcast an urgent announcement to all players |
| `inventory` | 10 | List carried items and equipment summary |
| `examine` | 10 | Detailed inspection of an entity, item, or room feature |
| `owned` | 10 | List objects owned by the player in the current area |

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-speech.so` and `man-src/` manual pages.
