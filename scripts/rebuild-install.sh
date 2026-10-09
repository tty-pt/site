#!/bin/sh
# rebuild-install.sh -- one-vintage clean rebuild + privileged install.
#
# Why this exists: the production server dlopens its modules and libraries
# by soname at runtime. Rebuilding only *some* of them (or hand-copying
# files across the day) leaves mixed-vintage binaries in the process, and
# mixed ABIs fail in silent ways (refused module loads, dropped hooks,
# dead sessions). This script makes a mixed tree impossible: wipe every
# build artifact `make all` regenerates, rebuild the whole dependency chain
# in order in a single pass, then install libs + binary + module aliases
# atomically-ish (one privileged step, last).
#
# Phase 1 CLEAN removes only what `make all` regenerates:
#   external/*/*.o, *.o.d | external/*/lib/*.{so,a} (chain + nd engine mods)
#   | external/axil/bin/axil | mods/*/*.so
# NEVER touched: var/ etc/ users/ htdocs/ *.log *.core rust/ target/
#   node_modules/ tests/ .git/ (data, vendored output, fixtures).
#
# Phase 2 BUILD is a single sequential `make all` (prerequisite order is the
# dependency order; no -j on purpose) plus the nd-* engine modules, which
# `make all` does not cover but the server dlopens at boot.
#
# Phase 3 INSTALL asks each package to install itself into $PREFIX ({bin,lib},
# plus headers, bare-soname aliases and pkgconfig) through mk/include.mk's
# `install` target -- PREFIX defaults to the mk/portable.mk table (/usr on
# Linux, /usr/local on OpenBSD), so link and run resolve the same installed
# copies.
#
# Usage:
#   ./scripts/rebuild-install.sh
#   PREFIX=/tmp/stage ./scripts/rebuild-install.sh   # staged test, no root
#   SUDO=doas ./scripts/rebuild-install.sh           # OpenBSD
#   SUDO="sudo -n" ./scripts/rebuild-install.sh     # Linux
#
# shellcheck disable=SC2086  # ${SUDO} must stay unquoted (empty or "doas ...")
set -eu

REPO_ROOT=$(cd "$(dirname "$0")/.." && pwd)
# Same table mk/portable.mk uses when the packages install themselves.
PREFIX=${PREFIX:-$(make -s print-prefix)}
SUDO=${SUDO:-}
cd "$REPO_ROOT"

log() { printf '=== %s ===\n' "$*"; }
fail() { printf 'ERROR: %s\n' "$*" >&2; exit 1; }

# Modules whose lib/ outputs `make all` (transitively) regenerates, plus the
# nd-* engine modules, which region rows name by soname and st_init() dlopens
# at boot from inside the jail. Anything not listed here is never deleted and
# never installed by this script.
CHAIN_LIBS="libqsys libcorm libxylem libstoma libjoint libislet libsepal \
	libhyle libtransp libbud libhyle-bud libhyle-source \
	axil axil-auth axil-hyle axil-tty axil-nd"
ENGINE_MODS=""
for d in external/nd-*/; do
	[ -f "$d/Makefile" ] && ENGINE_MODS="$ENGINE_MODS $(basename "$d")"
done

log "provenance: $(git rev-parse HEAD 2>/dev/null || echo nogit)"
git status --short 2>/dev/null | grep -v '^??' | head -10 || true

log "phase 1/3: clean build artifacts (regenerable only)"
find external mods \( -name '*.o' -o -name '*.o.d' \) \
	-not -path '*/node_modules/*' -not -path '*/rust/*' \
	-not -path '*/target/*' -not -path '*/tests/*' -delete
for m in $CHAIN_LIBS $ENGINE_MODS; do
	[ -d "external/$m/lib" ] || continue
	rm -f "external/$m/lib/"*.so "external/$m/lib/"*.a
done
rm -f external/axil/bin/axil
for d in mods/*/; do
	rm -f "$d"*.so "$d"*.o "$d"*.o.d
done

log "phase 2/3: ordered rebuild (single make all, sequential)"
make all
if [ -n "$ENGINE_MODS" ]; then
	for m in $ENGINE_MODS; do
		log "engine module: $m"
		make -C "external/$m"
	done
fi

log "phase 3/3: install to $PREFIX (mk/include.mk per package)"
# One installer: every package's own `install` target owns what lands in
# $PREFIX/lib and $PREFIX/bin (libs, bare-soname aliases for xy_load(), the axil
# binary, headers), so a staging run and a privileged run cannot drift apart.
SUDO="$SUDO" PREFIX="$PREFIX" make install-libs
[ -x external/axil/bin/axil ] \
	|| fail "external/axil/bin/axil missing after build"
if [ "$(uname)" = "Linux" ] && command -v ldconfig >/dev/null 2>&1; then
	${SUDO} ldconfig || true
fi

log "installed manifest (single vintage check: one timestamp)"
${SUDO} ls -la "$PREFIX/bin/axil" "$PREFIX"/lib/libaxil*.so "$PREFIX"/lib/libxylem.so "$PREFIX"/lib/axil-*.so 2>/dev/null || true
log "done; restart the server and verify (wrong-password 401 page, login sticks)"
