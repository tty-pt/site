#!/bin/sh

# Use entr to rebuild and restart the server
# -r: restart the child process when files change

DEBUG_DIR="$(cd "$(dirname "$0")/.." && pwd)/debug"
BUILD_LOG_DIR="$DEBUG_DIR/builds"
RUNTIME_LOG_DIR="$DEBUG_DIR/runtime"

find_files() {
    # Watch C, Rust, Makefiles, and configuration files
    # Exclude data, git, node_modules, and rust build artifacts
    find . -type f \
      -not -path '*/.*' \
      -not -path './items/*' \
      -not -path './node_modules/*' \
      -not -path '*/target/*' \
      -not -path './etc/*' \
      -not -path './lib/*' \
      -not -path './bin/*' \
      \( -name "*.c" -o -name "*.h" -o -name "*.rs" -o -name "Makefile" -o -name "*.mk" -o -name "mods.load" -o -name "*.json" \)
}

if ! command -v entr >/dev/null 2>&1; then
    echo "entr is not installed. Please install it first."
    exit 1
fi

# Ensure data dirs exist for the first run
make test-data-dirs >/dev/null 2>&1

# Ensure debug directories exist
mkdir -p "$BUILD_LOG_DIR" "$RUNTIME_LOG_DIR"

# Generate timestamp for log files
timestamp=$(date +%Y-%m-%d_%H-%M-%S)
build_log="$BUILD_LOG_DIR/build_$timestamp.log"
runtime_log="$RUNTIME_LOG_DIR/axil.log"

echo "Starting watch loop. Logs will be saved to:"
echo "  Build: $build_log"
echo "  Runtime: $runtime_log"

# Build and capture output, then start server with log capture
find_files | entr -n -r sh -c "\
    echo '=== Build started at $(date) ===' >> $build_log; \
    make 2>&1 | tee -a $build_log; \
    rc=\$\?; \
    echo '=== Build completed at $(date) (exit code: '\$rc') ===' >> $build_log; \
    AUTH_SKIP_CONFIRM=1 DEBUG=1 ./start.sh >> $runtime_log 2>&1"
