#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-8080}
export LD_LIBRARY_PATH="$SCRIPT_DIR/external/libxylem/lib:$SCRIPT_DIR/external/axil/lib:$SCRIPT_DIR/external/axil-auth/lib:$SCRIPT_DIR/external/axil-hyle/lib:$SCRIPT_DIR/external/hyle/lib:$SCRIPT_DIR/external/libtransp/lib:$SCRIPT_DIR/external/hyle/c/libhyle-bud/lib:$SCRIPT_DIR/external/hyle/c/libhyle-source/lib:$SCRIPT_DIR/external/bud/lib:$SCRIPT_DIR/external/libqmap/lib:$SCRIPT_DIR/external/stoma/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [ -n "$DEBUG" ]; then
    LOG_OUT="/dev/stdout"
else
    LOG_OUT="/tmp/site.log"
fi

if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
	echo "Port $PORT is already in use. Stop the existing server or run PORT=<port> make run." >&2
	exit 1
fi

# Check for AUTH_SKIP_CONFIRM environment variable or dev environment
if [ "$AUTH_ENV" = "prod" ]; then
    export AUTH_SKIP_CONFIRM=
else
    export AUTH_SKIP_CONFIRM=1
fi

if [ -n "$AUTH_SKIP_CONFIRM" ]; then
    echo "Starting axil with AUTH_SKIP_CONFIRM=$AUTH_SKIP_CONFIRM"
fi
AXIL_BIN="$SCRIPT_DIR/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
    AXIL_BIN="axil"
fi

if test ! -z "$GDB"; then
  gdb --args "$AXIL_BIN" -C "$SCRIPT_DIR" -p 8080 -d -m mods/core/core
else
  "$AXIL_BIN" -C "$SCRIPT_DIR" -p "$PORT" -d -m mods/core/core 2>&1 | tee "$LOG_OUT"
fi
