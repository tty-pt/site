#!/bin/sh
set -eu
gcc -Iexternal/libbud/include -Iexternal/axil-nd/include -Iexternal/nd-seven/include -Imods/nd/ux -Iinclude -o tests/unit/test_map_render tests/unit/test_map_render.c mods/nd/ux/nd_lamina.c mods/nd/ux/nd_seven.c -lxylem
./tests/unit/test_map_render
rm -f tests/unit/test_map_render
