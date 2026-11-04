#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/index_helpers_test

${CC:-clang} -Wall -Wextra -Werror \
	-I"$repo/external/libhyle/include" \
	-I"$repo/external/libhyle-source/include" \
	-I"$repo/external/libhyle-bud/include" \
	-I"$repo/external/libbud/include" \
	-I"$repo/external/axil/include" \
	-I"$repo/external/axil-auth/include" \
	-I"$repo/external/libstele/include" \
	-I"$repo/external/libcorm/include" \
	-I"$repo/external/libxylem/include" \
	-I"$repo/mods/common" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/index_helpers_test.c" \
	-L"$repo/mods/index" -Wl,-rpath,"$repo/mods/index" -l:index.so \
	-L"$repo/mods/common" -Wl,-rpath,"$repo/mods/common" -l:common.so \
	-L"$repo/external/axil/lib" -Wl,-rpath,"$repo/external/axil/lib" \
	-L"$repo/external/axil-auth/lib" -Wl,-rpath,"$repo/external/axil-auth/lib" \
	-L"$repo/external/libstele/lib" -Wl,-rpath,"$repo/external/libstele/lib" \
	-L"$repo/external/libcorm/lib" -Wl,-rpath,"$repo/external/libcorm/lib" \
	-L"$repo/external/libxylem/lib" -Wl,-rpath,"$repo/external/libxylem/lib" \
	-L"$repo/external/liblemma/lib" -Wl,-rpath,"$repo/external/liblemma/lib" \
	-lhyle-bud \
	-lhyle-source \
	-lhyle \
	-lbud \
	-lstele \
	-lcorm \
	-laxil \
	-laxil-auth \
	-lxylem \

LD_LIBRARY_PATH="$repo/external/axil-auth/lib:$repo/external/axil/lib:$repo/external/libstele/lib:$repo/external/libcorm/lib:$repo/external/libxylem/lib:$repo/external/liblemma/lib:${LD_LIBRARY_PATH:-}" "$bin"
