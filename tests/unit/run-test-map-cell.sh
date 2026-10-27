#!/bin/sh
set -eu
gcc -Wall -Wextra -Iexternal/axil-nd/include -o tests/unit/test_map_cell tests/unit/test_map_cell.c
./tests/unit/test_map_cell
rm -f tests/unit/test_map_cell
