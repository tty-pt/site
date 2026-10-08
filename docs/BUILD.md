# Build & rebuild — make, wasm, headers, chroot

How to build, rebuild, lint, format, and what bites you when you rebuild.
Read before touching any build files (`Makefile`, `build.mk`, module
`Makefile`s) or changing C/CSS that must reach a running server.

## Commands

```bash
make                      # build everything (native .so + wasm assets)
make dev                  # one-command dev server (auto-cleans stale axil, builds, starts)
make watch                # auto-rebuild + restart on :8080 with entr
make doctor               # verify toolchain, chroot, ports, and boundaries
make compile_commands.json# generate LSP compilation database for clangd / IDEs
make new-mod NAME=xyz DISPLAY="Xyz" # scaffold complete CRUD module in 1 second
make test-mod MOD=song    # run targeted test suite for a specific module
make test-fast            # run standalone unit + matrix + pages smoke tests
make test-e2e FILE=...    # run targeted e2e test with auto-server
make install-libs         # install libs + axil into $PREFIX (SUDO=doas/sudo)
make lint                 # clang-tidy (max 4 indent levels enforced)
make format               # clang-format on all .c/.h
make clean / make distclean
```

- `make lint`/`make format` cover `mods` + `external/libbud` only. Keep
  `external/libhyle` + `external/libhyle-bud` code tidy manually (tabs,
  ≤4 nest) — the site Makefile does not tidy them.
- Site `make` is GNU make; `build.mk:22` adds
  `-I$(REPO_ROOT)/external/axil/include -I$(REPO_ROOT)/external/libcorm/include -I$(REPO_ROOT)/external/libxylem/include -I$(REPO_ROOT)/external/libbud/include -I$(REPO_ROOT)/external/libhyle/include -I$(REPO_ROOT)/external/libhyle-source/include` (+ per-module `EXTRA_CFLAGS` for `hyle-bud` in `mods/index`, `mods/gig`, `mods/grp`).

## CRITICAL: stale system headers shadow the repo for native builds

`/usr/include/hyle-bud/hyle-bud.h` (OLD 6-param `hyle_bud_filter_field`) and
`/usr/include/bud/bud.h` (OLD, missing `filter_style`) are installed site
artifacts that shadow the repo headers for **native** module builds unless the
repo `-I` paths win (they now do — build.mk line 23). Consequences if broken:
ABI garbage in cross-.so calls, and compile errors for fields with more
initializers than the system `bud_field_desc_t`.

