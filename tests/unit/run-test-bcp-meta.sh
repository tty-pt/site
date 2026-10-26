#!/bin/sh
set -eu
gcc -Iexternal/axil-nd/include -Iexternal/nd-equip/include -I/usr/include/ttypt -o tests/unit/test_bcp_meta tests/unit/test_bcp_meta.c -lxylem
./tests/unit/test_bcp_meta
rm -f tests/unit/test_bcp_meta
