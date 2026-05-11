#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-8080}
export LD_LIBRARY_PATH="/home/quirinpa/site/external/axil/lib:/home/quirinpa/site/external/hyle/lib:/home/quirinpa/site/external/hyle/c/libhyle-bud/lib:/home/quirinpa/site/external/bud/lib:/home/quirinpa/site/external/qmap/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

if [ -n "$DEBUG" ]; then
    LOG_OUT="/dev/stdout"
else
    LOG_OUT="/tmp/site.log"
fi

if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
	echo "Port $PORT is already in use. Stop the existing server or run PORT=<port> make run." >&2
	exit 1
fi

# Check for AUTH_SKIP_CONFIRM environment variable
if [ -n "$AUTH_SKIP_CONFIRM" ]; then
    echo "Starting axil with AUTH_SKIP_CONFIRM=$AUTH_SKIP_CONFIRM"
fi

export AUTH_SKIP_CONFIRM=1
if test -z GDB; then
  gdb --args axil -C "$SCRIPT_DIR" -p 8080 -d -m mods/core/core
else
  axil -C "$SCRIPT_DIR" -p "$PORT" -d -m mods/core/core 2>&1 | tee "$LOG_OUT"
fi
