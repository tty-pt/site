---
name: boundary-and-build
description: "Check cross-module boundaries and rebuild with chroot. Use after any .c/.h, Makefile, mod, CSS, or asset change. Triggers on: make, boundary, module, chroot, axil, :8080, site paths"
---

# Boundary-and-Build — Cross-Module Boundaries + Build & Restart

Wraps the 4 checks wired in `Makefile:144 boundary-check` plus the chroot restart that `make watch` hides when it stalls.

## 1. Boundary checks — 7 rules (`scripts/check-module-boundaries.sh` + delegates)

Run after any `mods/*` edit:

```bash
sh scripts/check-module-boundaries.sh   # exits 1 on violation — covers:
# 1) cross-module #include "*.c" allowlist only site_ui.c|list.c
# 2) var/ literals outside common_storage.c/source or source_setup
# 3) getpwuid ban (M06)
# 4) xy_load outside mods/*/*
# 5) M07 idx_select_fields_for / idx_display_name / strcmp("song|poem|gig|grp")
# 6) M05 hyle_source_put/del/register outside mods/source (499,575)
# 7) site literals "(poem|song|gig|grp)" in common|index (grandfather site_paths.c)
# delegates to check-wasm-imports.sh W06

sh scripts/check-no-site-specific-js.sh # htdocs/*.js must not contain gig|song|poem|grp|repertoire|transpose|sb-|chord
grep -rn 'bud' external/hyle/src/include/hyle  # must be 0
grep -rn 'qmap' external/bud/src               # must be 0
```

`check-ux-purity.sh` is warn-only (`exit 0` always) — run separately via `pure-c-isomorphic` skill for `mods/*/ux/*.c`.

## 2. Build + stale header + asset version

```bash
make -j4  # repo -I before /usr/include fixes hyle-bud.h 6-vs-7 param shadowing
sh scripts/gen-asset-version.sh && cat mods/common/ux/version.gen.h  # after htdocs/styles.css|hyle.css|bud-client.js edits
# if header shadowing fix missing:
rm -f build.mk.d && make compile_commands.json && grep -c '"-I"' compile_commands.json
```

Stale header trap `BUILD.md:11` — if `hyle-bud.h` 6-vs-7 mismatch, `rm` and rebuild.

## 3. Chroot restart & verify (`TESTING.md:72-86`)

```bash
ps aux | grep axil | grep -v grep
kill -9 <pid> || true
AUTH_SKIP_CONFIRM=1 ./start.sh  # axil -C . -p 8080 -d -m mods/core/core, chroot at repo root
sleep 1; curl -s -i http://localhost:8080/ | head -20
# LD_LIBRARY_PATH order: build.mk adds repo -I before /usr/include
```

Stale `.so` `dlopen` keeps old code until `kill -9` — `make watch` (`find *.c|*.h|Makefile|mods.load | entr -r sh -c "make; ./start.sh"`) handles `*.c` but not WASM staleness — see `pure-c-isomorphic`.

See `docs/ARCHITECTURE.md` §3-5 DAG, `docs/BUILD.md`, `docs/CONVENTIONS.md` XY, `scripts/doctor.sh`, `scripts/watch.sh`.
