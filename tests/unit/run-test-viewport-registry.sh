#!/bin/sh
set -eu

REPO_ROOT="$(pwd)"

gcc -g -O0 \
	-I"$REPO_ROOT/include" \
	-I"$REPO_ROOT/external/libbud/include" \
	-I"$REPO_ROOT/external/axil-nd/include" \
	-I"$REPO_ROOT/external/nd-seven/include" \
	-I"$REPO_ROOT/external/axil-tty/include" \
	-I"$REPO_ROOT/external/liblamina/include" \
	-I"$REPO_ROOT/external/nd-attr/include" \
	-I"$REPO_ROOT/external/nd-equip/include" \
	-I"$REPO_ROOT/external/nd-spell/include" \
	-I"$REPO_ROOT/external/nd-fight/include" \
	-I"$REPO_ROOT/external/nd-mortal/include" \
	-I"$REPO_ROOT/external/libxylem/include" \
	-I"$REPO_ROOT/mods/nd/ux" \
	-o tests/unit/test_viewport_registry \
	tests/unit/test_viewport_registry.c \
	mods/nd/ux/nd_ui.c \
	mods/nd/ux/nd_map_ui.c \
	mods/nd/ux/nd_dock_ui.c \
	mods/nd/ux/nd_state_io.c \
	mods/nd/ux/nd_scene.c \
	mods/nd/ux/nd_lamina.c \
	mods/nd/ux/nd_seven.c \
	mods/nd/ux/nd_seven_atlas_draw.c \
	mods/nd/ux/nd_seven_ground.c \
	mods/nd/ux/nd_seven_motion.c \
	mods/nd/ux/nd_seven_objects.c \
	mods/nd/ux/glue/equip_ui.c \
	mods/nd/ux/glue/attr_ui.c \
	mods/nd/ux/glue/spell_ui.c \
	mods/nd/ux/glue/fight_ui.c \
	mods/nd/ux/glue/mortal_ui.c \
	-lbud -laxil -lxylem -lhyle-bud -lm

./tests/unit/test_viewport_registry
rm -f tests/unit/test_viewport_registry
