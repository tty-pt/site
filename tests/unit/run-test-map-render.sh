#!/bin/sh
set -eu
gcc -Iexternal/libbud/include -Iexternal/axil-nd/include -Imods/nd/ux -Iinclude -o tests/unit/test_map_render tests/unit/test_map_render.c mods/nd/ux/nd_lamina.c -lxylem
./tests/unit/test_map_render
rm -f tests/unit/test_map_render
