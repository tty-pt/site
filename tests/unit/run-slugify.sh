#!/bin/sh
# run-slugify.sh — matrix driver for the tests/unit slugify harnesses.
#
# Builds and runs slugify_test.c, caller_contract_test.c and slug_fuzz.c
# under several (instrumentation x locale) combinations:
#
#   ASAN     clang -fsanitize=address
#   VALGRIND plain build + valgrind (DWARF bounds; catches the seq3A
#            single OOB write ASAN misses in some layouts)
#   LOCALES  LC_ALL unset, "C", "C.UTF-8" (C must never be pinned in
#            FTS tests; here we deliberately EXERCISE C to verify the
#            slug code paths work under a root-server C-locale too)
#
# Expected results BEFORE the mpfd_get fix:
#   slugify_test        PASS everywhere (correct-usage semantics)
#   caller_contract 1   ASAN trip (iconv over-read), valgrind over-read
#   caller_contract 2   ASAN trip (strlen over-read) when title[255]!=0
#   caller_contract 3A  ASAN trip (grp[128] redzone write)
#   caller_contract 3B  silent by design (write lands in addressable
#                       neighbor stack — matches live-box behaviour)
#   slug_fuzz mode 0    PASS (robustness)
#   slug_fuzz mode 1    ASAN trip (iconv over-read via mismatch)
#
# Usage: tests/unit/run-slugify.sh [build|run|all]   (default: all)
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
UNIT="$ROOT/tests/unit"
CC=${CC:-clang}
CFLAGS="-g -O0 -I $ROOT/external/axil/include"
ASAN_FLAGS="-fsanitize=address"
OUT="$ROOT/debug/tests"
mkdir -p "$OUT"

BIN_ASAN="$OUT/slugify_test_asan"
BIN_VG="$OUT/slugify_test_vg"
BIN_CALLER="$OUT/caller_contract_test"
BIN_FUZZ="$OUT/slug_fuzz"
VG_OPTS="--error-exitcode=42 --quiet"

step=${1:-all}

build()
{
	echo "== building slugify harnesses =="
	$CC $CFLAGS $ASAN_FLAGS -o "$BIN_ASAN" "$UNIT/slugify_test.c" || return 1
	$CC $CFLAGS            -o "$BIN_VG"   "$UNIT/slugify_test.c" || return 1
	$CC $CFLAGS $ASAN_FLAGS -o "$BIN_CALLER" "$UNIT/caller_contract_test.c" || return 1
	$CC $CFLAGS $ASAN_FLAGS -o "$BIN_FUZZ"  "$UNIT/slug_fuzz.c" || return 1
	echo "built: $BIN_ASAN $BIN_VG $BIN_CALLER $BIN_FUZZ"
}

run_locale()
{
	loc=$1
	shift
	echo "--- locale: ${loc:-<unset>} ---"
	if [ -n "$loc" ]; then
		LC_ALL="$loc" LANG="$loc" "$@"
	else
		env -u LC_ALL -u LANG "$@"
	fi
}

run_slugify()
{
	for loc in C C.UTF-8 ""; do
		run_locale "$loc" "$BIN_ASAN"
		run_locale "$loc" valgrind $VG_OPTS "$BIN_VG"
	done
}

run_caller()
{
	for loc in C C.UTF-8 ""; do
		run_locale "$loc" env ASAN_OPTIONS=symbolize=0 "$BIN_CALLER" 1
		run_locale "$loc" env ASAN_OPTIONS=symbolize=0 "$BIN_CALLER" 2
		run_locale "$loc" env ASAN_OPTIONS=symbolize=0 "$BIN_CALLER" 3
	done
}

run_fuzz()
{
	for loc in C C.UTF-8 ""; do
		run_locale "$loc" env ASAN_OPTIONS=symbolize=0 "$BIN_FUZZ" 0 2000 1
		run_locale "$loc" env ASAN_OPTIONS=symbolize=0 "$BIN_FUZZ" 1 2000 1
	done
}

case "$step" in
	build) build ;;
	run)
		run_slugify
		run_caller
		run_fuzz
		;;
	*)
		build || exit 1
		run_slugify
		run_caller
		run_fuzz
		;;
esac
echo "== run-slugify.sh done =="
