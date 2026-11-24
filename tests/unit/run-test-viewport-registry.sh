#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"
unit_run_nd_ux_test tests/unit/test_viewport_registry.c
