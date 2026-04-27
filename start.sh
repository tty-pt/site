#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-8080}

if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
	echo "Port $PORT is already in use. Stop the existing server or run PORT=<port> make run." >&2
	exit 1
fi

exec ndc -C "$SCRIPT_DIR" -p "$PORT" -d -m mods/core/core >> /tmp/site.log 2>&1
# gdb --args ndc -C "$SCRIPT_DIR" -p 8080 -d -m mods/core/core
