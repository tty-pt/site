#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

exec ndc -C "$SCRIPT_DIR" -p 8080 -d -m mods/core/core >> /tmp/site.log 2>&1
