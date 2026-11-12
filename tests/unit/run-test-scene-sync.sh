#!/bin/sh
set -eu
gcc -Iexternal/libareole/include -Iexternal/liblamina/include -Iexternal/libbud/include -Iexternal/axil-nd/include -Iexternal/nd-seven/include -Iexternal/libchaff/include -Imods/nd/ux -Iinclude -o tests/unit/test_scene_sync tests/unit/test_scene_sync.c mods/nd/ux/nd_scene.c mods/nd/ux/nd_lamina.c mods/nd/ux/nd_seven.c mods/nd/ux/nd_seven_atlas_draw.c mods/nd/ux/nd_seven_ground.c mods/nd/ux/nd_seven_motion.c mods/nd/ux/nd_seven_objects.c external/libareole/src/areole.c external/liblamina/src/particle.c -lxylem -lxxhash -lm
./tests/unit/test_scene_sync
rm -f tests/unit/test_scene_sync
