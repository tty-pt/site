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
SSR_SERVER_PATH="$SCRIPT_DIR/mods/ssr/server.ts"
if [ -f "$SSR_SERVER_PATH" ]; then
    cd "$SCRIPT_DIR/mods/ssr"
    /home/quirinpa/.deno/bin/deno run --allow-read --allow-net --allow-env server.ts &
    deno_pid=$!
    cd "$SCRIPT_DIR"
fi

cd "$SCRIPT_DIR"
exec /home/quirinpa/ndc/bin/ndc -C "$SCRIPT_DIR" -p 8080 -d
