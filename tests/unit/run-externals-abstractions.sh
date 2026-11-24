#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
bin="$UNIT_TMPDIR/externals_abstractions_test"

# shellcheck disable=SC2086
"$UNIT_CC" -Wall -Wextra -Werror \
	$UNIT_EXT_INCLUDES -I"$REPO_ROOT/mods/common" -I"$REPO_ROOT/mods/source" -I"$REPO_ROOT" \
	-o "$bin" "$REPO_ROOT/tests/unit/externals_abstractions_test.c" \
	$UNIT_EXT_LDFLAGS \
	-lhyle-bud -lhyle-source -lhyle -lbud -lstele -lcorm -laxil -laxil-auth -lxylem

LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$bin"
