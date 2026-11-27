#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

unit_tmpdir
# shellcheck disable=SC2086
unit_build_archive libnd_seven_ffi_test.a "$UNIT_GCC" "-O1 -DLAM_WEBGL_NATIVE $ND_UX_INCLUDES" -- $ND_SEVEN_SRCS
_bin="$UNIT_TMPDIR/test_seven_gl_ffi"
# shellcheck disable=SC2086
"$UNIT_GCC" -Wall -Wextra -Werror -O1 -DLAM_WEBGL_NATIVE $ND_UX_INCLUDES \
	-o "$_bin" "$REPO_ROOT/tests/unit/test_seven_gl_ffi.c" "$UNIT_ARCHIVE" \
	$UNIT_EXT_LDFLAGS -lxylem -lstele -lxxhash -lm -lpthread
LD_LIBRARY_PATH="$UNIT_LD_LIBRARY_PATH" "$_bin"
