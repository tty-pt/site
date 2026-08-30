#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"
PORT="${PORT:-8080}"

echo "Preparing dev server on port ${PORT}..."

# Clean up any existing axil process holding PORT
if pkill -f "axil.*-p ${PORT}" 2>/dev/null || pkill -f "axil.*mods/core/core" 2>/dev/null; then
  echo "Stopped existing axil instance."
  sleep 0.5
fi

# Ensure data dirs exist
make test-data-dirs >/dev/null 2>&1

# Build
echo "Building project..."
make all

export AUTH_SKIP_CONFIRM=1
export DEBUG=1

echo "Starting server on http://localhost:${PORT}/ ..."
exec ./start.sh
