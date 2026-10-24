#!/bin/sh
set -eu
gcc -Iinclude -Iexternal/axil-nd/include -o tests/unit/test_axil_ws_multiplex tests/unit/test_axil_ws_multiplex.c -lxylem
./tests/unit/test_axil_ws_multiplex
rm -f tests/unit/test_axil_ws_multiplex
