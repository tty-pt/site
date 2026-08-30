#!/usr/bin/env bash
set -euo pipefail

REPO_ROOT="$(cd "$(dirname "$0")/.." && pwd)"
OUTPUT_FILE="${REPO_ROOT}/compile_commands.json"
TMP_FILE="${OUTPUT_FILE}.tmp"

# Common include flags
COMMON_INCLUDES=(
  "-I${REPO_ROOT}/external/axil/include"
  "-I${REPO_ROOT}/external/axil-auth/include"
  "-I${REPO_ROOT}/external/axil-hyle/include"
  "-I${REPO_ROOT}/external/qmap/include"
  "-I${REPO_ROOT}/external/libqmap/include"
  "-I${REPO_ROOT}/external/libxylem/include"
  "-I${REPO_ROOT}/external/bud/include"
  "-I${REPO_ROOT}/external/hyle/include"
  "-I${REPO_ROOT}/external/hyle/c/libhyle-source/include"
  "-I${REPO_ROOT}/external/hyle/c/libhyle-bud/include"
  "-I${REPO_ROOT}/external/libtransp/include"
  "-I${REPO_ROOT}/external/stoma/include"
  "-I${REPO_ROOT}/mods/common"
  "-I${REPO_ROOT}/mods/index"
  "-I${REPO_ROOT}/mods/song"
  "-I${REPO_ROOT}/mods/gig"
  "-I${REPO_ROOT}/mods/grp"
  "-I${REPO_ROOT}/mods/poem"
  "-I${REPO_ROOT}/mods/auth"
  "-I${REPO_ROOT}/mods/source"
  "-I${REPO_ROOT}/mods/mpfd"
)

INCLUDE_STR="${COMMON_INCLUDES[*]}"

echo "[" > "$TMP_FILE"
first=1

add_entry() {
  local file="$1"
  local flags="$2"
  local dir="${3:-$REPO_ROOT}"

  if [ $first -eq 0 ]; then
    echo "," >> "$TMP_FILE"
  fi
  first=0

  cat <<EOF >> "$TMP_FILE"
  {
    "directory": "${dir}",
    "file": "${file}",
    "command": "clang -std=c99 -Wall -Wextra -g ${flags} -c ${file}"
  }
EOF
}

# 1. Native modules
for f in $(find "${REPO_ROOT}/mods" -maxdepth 2 -name "*.c" | sort); do
  rel_file="${f#"${REPO_ROOT}/"}"
  add_entry "$rel_file" "-fPIC ${INCLUDE_STR}"
done

# 2. Module UX files (WASM & SSR components)
for f in $(find "${REPO_ROOT}/mods" -path "*/ux/*.c" | sort); do
  rel_file="${f#"${REPO_ROOT}/"}"
  add_entry "$rel_file" "-D__wasm__ --target=wasm32-wasi ${INCLUDE_STR}"
done

# 3. External C libraries
for d in axil axil-auth axil-hyle bud libqmap libtransp libxylem stoma; do
  if [ -d "${REPO_ROOT}/external/${d}" ]; then
    for f in $(find "${REPO_ROOT}/external/${d}" -name "*.c" -not -path "*/target/*" -not -path "*/.*" | sort); do
      rel_file="${f#"${REPO_ROOT}/"}"
      add_entry "$rel_file" "${INCLUDE_STR}"
    done
  fi
done

if [ -d "${REPO_ROOT}/external/hyle/c" ]; then
  for f in $(find "${REPO_ROOT}/external/hyle/c" -name "*.c" | sort); do
    rel_file="${f#"${REPO_ROOT}/"}"
    add_entry "$rel_file" "${INCLUDE_STR}"
  done
fi

# 4. Unit and Matrix test harnesses
if [ -d "${REPO_ROOT}/tests/unit" ]; then
  for f in $(find "${REPO_ROOT}/tests/unit" -name "*.c" | sort); do
    rel_file="${f#"${REPO_ROOT}/"}"
    add_entry "$rel_file" "${INCLUDE_STR}"
  done
fi

echo "" >> "$TMP_FILE"
echo "]" >> "$TMP_FILE"

mv "$TMP_FILE" "$OUTPUT_FILE"
echo "Generated compile_commands.json with $(grep -c '"file":' "$OUTPUT_FILE") entries."
