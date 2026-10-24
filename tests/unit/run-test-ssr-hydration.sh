#!/bin/sh
set -eu

REPO_ROOT="$(pwd)"

gcc -g -O0 \
	-I"$REPO_ROOT/include" \
	-I"$REPO_ROOT/external/libbud/include" \
	-I"$REPO_ROOT/external/axil-nd/include" \
	-I"$REPO_ROOT/external/axil-tty/include" \
	-I"$REPO_ROOT/external/liblamina/include" \
	-I"$REPO_ROOT/external/nd-attr/include" \
	-I"$REPO_ROOT/external/nd-equip/include" \
	-I"$REPO_ROOT/external/nd-spell/include" \
	-I"$REPO_ROOT/external/nd-fight/include" \
	-I"$REPO_ROOT/external/nd-mortal/include" \
	-I"$REPO_ROOT/external/libxylem/include" \
	-I"$REPO_ROOT/mods/nd/ux" \
	-o tests/unit/test_ssr_hydration \
	tests/unit/test_ssr_hydration.c \
	mods/nd/ux/nd_ui.c \
	mods/nd/ux/nd_scene.c \
	mods/nd/ux/nd_webgl.c \
	mods/nd/ux/nd_lamina.c \
	external/nd-equip/ux/equip_ui.c \
	external/nd-attr/ux/attr_ui.c \
	external/nd-spell/ux/spell_ui.c \
	external/nd-fight/ux/fight_ui.c \
	external/nd-mortal/ux/mortal_ui.c \
	-lbud -laxil -lxylem -lhyle-bud -lm

./tests/unit/test_ssr_hydration
rm -f tests/unit/test_ssr_hydration
