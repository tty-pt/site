# Build & rebuild — make, wasm, headers, chroot

How to build, rebuild, lint, format, and what bites you when you rebuild.
Read before touching any build files (`Makefile`, `build.mk`, module
`Makefile`s) or changing C/CSS that must reach a running server.

## Commands

```bash
make                      # build everything (native .so + wasm assets)
AUTH_SKIP_CONFIRM=1 make watch   # auto-rebuild + restart on :8080
make lint                 # clang-tidy (max 4 indent levels enforced)
make format               # clang-format on all .c/.h
make clean / make distclean
```

- `make lint`/`make format` cover `mods` + `external/bud` only. Keep
  `external/hyle` + `external/hyle/c/libhyle-bud` code tidy manually (tabs,
  ≤4 nest) — the site Makefile does not tidy them.
- Native CFLAGS are extended by `build.mk` line 23 with
  `-I$(REPO_ROOT)/external/bud/include -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include`.

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

## CRITICAL: the wasm rule has NO prerequisites — force rebuilds

The `$(WASM_PATH)/%.wasm` rule in `build.mk` has **no prerequisites**: a
`.wasm` only rebuilds when the target file is missing. Editing C sources that
compile into a wasm (e.g. `external/hyle/c/libhyle-bud/src/filter.c` →
`htdocs/list.wasm`) and re-running `make` **silently ships the stale wasm**.

Always force the rebuild:

```bash
rm -f htdocs/<target>.wasm && make
```

The `make` probe skips wasm silently if no WASI clang is available (silent 404
→ pure SSR; harmless).

## CSS cache bust

Any change to `htdocs/hyle.css` / `htdocs/styles.css` (or the CSS source, see
`docs/STYLING.md`) requires bumping the `?v=` query on both stylesheet links in
`mods/common/ux/site_ui.c` (two occurrences, both `site_ui_page` paths), then
rebuild + restart. Forget this and browsers serve stale CSS.

## Running the server

- Modules are dlopen'd **before** the chroot; DT_NEEDED deps resolve from the
  host root — do not chase missing libs inside the chroot.
- Start: `axil -C /home/quirinpa/site -p 8080 -d -m mods/core/core`. The
  `-m mods/core/core` flag is **required** — without it no handlers register.
- C frontend changes need module rebuild **+ server restart** to take effect.
- Chroot prerequisites (`sh` + libs):

```bash
mkdir -p ./bin ./lib ./lib/x86_64-linux-gnu
cp /bin/sh ./bin/sh
# ldd /bin/sh → copy the libs it needs, typically libnss_files.so.2
```

## Rebuild checklist after a code change

1. `rm -f htdocs/<changed>.wasm` if the change compiles into a wasm.
2. `make` (recompiles the affected `.so`; wasm too if forced above).
3. Restart `axil` (see above; add `AUTH_SKIP_CONFIRM=1` if e2e will run).
4. Bump `?v=` in site_ui.c if CSS changed, BEFORE the rebuild in step 2.

## Related docs

- `docs/STYLING.md` — CSS source of truth + cache-bust procedure.
- `docs/TESTING.md` — test commands + server prereqs.
- `docs/ARCHITECTURE.md` — module load order, chroot model.
