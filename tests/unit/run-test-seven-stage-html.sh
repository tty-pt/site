#!/bin/sh
set -eu

REPO_ROOT="$(pwd)"
CX_BIN="$REPO_ROOT/external/libbud/bin/cx"
for cx_src in \
	mods/nd/ux/nd_settings_ui.cx \
	mods/nd/ux/nd_hud_ui.cx \
	mods/nd/ux/nd_shell_ui.cx \
	mods/nd/ux/nd_map_ui.cx \
	mods/nd/ux/nd_dock_ui.cx \
	mods/nd/ux/glue/equip_ui.cx \
	mods/nd/ux/glue/attr_ui.cx; do
	[ -f "${cx_src}.c" ] && [ ! "$cx_src" -nt "${cx_src}.c" ] || "$CX_BIN" "$cx_src" -o "${cx_src}.c"
done

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
	-I"$REPO_ROOT/external/libareole/include" \
	-I"$REPO_ROOT/external/libxylem/include" \
	-I"$REPO_ROOT/external/libchaff/include" \
	-I"$REPO_ROOT/external/nd-world/include" \
	-I"$REPO_ROOT/mods/common/ux" \
	-I"$REPO_ROOT/mods/nd/ux" \
	-o tests/unit/test_seven_stage_html \
	tests/unit/test_seven_stage_html.c \
	mods/common/ux/site_ui.c \
	mods/nd/ux/nd_ui.c \
	mods/nd/ux/nd_win.c \
	mods/nd/ux/nd_settings_ui.cx.c \
	mods/nd/ux/nd_hud_ui.cx.c \
	mods/nd/ux/nd_shell_ui.cx.c \
	mods/nd/ux/nd_map_ui.cx.c \
	mods/nd/ux/nd_dock_ui.cx.c \
	mods/nd/ux/nd_state_io.c \
	mods/nd/ux/nd_scene.c \
	mods/nd/ux/nd_lamina.c \
	mods/nd/ux/nd_seven.c \
	external/libareole/src/areole.c external/liblamina/src/particle.c \
	mods/nd/ux/nd_seven_atlas_draw.c \
	mods/nd/ux/nd_seven_ground.c \
	mods/nd/ux/nd_seven_motion.c \
	mods/nd/ux/nd_seven_objects.c \
	mods/nd/ux/nd_seven_gl_shaders.c mods/nd/ux/nd_seven_gl_math.c mods/nd/ux/nd_seven_gl_billboard.c mods/nd/ux/nd_seven_gl_occ.c mods/nd/ux/nd_seven_gl_clouds.c mods/nd/ux/nd_seven_gl.c \
	mods/nd/ux/glue/equip_ui.cx.c \
	mods/nd/ux/glue/attr_ui.cx.c \
	mods/nd/ux/glue/spell_ui.c \
	mods/nd/ux/glue/fight_ui.c \
	mods/nd/ux/glue/mortal_ui.c \
	-L"$REPO_ROOT/external/axil/lib" \
	-L"$REPO_ROOT/external/libstele/lib" \
	-lbud -laxil -lxylem -lhyle-bud -lstele -lxxhash -lm

LD_LIBRARY_PATH="$REPO_ROOT/external/axil/lib:$REPO_ROOT/external/libstele/lib:${LD_LIBRARY_PATH:-}" ./tests/unit/test_seven_stage_html
rm -f tests/unit/test_seven_stage_html
