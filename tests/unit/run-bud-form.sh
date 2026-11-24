#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
bin="$UNIT_TMPDIR/bud_form_test"

# shellcheck disable=SC2086
"$UNIT_CC" -Wall -Wextra -Werror \
	$UNIT_EXT_INCLUDES \
	-o "$bin" "$REPO_ROOT/tests/unit/bud_form_test.c" \
	$UNIT_EXT_LDFLAGS \
	-lhyle-bud -lhyle-source -lhyle -lbud -lcorm -llemma -ljson-c

LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$bin"
