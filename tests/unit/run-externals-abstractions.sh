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
	-I"$repo/external/libqsys/include" \
	-I"$repo/external/libcorm/include" \
	-I"$repo/external/libxylem/include" \
	-I"$repo/mods/common" \
	-I"$repo/mods/source" \
	-I"$repo" \
	-o "$bin" \
	"$repo/tests/unit/externals_abstractions_test.c" \
	-L"$repo/external/libhyle-bud/lib" -lhyle-bud \
	-L"$repo/external/libhyle-source/lib" -lhyle-source \
	-L"$repo/external/libhyle/lib" -lhyle \
	-L"$repo/external/libbud/lib" -lbud \
	-L"$repo/external/libqsys/lib" -lqsys \
	-L"$repo/external/libcorm/lib" -lcorm \
	-L"$repo/external/axil/lib" -laxil \
	-L"$repo/external/axil-auth/lib" -laxil-auth \
	-L"$repo/external/libxylem/lib" -lxylem \
	-Wl,-rpath,"$repo/external/libhyle-bud/lib" \
	-Wl,-rpath,"$repo/external/libhyle-source/lib" \
	-Wl,-rpath,"$repo/external/libhyle/lib" \
	-Wl,-rpath,"$repo/external/libbud/lib" \
	-Wl,-rpath,"$repo/external/libqsys/lib" \
	-Wl,-rpath,"$repo/external/libcorm/lib" \
	-Wl,-rpath,"$repo/external/axil/lib" \
	-Wl,-rpath,"$repo/external/axil-auth/lib" \
	-Wl,-rpath,"$repo/external/libxylem/lib"

"$bin"
