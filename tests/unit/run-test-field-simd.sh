#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
BIN="$UNIT_TMPDIR/test_field_simd"
# shellcheck disable=SC2086
"$UNIT_GCC" -Wall -Wextra -Werror -O2 \
	-I"$REPO_ROOT/external/nd-world/include" \
	-I"$REPO_ROOT/external/libchaff/include" \
	-I"$REPO_ROOT/external/libcorolla/include" \
	"$REPO_ROOT/tests/unit/test_field_simd.c" \
	-lxxhash -lm -o "$BIN"
"$BIN"

if command -v clang-18 >/dev/null 2>&1 && command -v node >/dev/null 2>&1; then
	WASM="$UNIT_TMPDIR/test_field_simd.wasm"
	clang-18 --target=wasm32-wasi -O3 -msimd128 -DXXH_NO_XXH3 \
		-Wall -Wextra -Werror \
		-I"$REPO_ROOT/external/nd-world/include" \
		-I"$REPO_ROOT/external/libchaff/include" \
		-I"$REPO_ROOT/external/libcorolla/include" \
		"$REPO_ROOT/tests/unit/test_field_simd.c" \
		-lm -o "$WASM"
	node --no-warnings -e '
		const fs = require("fs");
		const { WASI } = require("node:wasi");
		const wasi = new WASI({ version: "preview1" });
		WebAssembly.instantiate(fs.readFileSync(process.argv[1]), wasi.getImportObject())
			.then(({ instance }) => wasi.start(instance));
	' "$WASM"
fi
