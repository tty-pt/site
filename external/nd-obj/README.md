# nd-obj — the Neverdark object model as a module

First-party bootstrap world module (Phase 2.4b): object model, entity,
`HD_*` tables, cloning. Owns `object.c`, `entity.c`, `item.c` (moved out of
`external/axil-nd/src/`).

Built lockstep with the engine against its source headers
(`-I../axil-nd/include`), not the installed `/usr/include/nd` (which lags).
Loaded at boot by the kernel (`xy_load("libnd-obj")` in `xy_install` — stem
includes the `lib` prefix, like `libaxil-auth`) into the
root region, alongside `axil-tty`.

Seeds: `item.c` (`select` command) moved first as the relocation proof;
`object.c`/`entity.c` (+ providers for the `object_*`/`ent_*` hooks) follow.
