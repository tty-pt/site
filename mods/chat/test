#!/bin/bash
set -euo pipefail

echo "=== Testing mods/chat ==="

# 1. Clean test sessions
rm -rf var/chat/sessions/test_sess*

# 2. Build and run unit tests for session persistence
clang -I. -Imods/chat \
  -o /tmp/chat_test mods/chat/test_unit.c

echo "Running unit tests..."
/tmp/chat_test

# 3. Build and run live Auren integration test
clang -I. -Imods/chat \
  -o /tmp/chat_auren_test mods/chat/test_auren.c

echo "Running Auren live integration test..."
/tmp/chat_auren_test

echo "=== All Chat Tests Passed ==="
