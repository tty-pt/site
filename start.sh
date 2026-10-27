#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)
PORT=${PORT:-8080}
# No LD_LIBRARY_PATH: every library resolves from the system path ($PREFIX/lib,
# mk/portable.mk) -- the same authority the link uses, so dev runs what prod
# runs. `make install-libs` publishes a rebuilt library there.

. "$SCRIPT_DIR/scripts/nd-kernel-mods.sh"
export LD_LIBRARY_PATH="$(nd_local_ld_path "$SCRIPT_DIR")${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
mkdir -p "$SCRIPT_DIR/var/nd"
mkdir -p "$SCRIPT_DIR/var/software"
if [ ! -d "$SCRIPT_DIR/var/software/corm" ]; then
    bash "$SCRIPT_DIR/scripts/seed-software.sh"
fi
export AXIL_ND_DB="${AXIL_ND_DB:-$SCRIPT_DIR/var/nd/universe.db}"
export AXIL_ND_HTDOCS="${AXIL_ND_HTDOCS:-$SCRIPT_DIR/htdocs}"

if [ ! -f "$AXIL_ND_DB" ]; then
    echo "NeverDark universe database not found at $AXIL_ND_DB. Auto-seeding default star sector..."
    bash "$SCRIPT_DIR/scripts/seed-nd-universe.sh" "$AXIL_ND_DB" 1
elif [ ! -f "$AXIL_ND_DB.seed-v5" ]; then
    # v2: content modules moved to the root region (plen=11 made their kernel
    #     calls silently fail: no biome colours, no trees).
    # v5: north of spawn is Lamina; the NW quadrant (x<=0,y>=1) is the 2D seven stage.
    # v3: the map driver is chosen by location (ASCII baseline, Lamina in the
    #     northern half of world 0); older universes resolve every region to Lamina.
    # Reseed; the old DB is kept, but its players and progress are NOT carried
    # over into the new universe.
    echo "NeverDark universe at $AXIL_ND_DB predates location-based map drivers; reseeding."
    echo "Players and progress in the old universe are not carried over; it is kept as $AXIL_ND_DB.pre-v5.bak"
    NEW_DB="$AXIL_ND_DB.reseed.$$"
    rm -f "$NEW_DB" "$NEW_DB.seed-v5"
    bash "$SCRIPT_DIR/scripts/seed-nd-universe.sh" "$NEW_DB" 1
    mv -f "$AXIL_ND_DB" "$AXIL_ND_DB.pre-v5.bak"
    mv -f "$NEW_DB" "$AXIL_ND_DB"
    mv -f "$NEW_DB.seed-v5" "$AXIL_ND_DB.seed-v5"
fi

if [ -n "$DEBUG" ]; then
    LOG_OUT="/dev/stdout"
else
    LOG_OUT="/tmp/site.log"
fi

if nc -z 127.0.0.1 "$PORT" 2>/dev/null; then
	echo "Port $PORT is already in use. Stop the existing server or run PORT=<port> make run." >&2
	exit 1
fi

# Check for AUTH_SKIP_CONFIRM environment variable or dev environment
if [ "$AUTH_ENV" = "prod" ]; then
    export AUTH_SKIP_CONFIRM=
else
    export AUTH_SKIP_CONFIRM=1
fi

if [ -n "$AUTH_SKIP_CONFIRM" ]; then
    echo "Starting axil with AUTH_SKIP_CONFIRM=$AUTH_SKIP_CONFIRM"
fi
AXIL_BIN="$SCRIPT_DIR/external/axil/bin/axil"
if [ ! -x "$AXIL_BIN" ]; then
    AXIL_BIN="axil"
fi

if test ! -z "$GDB"; then
  gdb --args "$AXIL_BIN" -C "$SCRIPT_DIR" -p 8080 -d -m "mods/core/core:$ND_KERNEL_MODS"
else
  "$AXIL_BIN" -A -C "$SCRIPT_DIR" -p "$PORT" -d -m "mods/core/core:$ND_KERNEL_MODS" 2>&1 | tee "$LOG_OUT"
fi
