#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-8090}"
MODULE="$DIR/lib/libaxil-embed-search"

if [ ! -f "$MODULE.so" ]; then
    echo "Building module first..."
    make -C "$DIR"
fi

echo "Starting Axil Embed Search server on http://localhost:$PORT ..."
echo "Open http://localhost:$PORT/ in your browser to view the interactive UI form."
echo "Press Ctrl+C to stop."
exec axil -d -p "$PORT" -m "$MODULE"
