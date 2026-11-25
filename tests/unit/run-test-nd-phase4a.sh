#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"

TMPBIN=$(mktemp /tmp/test_nd_phase4a.XXXXXX)
trap 'rm -f "$TMPBIN"' EXIT HUP INT TERM

cc -O2 -Wall -Wextra -Werror -Iexternal/axil-nd/include -Iexternal/libcorm/include -Iexternal/axil/include -Iexternal/libxylem/include -o "$TMPBIN" tests/unit/test_nd_phase4a.c
"$TMPBIN"

# Verify libnd-mcp.so BSS is strictly < 1 MB (was 655 MB before Phase 4A)
BSS_BYTES=$(size external/nd-mcp/lib/libnd-mcp.so | awk 'NR==2 {print $3}')
echo "libnd-mcp.so BSS bytes: $BSS_BYTES"
if [ "$BSS_BYTES" -ge 1048576 ]; then
	echo "FAIL: libnd-mcp.so BSS ($BSS_BYTES) >= 1 MiB" >&2
	exit 1
fi
echo "PASS: libnd-mcp.so BSS reduced to $BSS_BYTES bytes (< 1 MiB)"
