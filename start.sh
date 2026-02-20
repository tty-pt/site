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

cd /home/quirinpa/tty.pt/ssr
/home/quirinpa/.deno/bin/deno run --allow-read --allow-net server.ts &
deno_pid=$!

cd /home/quirinpa/tty.pt
exec /home/quirinpa/ndc/bin/ndc -C /home/quirinpa/tty.pt -p 8080 -d
