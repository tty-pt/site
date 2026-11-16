#!/bin/sh
set -eu
gcc -Iexternal/liblamina/include -Iexternal/nd-world/include -Iexternal/libareole/include -Iexternal/libbud/include -Iexternal/axil-nd/include -Iexternal/nd-seven/include -Iexternal/libchaff/include -Imods/nd/ux -Iinclude -o tests/unit/test_map_render tests/unit/test_map_render.c mods/nd/ux/nd_lamina.c mods/nd/ux/nd_seven.c mods/nd/ux/nd_seven_atlas_draw.c mods/nd/ux/nd_seven_ground.c mods/nd/ux/nd_seven_motion.c mods/nd/ux/nd_seven_objects.c external/libareole/src/areole.c external/liblamina/src/particle.c -lxylem -lxxhash -lm
./tests/unit/test_map_render
rm -f tests/unit/test_map_render
