REPO_ROOT != cd ../.. && pwd
WASM_PATH ?= $(REPO_ROOT)/htdocs

TARGET = nd.so

ND_UX_MODULES = \
	ux/glue/attr_ui.cx \
	ux/glue/equip_ui.cx \
	ux/glue/spell_ui.c \
	ux/glue/fight_ui.c \
	ux/glue/mortal_ui.c

ND_UX_PARTS = \
	ux/nd_win.c \
	ux/nd_settings_ui.cx \
	ux/nd_hud_ui.cx \
	ux/nd_shell_ui.cx \
	ux/nd_map_ui.cx \
	ux/nd_dock_ui.cx \
	ux/nd_state_io.c

ND_SEVEN_PARTS = \
	seven/nd_seven.c \
	seven/nd_seven_atlas.c \
	seven/nd_seven_atlas_draw.c \
	seven/nd_seven_mesh3d.c \
	seven/nd_seven_ground.c \
	seven/nd_seven_motion.c \
	seven/nd_seven_objects.c \
	seven/nd_seven_gl_shaders.c \
	seven/nd_seven_gl_math.c \
	seven/nd_seven_gl_billboard.c \
	seven/nd_seven_gl_occ.c \
	seven/nd_seven_gl_clouds.c \
	seven/nd_seven_gl.c

SRC = nd.c ux/nd_ui.c $(ND_UX_PARTS) $(ND_SEVEN_PARTS) ux/nd_scene.c ux/nd_lamina.c $(ND_UX_MODULES) $(REPO_ROOT)/external/libareole/src/areole.c $(REPO_ROOT)/external/libcorolla/src/corolla.c $(REPO_ROOT)/external/liblamina/src/particle.c
EXTRA_CFLAGS += -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/include -I$(REPO_ROOT)/external/axil/include -I$(REPO_ROOT)/external/axil-nd/include -I$(REPO_ROOT)/external/nd-seven/include -I$(REPO_ROOT)/external/nd-plant/include -I$(REPO_ROOT)/external/nd-stone/include -I$(REPO_ROOT)/external/axil-tty/include -I$(REPO_ROOT)/external/liblamina/include -I$(REPO_ROOT)/external/nd-attr/include -I$(REPO_ROOT)/external/nd-equip/include -I$(REPO_ROOT)/external/nd-spell/include -I$(REPO_ROOT)/external/nd-fight/include -I$(REPO_ROOT)/external/nd-mortal/include -I$(REPO_ROOT)/external/libxylem/include -I$(REPO_ROOT)/mods/nd/ux -I$(REPO_ROOT)/mods/nd/seven -I$(REPO_ROOT)/external/nd-world/include -I$(REPO_ROOT)/external/libchaff/include -I$(REPO_ROOT)/external/libislet/include -I$(REPO_ROOT)/external/libareole/include -I$(REPO_ROOT)/external/libcorolla/include
EXTRA_LDLIBS += -lbud -laxil -lxylem -lhyle-bud -lm

WASM_TARGETS = $(WASM_PATH)/nd.wasm
nd-src = ux/nd_ui.c $(ND_UX_PARTS) $(ND_SEVEN_PARTS) ux/nd_scene.c ux/nd_lamina.c $(ND_UX_MODULES) $(REPO_ROOT)/external/libxylem/src/libxylem-wasm.c $(REPO_ROOT)/mods/common/site_wasm_app.c $(REPO_ROOT)/external/libareole/src/areole.c $(REPO_ROOT)/external/libcorolla/src/corolla.c $(REPO_ROOT)/external/liblamina/src/particle.c
nd-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/include -I$(REPO_ROOT)/external/axil/include -I$(REPO_ROOT)/external/axil-nd/include -I$(REPO_ROOT)/external/nd-seven/include -I$(REPO_ROOT)/external/nd-plant/include -I$(REPO_ROOT)/external/nd-stone/include -I$(REPO_ROOT)/external/axil-tty/include -I$(REPO_ROOT)/external/liblamina/include -I$(REPO_ROOT)/external/nd-attr/include -I$(REPO_ROOT)/external/nd-equip/include -I$(REPO_ROOT)/external/nd-spell/include -I$(REPO_ROOT)/external/nd-fight/include -I$(REPO_ROOT)/external/nd-mortal/include -I$(REPO_ROOT)/external/libxylem/include -I$(REPO_ROOT)/mods/nd/ux -I$(REPO_ROOT)/mods/nd/seven -I$(REPO_ROOT)/external/nd-world/include -I$(REPO_ROOT)/external/libchaff/include -I$(REPO_ROOT)/external/libislet/include -I$(REPO_ROOT)/external/libareole/include -I$(REPO_ROOT)/external/libcorolla/include
nd-wasm-allowed = $(REPO_ROOT)/external/axil/wasm-allowed-imports.lst $(REPO_ROOT)/external/axil-tty/wasm-allowed-imports.lst $(REPO_ROOT)/external/liblamina/wasm-allowed-imports.lst
nd-opt-cflags = -O3 -msimd128 -DXXH_NO_XXH3
nd-ldflags = -Wl,-z,stack-size=524288 -Wl,--stack-first

include ../../external/libhyle-bud/hyle-bud-wasm.mk
nd-src += $(HYLE_BUD_WASM_SRC)
nd-cflags += $(HYLE_BUD_WASM_CFLAGS)

include ../../build.mk

ND_UX_HDRS = ux/nd_ui.h ux/nd_ui_internal.h ux/nd_lamina.h
ND_SEVEN_HDRS = seven/nd_seven.h seven/nd_seven_atlas.h seven/nd_seven_atlas_draw.h seven/nd_seven_gl.h seven/nd_seven_gl_internal.h seven/nd_seven_ground.h seven/nd_seven_mesh3d.h seven/nd_seven_motion.h seven/nd_seven_objects.h seven/nd_seven_shader.h

$(TARGET): $(ND_UX_HDRS) $(ND_SEVEN_HDRS) $(REPO_ROOT)/external/nd-world/include/nd/field.h

$(WASM_PATH)/nd.wasm: $(nd-src) $(ND_UX_HDRS) $(ND_SEVEN_HDRS) $(REPO_ROOT)/external/nd-seven/include/nd/seven-world.h $(REPO_ROOT)/external/nd-world/include/nd/field.h $(REPO_ROOT)/external/axil-nd/include/nd/bcp.h $(REPO_ROOT)/external/axil/include/ttypt/axil-ws.h $(REPO_ROOT)/external/liblamina/include/ttypt/lam.h
