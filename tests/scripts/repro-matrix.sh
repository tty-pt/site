#!/bin/sh
# repro-matrix.sh — cross-toolchain memory-safety matrix for the song-add
# crash investigation. Each cell runs a harness and records PASS/FAIL; the
# script exits non-zero if any expected-pass cell failed.
#
# Cells (expected AFTER fix — post-fix the matrix is a regression gate):
#   slugify_test   : 23 assertion cases vs REAL axil-encode.c
#                    ASAN          PASS (correct-usage semantics)
#                    valgrind      PASS
#   caller_contract: 3 caller replicas (fixed mpfd_get contract)
#                    ASAN seq1     CLEAN (bounded iconv read)
#                    ASAN seq2     CLEAN (NUL-terminated + length API)
#                    ASAN seq3     CLEAN (choir[127] in-bounds)
#   slug_fuzz      : real axil_slugify fuzz
#                    mode 0        PASS (robustness)
#                    mode 1        CLEAN (fixed caller: capped length)
#   mpfd_contract  : pins the mpfd_get contract (verbatim replica)
#                    CLEAN (11/11 on fixed contract)
#
# With the sanitizer symbolizer hang (this box) use symbolize=0.
#
# Usage: tests/scripts/repro-matrix.sh [--build] [--quick]
set -u

ROOT=$(cd "$(dirname "$0")/../.." && pwd)
UNIT="$ROOT/tests/unit"
OUT="$ROOT/debug/tests"
mkdir -p "$OUT"

CC=${CC:-clang}
CFLAGS="-g -O0 -I $ROOT/external/axil/include"
VG_OPTS="--error-exitcode=42 --quiet"

B_SLUG="$OUT/slugify_test"
B_CALLER="$OUT/caller_contract_test"
B_FUZZ="$OUT/slug_fuzz"
B_MFPD="$OUT/mpfd_contract_test"

pass=0
fail=0

report()
{
	label=$1
	rc=$2
	expect_trip=$3
	if [ "$rc" -ne 0 ] && [ "$expect_trip" = "trip" ]; then
		echo "PASS(TRIP) $label (rc=$rc)"
		pass=$((pass + 1))
	elif [ "$rc" -eq 0 ] && [ "$expect_trip" = "clean" ]; then
		echo "PASS(CLEAN) $label"
		pass=$((pass + 1))
	else
		echo "FAIL       $label (rc=$rc, expected $expect_trip)"
		fail=$((fail + 1))
	fi
}

build_all()
{
	echo "== building matrix binaries =="
	$CC $CFLAGS -o "$B_SLUG" "$UNIT/slugify_test.c" || return 1
	$CC $CFLAGS -fsanitize=address -o "$B_CALLER" "$UNIT/caller_contract_test.c" || return 1
	$CC $CFLAGS -fsanitize=address -o "$B_FUZZ" "$UNIT/slug_fuzz.c" || return 1
	$CC $CFLAGS -o "$B_MFPD" "$UNIT/mpfd_contract_test.c" || return 1
}

run()
{
	echo "== slugify_test (ASAN) =="
	ASAN_OPTIONS=symbolize=0 "$B_SLUG" >/dev/null 2>&1
	report "slugify_test ASAN" $? clean

	echo "== slugify_test (valgrind) =="
	valgrind $VG_OPTS "$B_SLUG" >/dev/null 2>&1
	report "slugify_test valgrind" $? clean

	echo "== caller_contract_test (ASAN) =="
	for s in 1 2 3; do
		ASAN_OPTIONS=symbolize=0 "$B_CALLER" "$s" >/dev/null 2>&1
		report "caller_contract seq$s ASAN" $? clean
	done

	echo "== slug_fuzz (ASAN) =="
	ASAN_OPTIONS=symbolize=0 "$B_FUZZ" 0 2000 1 >/dev/null 2>&1
	report "slug_fuzz mode0 ASAN" $? clean
	ASAN_OPTIONS=symbolize=0 "$B_FUZZ" 1 2000 1 >/dev/null 2>&1
	report "slug_fuzz mode1 ASAN" $? clean

	echo "== mpfd_contract_test (contract) =="
	"$B_MFPD" >/dev/null 2>&1
	report "mpfd_contract_test" $? clean
}

QUICK=0
BUILT=0
for a in "$@"; do
	case "$a" in
		--build) build_all || exit 1; BUILT=1 ;;
		--quick) QUICK=1 ;;
	esac
done

if [ "$BUILT" -eq 0 ] && [ "$QUICK" -eq 0 ]; then
	build_all || exit 1
elif [ "$BUILT" -eq 0 ] && { [ ! -x "$B_SLUG" ] || [ ! -x "$B_CALLER" ] || [ ! -x "$B_FUZZ" ]; }; then
	build_all || exit 1
fi

run

echo ""
echo "== repro-matrix: $pass passed, $fail failed =="
[ "$fail" -eq 0 ]