- Verify which header a TU resolves: `cc -H -E ... index.c | grep hyle-bud`.
- Fix: keep the repo include paths BEFORE system paths in native CFLAGS.
- NEVER edit `/usr/include` copies.
- `make lint` passes the same repo include paths so clang-tidy resolves the
  repo headers too (otherwise it reports "too many arguments, expected 6, have
  7"). clang-tidy errors on include-only `mods/*/ux/*.c` files are pre-existing
  noise (they are not standalone TUs).

## WASM rebuilds and dependencies

`build.mk:57` generates `.d` files for WASM via `-MM` and lists `LIST_UX_DEPS` in
each module `Makefile` as explicit fallback. The `$(WASM_PATH)/%.wasm` rule depends on
`$($*-src)` + `$(WASM_COMMON_SRC)` via `.d` includes, so editing `filter.c`/`site_ui.c`
rebuilds dependent WASMs automatically.

The `make` probe still skips wasm silently if no WASI clang is available
(silent 404 → pure SSR; harmless).

## Portable `make` / `bmake`

Site `make` is **GNU make** (Linux). `external/*` libs use
`$(HOME)/mk/portable.mk` which is BSD/`bmake` portable for `SYS`/`SO` detection.
`build.mk` itself is GNU (`ifeq`, `!=`, pattern rules); `bmake -C mods/core`
will error on `ifeq` — use `make` for the site, `bmake` only for the
external libs if needed.

`build.mk` keeps `all` as the first target so `make -C mods/core`
defaults to `all: dirs $(TARGET) $(WASM_TARGETS)`. `VERSION_GEN`
(`mods/common/ux/version.gen.h`) is generated only by `mods/common`
(`ux/version.gen.h: ...; sh ../../scripts/gen-asset-version.sh`) and
`site_page.c` uses `__has_include` fallback.

## Profiles and bootstrap

`PROFILE` (`dev` default, `release` → `-O2 -DNDEBUG`) in `Makefile:8` + `build.mk:22` (`CFLAGS` + `WASM_CFLAGS`). `make PROFILE=release`. Top-level `make all` bootstraps `external/stoma`, `hyle`, `hyle-source`, `bud`, `hyle-bud`, `axil`, `corm`, `libxylem` (`axil-lib`/`corm-lib`/`xylem-lib`) before `mods`. Deploy uses allowlist `Makefile:173 PROD_ASSETS`.

WASM allowlist `scripts/wasm-allowed-imports.lst` (`env.bud_host_*`) wired via `build.mk:37 WASM_LDFLAGS += -Wl,--allow-undefined-file=...` and enforced by `scripts/check-wasm-imports.sh` (blocking in `make all`).

## CSS cache bust

`mods/common/ux/version.gen.h` is content-hashed from `htdocs/styles.css` +
`hyle.css` + `bud-client.js` + `bud-hydrate.js` + `hyle-fragments.js` via
`scripts/gen-asset-version.sh` (separate hashes for CSS, client, and fragments). 
`mods/common/ux/site_page.c:108` includes it via `__has_include` fallback
(`SITE_CSS_V`/`SITE_CLIENT_V`/`SITE_FRAGMENTS_V`). Editing CSS/JS updates the hash on next
`make` (only `common.so` rebuilds); no manual `?v=` bump.

Source of truth for the hash: run `sh scripts/gen-asset-version.sh && cat mods/common/ux/version.gen.h`.

## Running the server

- Modules are dlopen'd on **both sides** of the chroot, split by the
  `on_axil_post_chroot()` hook (`external/axil/src/libaxil.c`, fired between
  `init_pre_bind()` and the first bind): the `-m` chain, engine deps,
  handlers and the DB open resolve from the **host** root pre-chroot; the
  engine's two file loaders — `st_init()` (persisted planet modules) and
  `nd_mods_load()` (the flat `external/axil-nd/mods.load` list) — resolve
  **inside the jail** post-chroot. DT_NEEDED of the post-chroot modules either
  reuses the already-mapped copy (libc, libxylem) or must live in a glibc
  default dir inside the jail (`usr/lib/libm.so.6` today) — do not chase them
  in `/etc/ld.so.cache` (a jail has none) or `/usr/local/lib` (not a loader
  default).
- Start: `axil -C /home/quirinpa/site -p 8080 -d -m mods/core/core` or `AUTH_SKIP_CONFIRM=1 make watch`. The
  `-m mods/core/core` flag is **required** — without it no handlers register.
- C frontend and module changes need module rebuild **+ server restart** to take effect. If `axil` is already running when `.so` files are recompiled, kill the existing process (`ps aux | grep axil`, `kill -9 <pid>`) so `dlopen` loads the new binary objects.
- When adding shared component libraries (like `libhyle-bud.so`), ensure native dependencies (e.g. `-ljson-c`) are listed in `LDLIBS` in `Makefile` so dynamic linking resolves all symbols cleanly upon startup (`ldd libhyle-bud.so`).
- Chroot prerequisites (`sh` + libs):

```bash
mkdir -p ./bin ./lib ./lib/x86_64-linux-gnu
cp /bin/sh ./bin/sh
# ldd /bin/sh → copy the libs it needs, typically libnss_files.so.2
```

## The `axil -C` chroot jail — seed it, then gate the restart on it

The split above means the jail must contain every module `st_init()` and
`nd_mods_load()` can name, at the paths `module_load_path()` walks inside the
jail (`/lib`, `/usr/lib`, `/usr/local/lib`). `scripts/jail-manifest.sh` is the
single source of truth (the manifest, and why); `sh scripts/seed-jail.sh
[JAIL]` copies it in; `sh scripts/check-jail.sh [JAIL]` verifies it, and is
the **hard pre-restart gate** — it also fails on a module shadowed by an
earlier search dir, on an unreachable post-chroot `DT_NEEDED`, and on a jail
that is not the site root.

Ladder (`make` builds the tree `.so` files; `install-libs` is what builds and
publishes the `nd-*` modules):

```bash
make
SUDO=doas make install-libs
make install-jail          # JAIL defaults to /var/www; seeds + checks
```

Invariants (enforced by `check-jail.sh`):

- **The jail IS the site root.** Every post-chroot path is site-root-relative,
  so `var/` and `htdocs/` are the same files before and after. A jail anywhere
  else boots an *empty* world silently (corm opens `var/nd/std.db` with
  `O_RDONLY` and simply finds nothing) and would save the world back into
  itself.
- **`AXIL_*` env under `-C` (root) must be site-root-relative**
  (`external/axil-nd/mods.load`, `var/nd/std.db`, `external/axil-nd/htdocs`,
  `external/axil-tty/htdocs`); an absolute path resolves to `$JAIL/<abs>` after
  the chroot and misses. Dev (`start.sh`, `run-with-server.sh`) uses absolute
  paths, which is fine because dev never runs as root.

## One authority: $PREFIX/lib — link, load, dev, prod

There is **no `-L` into the tree** (`build.mk`, module `Makefile`s — `sh
scripts/check-no-tree-rpath.sh`, wired into `boundary-check`, fails the build
if one appears or if a shipped `.so` records a tree `DT_RUNPATH`), and **no
`LD_LIBRARY_PATH`** in `start.sh` or `scripts/run-with-server.sh`. Modules link
plain `-lhyle-bud -ltransp …` against the default system search path;
`external/mk/portable.mk:16-29` picks that directory (`/usr` on Linux,
`/usr/local` on OpenBSD), and `make install-libs` asks each package's own
`install` target (from `external/mk/include.mk`) to publish there — libs, the
bare-soname aliases `xy_load()` addresses, headers and the `axil` binary.

So the file the link inspects, the file the loader maps, the file
`xy_load("libaxil-auth")` fetches, and the file dev and prod run are one and
the same file.

Why the rule exists: a tree copy reachable at link or load time plus the same
soname fetched from the system maps two distinct files as two objects, only one
of which gets the xy context bound into it — callers linked to the other
dispatch through a zero context, which took the daemon down on a *successful*
login while wrong-password 401s and every page still looked healthy.

Consequence: after rebuilding any runtime library, `SUDO=doas make install-libs`
before restarting axil. A link failure with undefined symbols means the
installed copy is older than the tree — install it, don't add paths back.
`scripts/rebuild-install.sh` does a clean rebuild + install in one pass and
delegates its install to the same target.

## Rebuild checklist after a code change

1. `make` (recompiles affected `.so`; WASM `.d` deps from `build.mk:57` rebuild WASMs automatically when `filter.c`/`site_ui.c` changes; CSS/JS hash auto-regens `mods/common/ux/version.gen.h` via `scripts/gen-asset-version.sh`).
2. If any `external/*/lib` library changed: `SUDO=doas make install-libs`.
3. Restart `axil` (see above; add `AUTH_SKIP_CONFIRM=1` if e2e will run).
4. Verify: `sh scripts/check-module-boundaries.sh && sh scripts/check-ux-purity.sh && sh scripts/check-wasm-imports.sh && sh scripts/check-no-tree-rpath.sh` (wired into `make all` and `make test`).

## Related docs

- `docs/STYLING.md` — CSS source of truth + cache-bust procedure.
- `docs/TESTING.md` — test commands + server prereqs.
- `docs/ARCHITECTURE.md` — module load order, chroot model.
