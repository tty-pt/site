#!/bin/sh
set -e

DIR="$(cd "$(dirname "$0")" && pwd)"
PORT="${PORT:-8092}"
MODULE="$DIR/lib/libaxil-resume-search"

if [ ! -f "$MODULE.so" ]; then
    echo "Building module first..."
    make -C "$DIR"
fi

mkdir -p "$DIR/uploads"

echo "=========================================================="
echo "⚡ Axil Resume & Job Opportunity Semantic Search Server"
echo "=========================================================="
echo "Server URL: http://localhost:$PORT"
echo "Web UI:     http://localhost:$PORT/"
echo "Upload:     POST http://localhost:$PORT/resume"
echo "Search:     GET  http://localhost:$PORT/candidates?job=..."
echo "Embedding:  http://127.0.0.1:4242/v1/embeddings (axil-auren)"
echo "Storage:    $DIR/uploads"
echo "=========================================================="
echo "Press Ctrl+C to stop."
exec axil -d -p "$PORT" -m "$MODULE"
