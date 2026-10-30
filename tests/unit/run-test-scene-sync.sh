#!/bin/sh
set -eu
gcc -Iexternal/libbud/include -Iexternal/axil-nd/include -Iexternal/nd-seven/include -Imods/nd/ux -Iinclude -o tests/unit/test_scene_sync tests/unit/test_scene_sync.c mods/nd/ux/nd_scene.c mods/nd/ux/nd_lamina.c mods/nd/ux/nd_seven.c -lxylem
./tests/unit/test_scene_sync
rm -f tests/unit/test_scene_sync
