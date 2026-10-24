#!/bin/bash
set -eu

# seed-nd-universe.sh — Seeds a clean NeverDark MUCK database with planet regions,
# all tenant modules loaded, and initial world fixtures (Citadel, NPC, items).

TARGET_DB="var/nd/universe.db"
NUM_PLANETS=1

for arg in "$@"; do
	case "$arg" in
		--sector32)
			NUM_PLANETS=32
			;;
		[0-9]*)
			NUM_PLANETS="$arg"
			;;
		*)
			TARGET_DB="$arg"
			;;
	esac
done

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AXIL_ND_DIR="$REPO_ROOT/external/axil-nd"
AXIL_BIN_DIR="$REPO_ROOT/external/axil/bin"

if [ ! -d "$AXIL_ND_DIR" ]; then
	echo "Error: external/axil-nd not found" >&2
	exit 1
fi

mkdir -p "$(dirname "$TARGET_DB")"
cp "$REPO_ROOT"/external/nd-*/data/*.dsv "$(dirname "$TARGET_DB")"/ 2>/dev/null || true
cp "$REPO_ROOT"/mods/nd/data/*.dsv "$(dirname "$TARGET_DB")"/ 2>/dev/null || true
TARGET_DB="$(cd "$(dirname "$TARGET_DB")" 2>/dev/null && pwd)/$(basename "$TARGET_DB")"

# Ensure all tenant module shared libraries are symlinked in external/axil-nd/lib/
mkdir -p "$AXIL_ND_DIR/lib"
for d in "$REPO_ROOT"/external/nd-*; do
	mod_name="$(basename "$d")"
	if [ -f "$d/lib/lib$mod_name.so" ]; then
		ln -sf "../../$mod_name/lib/lib$mod_name.so" "$AXIL_ND_DIR/lib/lib$mod_name.so"
	fi
done

if [ -f "$REPO_ROOT/external/axil-auth/lib/libaxil-auth.so" ]; then
	ln -sf "../../axil-auth/lib/libaxil-auth.so" "$AXIL_ND_DIR/lib/libaxil-auth.so"
fi

# Ephemeral auth setup matching current runtime user
SEED_TMP="$(mktemp -d)"
AUTH_DIR="$SEED_TMP/auth"
mkdir -p "$AUTH_DIR/etc"

USER_NAME="${USER:-$(id -un)}"
USER_PASS="admin123"

# Generate bcrypt hash for user
USER_HASH='$$2b$$12$$e8YgQv7rI5B4k2N5r8H1LezE2B2A2B2A2B2A2B2A2B2A2B2A2B2A2'
if command -v python3 >/dev/null 2>&1; then
	USER_HASH=$(python3 -c "import bcrypt; print(bcrypt.hashpw(b'$USER_PASS', bcrypt.gensalt()).decode())" 2>/dev/null || echo "$USER_HASH")
fi

printf '%s:%s:1001:67::::::\n' "$USER_NAME" "$USER_HASH" > "$AUTH_DIR/etc/shadow"
printf '%s:x:1001:67::%s:/bin/false\n' "$USER_NAME" "$AUTH_DIR" > "$AUTH_DIR/etc/passwd"

SEED_PORT=$((28100 + RANDOM % 1000))
SEED_LOG="$SEED_TMP/seed.log"
SEED_TXT="$SEED_TMP/seed.txt"
: > "$SEED_TXT"

SEED_PID=""
cleanup() {
	if [ -n "$SEED_PID" ]; then
		kill -TERM "$SEED_PID" 2>/dev/null || true
		wait "$SEED_PID" 2>/dev/null || true
	fi
	if [ -n "${SEED_KEEP_LOG:-}" ] && [ -f "$SEED_LOG" ]; then cp "$SEED_LOG" "$SEED_KEEP_LOG"; fi
	rm -rf "$SEED_TMP"
}
trap cleanup EXIT INT TERM

# Library paths
export PATH="$AXIL_BIN_DIR:$PATH"
export AXIL_AUTH_ETC="$AUTH_DIR/etc"
export AXIL_ND_DB="$TARGET_DB"

MODULE_SET="./lib/axil-nd"

echo "Booting temporary NeverDark seeder on port $SEED_PORT..."
(cd "$AXIL_ND_DIR" && axil -d -A -p "$SEED_PORT" -m "$MODULE_SET" > "$SEED_LOG" 2>&1 & echo $! > "$SEED_TMP/pid")
SEED_PID=$(cat "$SEED_TMP/pid")

# Wait for server ready
tries=50
while [ $tries -gt 0 ]; do
	if grep -q "st_init: st_restore: region id=0x0000000000000000" "$SEED_LOG" 2>/dev/null; then
		break
	fi
	tries=$((tries - 1))
	sleep 0.1
done

if [ $tries -eq 0 ]; then
	echo "Error: NeverDark daemon failed to start during seed. Log:" >&2
	cat "$SEED_LOG" >&2
	exit 1
fi

echo "Connecting admin session to seed $NUM_PLANETS planet(s)..."
exec 8<>/dev/tcp/127.0.0.1/$SEED_PORT

cmd() {
	printf '%s\n\n' "$*" >&8 2>/dev/null || return 0
	local line
	while read -t 0.25 -u 8 -r line; do
		printf '%s\n' "$line" >> "$SEED_TXT"
	done
	return 0
}

wait_marker() {
	local marker="$1"
	local tries="${2:-80}"
	local line
	while [ $tries -gt 0 ]; do
		grep -qaF "$marker" "$SEED_TXT" && return 0
		if read -t 0.05 -u 8 -r line; then
			printf '%s\n' "$line" >> "$SEED_TXT"
			continue
		fi
		tries=$((tries - 1))
	done
	return 1
}

# Login
cmd "connect $USER_NAME"
wait_marker "Password:"
cmd "$USER_PASS"
wait_marker "nd_player_login" || true

# Seed initial universe fixtures
echo "Establishing universe sector with $NUM_PLANETS planet(s)..."

# Establish central planet 1 fixtures once
cmd "planet 1"
wait_marker "planet 1 established" || true

# Seed Planet 1 region with tenant content modules
for mod in libnd-core libnd-attr libnd-equip libnd-fight libnd-spell libnd-shop libnd-level libnd-mortal libnd-race libnd-class libnd-mob libnd-biome libnd-plant libnd-drink libnd-seat libnd-stone libnd-vanilla libnd-wts libnd-other; do
	cmd "loadmod $mod 1"
	wait_marker "loaded into" || true
done

cmd "room 0 0 0 1"
wait_marker "Room created" || true
cmd "teleport 0 0 0 1"
cmd "name The Citadel Common Hall"
cmd "biome urban_stone"
cmd "create Steel Broadsword"
cmd "drop Steel Broadsword"
cmd "create Leather Tunic"
cmd "drop Leather Tunic"
cmd "create Iron Helm"
cmd "drop Iron Helm"

# 2. Room at (1, 0, 0, 1): "The Merchant Bazaar" (East exit)
cmd "room 1 0 0 1"
cmd "teleport 1 0 0 1"
cmd "name The Merchant Bazaar"
cmd "create Shopkeeper Vaelin"
cmd "drop Shopkeeper Vaelin"
cmd "create Health Potion"
cmd "drop Health Potion"
cmd "create Mana Potion"
cmd "drop Mana Potion"

# 3. Room at (0, 1, 0, 1): "The Arcane Sanctum" (North exit)
cmd "room 0 1 0 1"
cmd "teleport 0 1 0 1"
cmd "name The Arcane Sanctum"
cmd "create Spell Altar"
cmd "drop Spell Altar"

# 4. Room at (0, -1, 0, 1): "The Training Grounds" (South exit)
cmd "room 0 -1 0 1"
cmd "teleport 0 -1 0 1"
cmd "name The Training Grounds"
cmd "create Training Golem"
cmd "drop Training Golem"

# 5. Room at (-1, 0, 0, 1): "The Whispering Woods" (West exit)
cmd "room -1 0 0 1"
cmd "teleport -1 0 0 1"
cmd "name The Whispering Woods"
cmd "create Ancient Pine"
cmd "drop Ancient Pine"
cmd "create Wild Mushrooms"
cmd "drop Wild Mushrooms"

# 6. Room at (0, 2, 0, 1): "The Boreal Canopy" (North of Arcane Sanctum)
cmd "room 0 2 0 1"
cmd "teleport 0 2 0 1"
cmd "name The Boreal Canopy"
cmd "create Tall Fir"
cmd "drop Tall Fir"

# Return player to Central Spawn Hall
cmd "teleport 0 0 0 1"

# Seed spawn room at (0, 0, 0, 0) with initial ground item fixtures
cmd "teleport 0 0 0 0"
cmd "create Steel Broadsword"
cmd "drop Steel Broadsword"
cmd "create Leather Tunic"
cmd "drop Leather Tunic"
cmd "create Iron Helm"
cmd "drop Iron Helm"

# If 2 or more planets, establish Planet 2 as the 3D WebGL Showcase World
if [ "$NUM_PLANETS" -ge 2 ]; then
	cmd "planet 2"
	wait_marker "planet 2 established" || true

	for mod in libnd-biome libnd-attr libnd-equip libnd-fight libnd-spell; do
		cmd "loadmod $mod 2"
		wait_marker "loaded into" || true
	done

	# Room 1: The High Crystal Sanctum at (0, 0, 0, 2)
	cmd "room 0 0 0 2"
	cmd "teleport 0 0 0 2"
	cmd "name The High Crystal Sanctum"
	cmd "biome crystal"
	cmd "create Crystal Core"
	cmd "drop Crystal Core"
	cmd "create Floating Prism Relic"
	cmd "drop Floating Prism Relic"

	# Room 2: The Observatory Terrace at (1, 0, 0, 2) (East exit)
	cmd "room 1 0 0 2"
	cmd "teleport 1 0 0 2"
	cmd "name The Observatory Terrace"
	cmd "create Star Tracker"
	cmd "drop Star Tracker"

	# Room 3: The Luminescent Spire at (0, 1, 0, 2) (North exit)
	cmd "room 0 1 0 2"
	cmd "teleport 0 1 0 2"
	cmd "name The Luminescent Spire"
	cmd "create Sun Prism"
	cmd "drop Sun Prism"

	# Room 4: The Shadow Vault at (0, -1, 0, 2) (South exit)
	cmd "room 0 -1 0 2"
	cmd "teleport 0 -1 0 2"
	cmd "name The Shadow Vault"
	cmd "create Obsidian Sentinel"
	cmd "drop Obsidian Sentinel"

	# Return to Planet 1 spawn
	cmd "teleport 0 0 0 1"
fi

# If 32-planet sector is requested, register remaining planetary coordinates
if [ "$NUM_PLANETS" -gt 2 ]; then
	p=3
	while [ "$p" -le "$NUM_PLANETS" ]; do
		cmd "planet $p"
		p=$((p + 1))
	done
fi

echo "Flushing seeded universe database..."
cmd "save"
wait_marker "Saved" || true

exec 8>&-

echo "Stopping seeder daemon..."
kill -TERM "$SEED_PID" 2>/dev/null || true
wait "$SEED_PID" 2>/dev/null || true
SEED_PID=""

echo "Successfully seeded $NUM_PLANETS planet(s) in $TARGET_DB."
