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
  pkill -f "axil.*-p ${PORT}" 2>/dev/null || true
}
trap cleanup EXIT HUP INT TERM

is_server_up() {
  curl -s --max-time 1 "http://localhost:${PORT}/" >/dev/null 2>&1
}

export AUTH_SKIP_CONFIRM=1

# Clean up any stale process holding port to ensure fresh test runs
pkill -f "axil.*-p ${PORT}" 2>/dev/null || true
sleep 0.5

# Ensure data directories exist
mkdir -p "${REPO_ROOT}/var/poem" "${REPO_ROOT}/var/song" "${REPO_ROOT}/var/gig" "${REPO_ROOT}/var/grp" "${REPO_ROOT}/var/software" "${REPO_ROOT}/var/song.types" "${REPO_ROOT}/var/song.authors" "${REPO_ROOT}/var/nd"

export AUTH_SKIP_CONFIRM=1
export AXIL_ND_DB="${REPO_ROOT}/var/nd/universe.db"
export AXIL_ND_HTDOCS="${REPO_ROOT}/htdocs"

if [ -f "$REPO_ROOT/scripts/nd-kernel-mods.sh" ]; then
  . "$REPO_ROOT/scripts/nd-kernel-mods.sh"
  export LD_LIBRARY_PATH="$(nd_local_ld_path "$REPO_ROOT")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
fi

if [ ! -f "$AXIL_ND_DB" ]; then
  bash "${REPO_ROOT}/scripts/seed-nd-universe.sh" "$AXIL_ND_DB" 1
fi

AXIL_BIN="${REPO_ROOT}/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
  AXIL_BIN="axil"
fi

# Clean up any stale process holding port before starting
pkill -f "axil.*-p ${PORT}" 2>/dev/null || true
# The daemonised predecessor can still be shutting down (and holding the
# listen socket) when the previous run's cleanup returns; wait it out so the
# new bind does not silently fail.
for _ in $(seq 1 100); do
  pgrep -f "axil.*-p ${PORT}" >/dev/null 2>&1 || break
  sleep 0.1
done
sleep 0.1

"$AXIL_BIN" -A -C "$REPO_ROOT" -p "$PORT" -d -m "mods/core/core${ND_KERNEL_MODS:+:$ND_KERNEL_MODS}" > /tmp/axil_transient.log 2>&1 &
SERVER_PID=$!

# Wait for server readiness (up to 30 seconds; startup scales with var/ data)
for _ in $(seq 1 300); do
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
