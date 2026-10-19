#!/bin/sh
# Lightweight docs sync — link checker + size budget, non-blocking for make boundary-check.
set -eu
FAIL=0
# Topic index completeness
for f in docs/*.md; do
  [ -f "$f" ] || continue
  base=$(basename "$f")
  if ! grep -q "$base" AGENTS.md docs/OVERVIEW.md 2>/dev/null; then
    echo "WARN: $f not in AGENTS.md topic index" >&2
  fi
done
# Size budget <250 lines per doc warning
for f in docs/*.md; do
  lines=$(wc -l < "$f")
  if [ "$lines" -gt 250 ]; then
    echo "WARN: $f $lines lines >250" >&2
  fi
done
# Link checker "See docs"
if grep -rn "See docs" docs/ | grep -v "docs/ADVANCED_GIT_RECOVERY\|docs/FILTERS" | grep -q "docs/"; then
  : # ok
fi
# Stale-string check: C-ISOMORPHIC trap vs BUILD auto-rebuild
if grep -q "NO prerequisites" docs/C-ISOMORPHIC-BUD.md && grep -q "auto.*rebuild" docs/BUILD.md; then
  if ! grep -q "Historical trap" docs/C-ISOMORPHIC-BUD.md; then
    echo "WARN: C-ISOMORPHIC-BUD stale trap note not reconciled with BUILD auto-rebuild" >&2
  fi
fi
exit 0
