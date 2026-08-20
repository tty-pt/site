#!/bin/sh
# One-time migration: items/<mod>/items/<id> → var/<source>/<id>
# Also renames the gig (formerly songbook) meta file 'choir' → 'grp'
# and removes legacy directories. Idempotent — safe to re-run.
set -e

SITE=${SITE:-$(cd "$(dirname "$0")/.." && pwd)}
cd "$SITE"

mkdir -p var

mv items/song/items  var/song       2>/dev/null || true
mv items/song/types  var/song.types 2>/dev/null || true
mv items/poem/items  var/poem       2>/dev/null || true
mv items/songbook/items var/gig     2>/dev/null || true
mv items/choir/items var/grp        2>/dev/null || true

# Field rename: each gig item's 'choir' reference file becomes 'grp'.
for d in var/gig/*/; do
	[ -f "$d/choir" ] && mv "$d/choir" "$d/grp"
done

# Legacy leftovers (unreferenced by current code).
rm -rf items/chords items/items items/songbook/item_songs \
	items/songbook/rows items/choir/index.tsv
rmdir items/song items/songbook items/choir items 2>/dev/null || true

echo "migration complete:"
ls var 2>/dev/null
