#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"
unit_run_nd_seven_test tests/unit/test_map_render.c "-O1" "-lxylem -lxxhash -lm -lpthread"
