#!/bin/sh
set -eu

repo=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
tmpdir=$(mktemp -d)
trap 'rm -rf "$tmpdir"' EXIT HUP INT TERM
bin=$tmpdir/externals_abstractions_test

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
	-I"$repo/mods/source" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/externals_abstractions_test.c" \
	-L"$repo/external/axil/lib" -Wl,-rpath,"$repo/external/axil/lib" \
	-L"$repo/external/axil-auth/lib" -Wl,-rpath,"$repo/external/axil-auth/lib" \
	-L"$repo/external/libstele/lib" -Wl,-rpath,"$repo/external/libstele/lib" \
	-L"$repo/external/libcorm/lib" -Wl,-rpath,"$repo/external/libcorm/lib" \
	-L"$repo/external/libxylem/lib" -Wl,-rpath,"$repo/external/libxylem/lib" \
	-lhyle-bud \
	-lhyle-source \
	-lhyle \
	-lbud \
	-lstele \
	-lcorm \
	-laxil \
	-laxil-auth \
	-lxylem \

LD_LIBRARY_PATH="$repo/external/axil-auth/lib:$repo/external/axil/lib:$repo/external/libstele/lib:$repo/external/libcorm/lib:$repo/external/libxylem/lib:${LD_LIBRARY_PATH:-}" "$bin"
