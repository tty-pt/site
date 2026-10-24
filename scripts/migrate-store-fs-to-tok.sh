#!/bin/sh
set -e

# migrate-store-fs-to-tok.sh: pack multi-file store_fs records into record.tok containers
DATA_DIR="${1:-var/song}"
PACKER="./external/libhyle-source/bin/hyle-tok"

if [ ! -d "$DATA_DIR" ]; then
    echo "Directory not found: $DATA_DIR"
    exit 1
fi

if [ ! -x "$PACKER" ]; then
    echo "hyle-tok binary not found or not executable at $PACKER"
    exit 1
fi

echo "Migrating $DATA_DIR to record.tok containers..."
count=0

for item_dir in "$DATA_DIR"/*; do
    if [ ! -d "$item_dir" ]; then
        continue
    fi
    id=$(basename "$item_dir")
    container="$item_dir/record.tok"
    if [ -f "$container" ]; then
        continue
    fi
    echo "  packing $id -> $container..."
    "$PACKER" pack-dir "$item_dir" "$container"
    count=$((count + 1))
done

echo "Migration complete: $count containers created."
