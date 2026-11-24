#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
bin="$UNIT_TMPDIR/index_helpers_test"

# shellcheck disable=SC2086
"$UNIT_CC" -Wall -Wextra -Werror \
	$UNIT_EXT_INCLUDES -I"$REPO_ROOT/mods/common" -I"$REPO_ROOT" \
	-o "$bin" "$REPO_ROOT/tests/unit/index_helpers_test.c" \
	-L"$REPO_ROOT/mods/index" -Wl,-rpath,"$REPO_ROOT/mods/index" -l:index.so \
	-L"$REPO_ROOT/mods/common" -Wl,-rpath,"$REPO_ROOT/mods/common" -l:common.so \
	$UNIT_EXT_LDFLAGS \
	-lhyle-bud -lhyle-source -lhyle -lbud -lstele -lcorm -laxil -laxil-auth -lxylem

LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$bin"
