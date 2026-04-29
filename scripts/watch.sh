#!/bin/sh

# Use entr to rebuild and restart the server
# -r: restart the child process when files change

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

echo "Starting watch loop. Server logs will be visible here."
find_files | entr -c -r sh -c "make && DEBUG=1 ./start.sh"
