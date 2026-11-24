#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
unit_cx_transpile mods/common/ux/site_media.cx
bin="$UNIT_TMPDIR/site_media_test"

# shellcheck disable=SC2086
"$UNIT_CC" -Wall -Wextra -Werror \
	$UNIT_EXT_INCLUDES -I"$REPO_ROOT/mods/common" -I"$REPO_ROOT/mods/common/ux" \
	-o "$bin" "$REPO_ROOT/tests/unit/site_media_test.c" \
	$UNIT_EXT_LDFLAGS -lbud

LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$bin"
