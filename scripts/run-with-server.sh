#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PORT="${PORT:-8080}"
SERVER_PID=""

cleanup() {
  if [ -n "$SERVER_PID" ]; then
    kill "$SERVER_PID" 2>/dev/null || true
    wait "$SERVER_PID" 2>/dev/null || true
  fi
}
trap cleanup EXIT HUP INT TERM

is_server_up() {
  curl -s --max-time 1 "http://localhost:${PORT}/" >/dev/null 2>&1
}

# If server is already running, just run the command directly
if is_server_up; then
  "$@"
  exit $?
fi

# Ensure data directories exist
mkdir -p "${REPO_ROOT}/var/poem" "${REPO_ROOT}/var/song" "${REPO_ROOT}/var/gig" "${REPO_ROOT}/var/grp" "${REPO_ROOT}/var/song.types" "${REPO_ROOT}/var/song.authors"

export AUTH_SKIP_CONFIRM=1
export LD_LIBRARY_PATH="${REPO_ROOT}/external/libxylem/lib:${REPO_ROOT}/external/axil/lib:${REPO_ROOT}/external/axil-auth/lib:${REPO_ROOT}/external/axil-hyle/lib:${REPO_ROOT}/external/hyle/lib:${REPO_ROOT}/external/libtransp/lib:${REPO_ROOT}/external/hyle/c/libhyle-bud/lib:${REPO_ROOT}/external/hyle/c/libhyle-source/lib:${REPO_ROOT}/external/bud/lib:${REPO_ROOT}/external/qmap/lib:${REPO_ROOT}/external/stoma/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"

AXIL_BIN="${REPO_ROOT}/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
  AXIL_BIN="axil"
fi

# Clean up any stale process holding port before starting
pkill -f "axil.*-p ${PORT}" 2>/dev/null || true
sleep 0.1

"$AXIL_BIN" -C "$REPO_ROOT" -p "$PORT" -d -m mods/core/core > /tmp/axil_transient.log 2>&1 &
SERVER_PID=$!

# Wait for server readiness (up to 5 seconds)
for _ in $(seq 1 50); do
  if is_server_up; then
    break
  fi
  sleep 0.1
done

if ! is_server_up; then
  echo "Error: Failed to start transient axil server on port ${PORT}." >&2
  cat /tmp/axil_transient.log >&2 || true
  exit 1
fi

# Run the test command
set +e
"$@"
RC=$?
set -e

exit $RC
