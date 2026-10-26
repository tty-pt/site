# axil-nd-session

`nd-session` for [axil-nd](../axil-nd), ported from SIC to libxylem.

Manages connection lifecycle, player authentication, avatar selection, player persistence (`save`), and the in-game help/manual subsystem (`help` / `man`).

## Role & Subsystem

`nd-session` owns player sessions from initial handshake through disconnection. It formats the login MOTD, authenticates passwords, saves player records to disk, and executes the in-game documentation toolchain.

## In-Game Commands (`man-src/`)

| Command | Section | Description |
|---------|---------|-------------|
| `help` | 10 | Context-sensitive topic list and gameplay guide |
| `man` | 10 | Formatted manual page viewer for game commands and mechanics |
| `begin` | 10 | New player onboarding tour and introductory guide |
| `save` | 10 | Explicitly persist player state and inventory to the database |
| `avatar` | 10 | Choose or customize visual character representation |
| `connect` | 10 | Session reconnection and authentication entrypoint |
| `modules` | 10 | Guide to installing and developing `nd-*` game modules |
| `boot` | 10 | Disconnect an idle or misbehaving session (operator command) |
| `toad` | 10 | Transform/penalize a misbehaving entity (operator command) |

## Packaged Assets

- **Art (`art/`)**: Player avatar graphics and UI portraits.
- **Manuals (`man-src/`)**: Essential session commands formatted for `nroff -mandoc`.

## Install & Build

```sh
make
make install
```
Installs `lib/libnd-session.so`, `art/`, and `man/`.
