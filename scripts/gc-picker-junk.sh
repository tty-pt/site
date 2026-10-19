#!/bin/sh
# gc-picker-junk.sh — reference-based garbage collection for var/song.types
#
# Removes song.types entities that no live song references and that are not
# module-test fixtures. Backs up the whole dataset first.
#
# Usage: sh scripts/gc-picker-junk.sh [repo-root]
# Default repo-root: .
# After running, restart axil so the in-memory hyle store reloads.

set -u

BASE="${1:-.}"
TYPES="$BASE/var/song.types"

if [ ! -d "$TYPES" ]; then
  echo "gc-picker-junk: no $TYPES" >&2
  exit 1
fi

STAMP=$(date +%Y%m%d-%H%M%S)
BACKUP="/tmp/song.types-$STAMP.tgz"
tar czf "$BACKUP" -C "$BASE/var" song.types 2>/dev/null
echo "backup: $BACKUP"

# Hard keep-list: module fixtures + canonical types tests depend on
# (defensive; even if currently unreferenced by var/song, tests create
# songs referencing these on every run).
KEEP_FIXTURES="sbt_com sbt_ent sbt_san gj_type_c gj_type_e src_type_b
test_type_src test_type_grp test_type_gig test_type_song
communion entry"

BEFORE=$(ls -1 "$TYPES" | wc -l)
REMOVED=0
REMOVED_LIST=""

for slug in $(ls -1 "$TYPES"); do
  # Skip if in fixture keep-list
  skip=0
  for fix in $KEEP_FIXTURES; do
    if [ "$slug" = "$fix" ]; then skip=1; break; fi
  done
  [ "$skip" = "1" ] && continue

  # Auto-generated duplicate entities created by the picker Add-on-search path:
  # their slug is the canonical name plus a 12-hex unique suffix (e.g. the
  # junk "Communion5860cb7afd18" entities). They are never canonical.
  case "$slug" in
    genrejazz*|keyenter*|test_type_*|communion[0-9a-f]*|entry[0-9a-f]*)
      rm -rf "$TYPES/$slug"
      REMOVED=$((REMOVED + 1))
      REMOVED_LIST="$REMOVED_LIST $slug"
      continue
      ;;
  esac

  # Read the label (name file inside the entity dir)
  LABEL=""
  if [ -f "$TYPES/$slug/name" ]; then
    LABEL=$(cat "$TYPES/$slug/name" | tr -d '\n')
  fi
  [ -z "$LABEL" ] && LABEL="$slug"

  # Check if any live song references this label in its type field
  if find "$BASE/var/song" -maxdepth 2 -name type -exec grep -qlF "$LABEL" {} + 2>/dev/null; then
    continue
  fi

  # Unreferenced + not fixture: remove
  rm -rf "$TYPES/$slug"
  REMOVED=$((REMOVED + 1))
  REMOVED_LIST="$REMOVED_LIST $slug"
done

AFTER=$(ls -1 "$TYPES" 2>/dev/null | wc -l)
echo "var/song.types: $BEFORE -> $AFTER ($REMOVED removed)"
if [ -n "$REMOVED_LIST" ]; then
  echo "removed:$REMOVED_LIST"
fi
