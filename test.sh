#!/usr/bin/env bash
set -euo pipefail

echo "========================================================"
echo "    Release Verification Suite: Core Platform & Tooling  "
echo "========================================================"

ROOT_DIR="$(cd "$(dirname "$0")" && pwd)"

echo "=== Wave 1: libqsys ==="
make -C "$ROOT_DIR/external/libqsys" test

echo "=== Wave 1: libbud ==="
make -C "$ROOT_DIR/external/libbud" test

echo "=== Wave 2: libcorm ==="
make -C "$ROOT_DIR/external/libcorm" test

echo "=== Wave 3: libxylem ==="
make -C "$ROOT_DIR/external/libxylem" test

echo "=== Wave 3: libjoint ==="
make -C "$ROOT_DIR/external/libjoint" test

echo "=== Wave 3: libislet ==="
make -C "$ROOT_DIR/external/libislet" test

echo "=== Wave 3: libsepal ==="
make -C "$ROOT_DIR/external/libsepal" test

echo "=== Wave 3: liblemma ==="
make -C "$ROOT_DIR/external/liblemma" test

echo "=== Wave 4: libhyle ==="
make -C "$ROOT_DIR/external/libhyle" test

echo "=== Wave 4: axil ==="
make -C "$ROOT_DIR/external/axil" test

echo "=== Wave 6: axil-tty ==="
( cd "$ROOT_DIR/external/axil-tty" && ./test.sh )

echo "=== Wave 6: axil-auren ==="
make -C /home/quirinpa/axil-auren/tests run

echo "=== Tooling: pi-quest (workspace) ==="
( cd "$ROOT_DIR/.pi/extensions/pi-quest" && npm test )

echo "=== Tooling: pi-quest (standalone) ==="
( cd /home/quirinpa/pi-quest && npm test )

echo "=== Tooling: pi-mm (workspace) ==="
( cd "$ROOT_DIR/.pi/extensions/pi-mm" && npm test )

echo "=== Tooling: pi-mm (standalone) ==="
( cd /home/quirinpa/pi-mm && npm test )

echo "========================================================"
echo "    ALL CORE LIBRARIES AND EXTENSIONS PASSED CLEANLY!    "
echo "========================================================"
