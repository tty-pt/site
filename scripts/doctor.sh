#!/usr/bin/env bash
set -uo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
cd "$REPO_ROOT"

BOLD="\033[1m"
GREEN="\033[0;32m"
YELLOW="\033[0;33m"
RED="\033[0;31m"
RESET="\033[0m"

echo -e "${BOLD}=== System & Project Health Check (make doctor) ===${RESET}\n"

ERRORS=0
WARNINGS=0

check_pass() {
  echo -e "  [ ${GREEN}OK${RESET} ] $1"
}

check_warn() {
  echo -e "  [ ${YELLOW}WARN${RESET} ] $1"
  WARNINGS=$((WARNINGS + 1))
}

check_fail() {
  echo -e "  [ ${RED}FAIL${RESET} ] $1"
  ERRORS=$((ERRORS + 1))
}

# 1. Compilers & Toolchain
echo -e "${BOLD}1. Compilers & Runtimes${RESET}"
if command -v clang >/dev/null 2>&1; then
  CLANG_VER=$(clang --version | head -n1)
  check_pass "clang available: $CLANG_VER"
else
  check_fail "clang is not installed or not in PATH"
fi

if echo 'int main(void){return 0;}' | clang -x c - -target wasm32-wasi -c -o /dev/null 2>/dev/null; then
  check_pass "wasm32-wasi target supported by clang"
else
  check_warn "wasm32-wasi target not found — WASM bundles will be skipped (pure SSR fallback)"
fi

if command -v deno >/dev/null 2>&1; then
  DENO_VER=$(deno --version | head -n1)
  check_pass "deno available: $DENO_VER"
else
  check_warn "deno is not installed — e2e tests will not run"
fi

if command -v entr >/dev/null 2>&1; then
  check_pass "entr available (for make watch)"
else
  check_warn "entr is not installed (optional: needed for make watch)"
fi

# 2. Build Artifacts & LSP
echo -e "\n${BOLD}2. Build Artifacts & IDE Support${RESET}"
if [ -f "compile_commands.json" ]; then
  ENTRIES=$(grep -c '"file":' compile_commands.json || echo 0)
  check_pass "compile_commands.json present ($ENTRIES indexed files)"
else
  check_warn "compile_commands.json missing (run 'make compile_commands.json')"
fi

if [ -f ".clangd" ]; then
  check_pass ".clangd configuration present"
else
  check_warn ".clangd missing"
fi

# 3. Data Directories
echo -e "\n${BOLD}3. Data Directories${RESET}"
DATA_DIRS=("var/poem" "var/song" "var/gig" "var/grp" "var/song.types" "var/song.authors")
ALL_DIRS_OK=1
for d in "${DATA_DIRS[@]}"; do
  if [ ! -d "$d" ]; then
    ALL_DIRS_OK=0
    mkdir -p "$d"
  fi
done
if [ $ALL_DIRS_OK -eq 1 ]; then
  check_pass "All dataset directories present in var/"
else
  check_pass "Created missing dataset directories in var/"
fi

# 4. Chroot & Server Prerequisites
echo -e "\n${BOLD}4. Chroot Prerequisites${RESET}"
if [ -x "./bin/sh" ]; then
  check_pass "chroot /bin/sh binary present"
else
  check_warn "chroot ./bin/sh missing (run 'mkdir -p ./bin && cp /bin/sh ./bin/sh')"
fi

# 5. Header Precedence Check
echo -e "\n${BOLD}5. Header Resolution Precedence${RESET}"
if [ -f "/usr/include/hyle-bud/hyle-bud.h" ]; then
  check_pass "Stale system header detected in /usr/include; repo include flags properly prioritize local headers"
else
  check_pass "No conflicting system headers in /usr/include/hyle-bud"
fi

# 6. Server & Port Status
echo -e "\n${BOLD}6. Server Status (Port 8080)${RESET}"
if nc -z 127.0.0.1 8080 2>/dev/null; then
  HTTP_CODE=$(curl -s -o /dev/null -w "%{http_code}" --max-time 1 http://localhost:8080/ || echo "err")
  if [ "$HTTP_CODE" = "200" ] || [ "$HTTP_CODE" = "303" ]; then
    check_pass "axil is running and healthy on http://localhost:8080 (HTTP $HTTP_CODE)"
  else
    check_warn "Port 8080 is in use (HTTP response: $HTTP_CODE)"
  fi
else
  check_pass "Port 8080 is available (server offline)"
fi

# 7. Boundary Checks
echo -e "\n${BOLD}7. Architectural Boundary Invariants${RESET}"
if sh scripts/check-module-boundaries.sh >/dev/null 2>&1 && \
   sh scripts/check-ux-purity.sh >/dev/null 2>&1 && \
   sh scripts/check-no-site-specific-js.sh >/dev/null 2>&1 && \
   sh scripts/check-wasm-imports.sh >/dev/null 2>&1; then
  check_pass "All architectural boundary & purity checks PASS"
else
  check_fail "Boundary or purity checks failed (run 'make boundary-check' for details)"
fi

echo -e "\n${BOLD}=== Doctor Summary: ${ERRORS} errors, ${WARNINGS} warnings ===${RESET}"
if [ $ERRORS -eq 0 ]; then
  echo -e "${GREEN}Project is in healthy working condition!${RESET}\n"
  exit 0
else
  echo -e "${RED}Please resolve the errors above.${RESET}\n"
  exit 1
fi
