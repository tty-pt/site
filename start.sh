#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-8080}

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
    echo "Starting ndc with AUTH_SKIP_CONFIRM=$AUTH_SKIP_CONFIRM"
fi

exec ndc -C "$SCRIPT_DIR" -p "$PORT" -d -m mods/core/core >> "$LOG_OUT" 2>&1
# gdb --args ndc -C "$SCRIPT_DIR" -p 8080 -d -m mods/core/core
