#!/bin/sh
set -e

SCRIPT_DIR=$(cd "$(dirname "$0")" && pwd)

exec ndc -C "$SCRIPT_DIR" -p 8080 -d >> /tmp/site.log 2>&1
