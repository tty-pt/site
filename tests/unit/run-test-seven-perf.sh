#!/bin/sh
set -eu
. "$(dirname "$0")/common.sh"
unit_run_nd_seven_test tests/unit/test_seven_perf.c "-Wall -Wextra -Werror -O1" "-lxxhash -lm -lpthread"
