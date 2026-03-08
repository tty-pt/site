#!/bin/sh
set -e

pkill -x ndc 2>/dev/null || true
pkill -x rust 2>/dev/null || true
pkill -f "deno.*server" 2>/dev/null || true

cleanup() {
    kill "$deno_pid" 2>/dev/null || true
    wait "$deno_pid" 2>/dev/null || true
}

trap cleanup INT TERM

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

# Start SSR server if it exists
SSR_SERVER_PATH="$SCRIPT_DIR/mods/ssr/server.tsx"
if [ -f "$SSR_SERVER_PATH" ]; then
    cd "$SCRIPT_DIR/mods/ssr"
    deno task start &
    deno_pid=$!
    cd "$SCRIPT_DIR"
fi

sleep 1
ndc -C "$SCRIPT_DIR" -p 8080 -d 2>&1 | tee /tmp/site.log
# echo ndc -C "$SCRIPT_DIR" -p 8080 -d
# gdb `which ndc`
