#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
bin="$UNIT_TMPDIR/nd_ui_slot_test"

# shellcheck disable=SC2086
"$UNIT_CC" -g -Wall -Wextra \
	-I"$REPO_ROOT" $UNIT_EXT_INCLUDES \
	-o "$bin" "$REPO_ROOT/tests/unit/nd_ui_slot_test.c" \
	$UNIT_EXT_LDFLAGS -lbud -lxylem

LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$bin"
