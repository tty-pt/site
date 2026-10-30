#!/bin/bash
set -euo pipefail

echo "=== Testing mods/chat ==="

REPO_ROOT="$(cd ../.. && pwd)"

# 1. Clean test sessions
rm -rf "$REPO_ROOT/var/chat/sessions/test_sess"*

# 2. Build and run unit tests for session persistence
clang -I"$REPO_ROOT" -I. \
  -o /tmp/chat_test test_unit.c

echo "Running unit tests..."
/tmp/chat_test

# 3. Build and run live Auren integration test
clang -I"$REPO_ROOT" -I. \
  -o /tmp/chat_auren_test test_auren.c

echo "Running Auren live integration test..."
/tmp/chat_auren_test

echo "=== All Chat Tests Passed ==="
