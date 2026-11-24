#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
bin="$UNIT_TMPDIR/auth_group_permissions_test"

# shellcheck disable=SC2086
"$UNIT_CC" -Wall -Wextra \
	$UNIT_EXT_INCLUDES -I"$REPO_ROOT/mods" \
	-o "$bin" "$REPO_ROOT/tests/unit/auth_group_permissions_test.c" \
	$UNIT_EXT_LDFLAGS \
	-laxil -lcorm -lxylem -lbud -lhyle -lhyle-source -lhyle-bud -llemma -ljson-c -lstele

LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$bin"
