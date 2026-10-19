#!/bin/sh
set -eu
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUT="$ROOT/mods/common/ux/version.gen.h"
CSS_CKSUM="$( (cksum "$ROOT/htdocs/styles.css" 2>/dev/null; cksum "$ROOT/htdocs/hyle.css" 2>/dev/null; cksum "$ROOT/htdocs/bud-client.js" 2>/dev/null; cksum "$ROOT/htdocs/bud-hydrate.js" 2>/dev/null) | cksum | awk '{print $1}')"
FRAG_CKSUM="$(cksum "$ROOT/htdocs/hyle-fragments.js" 2>/dev/null | awk '{print $1}')"
# fallback if cksum missing
if [ -z "$CSS_CKSUM" ]; then
	CSS_CKSUM="$(date +%s)"
fi
if [ -z "$FRAG_CKSUM" ]; then
	FRAG_CKSUM="$CSS_CKSUM"
fi
# short hex for url
HEX="$(printf "%08x" "$CSS_CKSUM" 2>/dev/null || printf "%s" "$CSS_CKSUM")"
FRAG_HEX="$(printf "%08x" "$FRAG_CKSUM" 2>/dev/null || printf "%s" "$FRAG_CKSUM")"
# client version from bud-client + hydrate
CLIENT_HEX="$HEX"
cat > "$OUT.tmp" <<EOF
#pragma once
#define SITE_CSS_V "?v=$HEX"
#define SITE_CLIENT_V "?v=$CLIENT_HEX"
#define SITE_FRAGMENTS_V "?v=$FRAG_HEX"
EOF
# only replace if changed to avoid rebuild churn
if [ -f "$OUT" ] && cmp -s "$OUT.tmp" "$OUT"; then
	rm "$OUT.tmp"
else
	mv "$OUT.tmp" "$OUT"
fi
