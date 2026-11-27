REPO_ROOT ?= $(shell cd ../.. && pwd)

CC ?= cc
CFLAGS ?= -O2 -g -Wall -Wextra

FT_CFLAGS := $(shell pkg-config --cflags freetype2 2>/dev/null || echo -I/usr/include/freetype2)
FT_LDLIBS := $(shell pkg-config --libs freetype2 2>/dev/null || echo -lfreetype)

BUILD_DIR = build
TARGET = nd-native

ND_UX_DIR = $(REPO_ROOT)/mods/nd/ux
ND_SEVEN_DIR = $(REPO_ROOT)/mods/nd/seven

CX ?= $(shell if [ -x "$(REPO_ROOT)/external/libbud/bin/cx" ]; then echo "$(REPO_ROOT)/external/libbud/bin/cx"; else command -v cx 2>/dev/null || echo cx; fi)

ND_UX_CX = \
	$(ND_UX_DIR)/nd_settings_ui.cx \
	$(ND_UX_DIR)/nd_hud_ui.cx \
	$(ND_UX_DIR)/nd_shell_ui.cx \
	$(ND_UX_DIR)/nd_map_ui.cx \
	$(ND_UX_DIR)/nd_dock_ui.cx \
	$(ND_UX_DIR)/glue/attr_ui.cx \
	$(ND_UX_DIR)/glue/equip_ui.cx

ND_UX_SRCS = \
	$(ND_UX_DIR)/nd_ui.c \
	$(ND_UX_DIR)/nd_win.c \
	$(ND_UX_DIR)/nd_state_io.c \
	$(ND_UX_DIR)/nd_scene.c \
	$(ND_UX_DIR)/nd_lamina.c \
	$(ND_UX_DIR)/glue/spell_ui.c \
	$(ND_UX_DIR)/glue/fight_ui.c \
	$(ND_UX_DIR)/glue/mortal_ui.c \
	$(ND_UX_CX:.cx=.cx.c)

ND_SEVEN_SRCS = \
	$(ND_SEVEN_DIR)/nd_seven.c \
	$(ND_SEVEN_DIR)/nd_seven_atlas.c \
	$(ND_SEVEN_DIR)/nd_seven_atlas_draw.c \
	$(ND_SEVEN_DIR)/nd_seven_mesh3d.c \
	$(ND_SEVEN_DIR)/nd_seven_ground.c \
	$(ND_SEVEN_DIR)/nd_seven_motion.c \
	$(ND_SEVEN_DIR)/nd_seven_objects.c \
	$(ND_SEVEN_DIR)/nd_seven_gl_shaders.c \
	$(ND_SEVEN_DIR)/nd_seven_gl_math.c \
	$(ND_SEVEN_DIR)/nd_seven_gl_billboard.c \
	$(ND_SEVEN_DIR)/nd_seven_gl_occ.c \
	$(ND_SEVEN_DIR)/nd_seven_gl_clouds.c \
	$(ND_SEVEN_DIR)/nd_seven_gl.c

EXTRA_SRCS = \
	$(REPO_ROOT)/mods/common/ux/site_ui.c \
	$(REPO_ROOT)/external/libareole/src/areole.c \
	$(REPO_ROOT)/external/libcorolla/src/corolla.c \
	$(REPO_ROOT)/external/liblamina/src/particle.c \
	$(REPO_ROOT)/external/nd-mob-gl/src/libnd-mob-gl.c

INCS = \
	-I$(REPO_ROOT)/lib/bud-lamina \
	-I$(REPO_ROOT)/mods/common \
	-I$(REPO_ROOT)/mods/common/ux \
	-I$(REPO_ROOT)/mods/nd/ux \
	-I$(REPO_ROOT)/mods/nd/seven \
	-I$(REPO_ROOT)/include \
	-I$(REPO_ROOT)/external/libbud/include \
	-I$(REPO_ROOT)/external/libhyle/include \
	-I$(REPO_ROOT)/external/libhyle-bud/include \
	-I$(REPO_ROOT)/external/libcorm/include \
	-I$(REPO_ROOT)/external/libstele/include \
	-I$(REPO_ROOT)/external/libxylem/include \
	-I$(REPO_ROOT)/external/liblamina/include \
	-I$(REPO_ROOT)/external/axil/include \
	-I$(REPO_ROOT)/external/axil-nd/include \
	-I$(REPO_ROOT)/external/axil-tty/include \
	-I$(REPO_ROOT)/external/nd-seven/include \
	-I$(REPO_ROOT)/external/nd-plant/include \
	-I$(REPO_ROOT)/external/nd-mob/include \
	-I$(REPO_ROOT)/external/nd-mob-gl/include \
	-I$(REPO_ROOT)/external/nd-stone/include \
	-I$(REPO_ROOT)/external/nd-attr/include \
	-I$(REPO_ROOT)/external/nd-equip/include \
	-I$(REPO_ROOT)/external/nd-spell/include \
	-I$(REPO_ROOT)/external/nd-fight/include \
	-I$(REPO_ROOT)/external/nd-mortal/include \
	-I$(REPO_ROOT)/external/nd-world/include \
	-I$(REPO_ROOT)/external/libchaff/include \
	-I$(REPO_ROOT)/external/libislet/include \
	-I$(REPO_ROOT)/external/libareole/include \
	-I$(REPO_ROOT)/external/libcorolla/include \
	$(FT_CFLAGS)

DEFS = -DLAM_WEBGL_NATIVE -DLAM_NATIVE_CLIENT -DAXIL_WS_NATIVE -DAXIL_WS_TLS -DND_FIELD_MEMO_EXTERN

LIB_DIRS = \
	$(REPO_ROOT)/external/liblamina/lib \
	$(REPO_ROOT)/external/libbud/lib \
	$(REPO_ROOT)/external/libhyle-bud/lib \
	$(REPO_ROOT)/external/libcorm/lib \
	$(REPO_ROOT)/external/libstele/lib \
	$(REPO_ROOT)/external/libxylem/lib

LDFLAGS = $(foreach d,$(LIB_DIRS),-L$(d) -Wl,-rpath,$(d))
LDLIBS = -llamina -lbud -lhyle-bud -lcorm -lstele -lxylem -lxxhash -lpng $(FT_LDLIBS) \
	-lssl -lcrypto -lGL -lGLEW -lglfw -lEGL -lX11 -lm

BUD_LAMINA_LIB = $(REPO_ROOT)/lib/bud-lamina/libbud-lamina.a

APP_OBJ = $(BUILD_DIR)/main.o
UX_OBJS = $(patsubst $(ND_UX_DIR)/%.c,$(BUILD_DIR)/ux/%.o,$(ND_UX_SRCS))
SEVEN_OBJS = $(patsubst $(ND_SEVEN_DIR)/%.c,$(BUILD_DIR)/seven/%.o,$(ND_SEVEN_SRCS))
EXTRA_OBJS = \
	$(BUILD_DIR)/extra/site_ui.o \
	$(BUILD_DIR)/extra/areole.o \
	$(BUILD_DIR)/extra/corolla.o \
	$(BUILD_DIR)/extra/particle.o \
	$(BUILD_DIR)/extra/libnd-mob-gl.o

ALL_OBJS = $(APP_OBJ) $(UX_OBJS) $(SEVEN_OBJS) $(EXTRA_OBJS)
DEP_FLAGS = -MMD -MP

.PHONY: all clean bud-lamina

all: $(TARGET)

bud-lamina:
	$(MAKE) -C $(REPO_ROOT)/lib/bud-lamina

$(BUD_LAMINA_LIB): bud-lamina

$(TARGET): $(ALL_OBJS) $(BUD_LAMINA_LIB)
	$(CC) $(CFLAGS) -o $@ $(ALL_OBJS) $(BUD_LAMINA_LIB) $(LDFLAGS) $(LDLIBS)

.SUFFIXES:

$(ND_UX_CX:.cx=.cx.c): %.cx.c: %.cx
	$(CX) $< -o $@

$(BUILD_DIR)/main.o: main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/ux/%.o: $(ND_UX_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter -Wno-unused-function $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/seven/%.o: $(ND_SEVEN_DIR)/%.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter -Wno-unused-function $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/extra/site_ui.o: $(REPO_ROOT)/mods/common/ux/site_ui.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter -Wno-unused-function $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/extra/areole.o: $(REPO_ROOT)/external/libareole/src/areole.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/extra/corolla.o: $(REPO_ROOT)/external/libcorolla/src/corolla.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/extra/particle.o: $(REPO_ROOT)/external/liblamina/src/particle.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter $(DEFS) $(INCS) -c -o $@ $<

$(BUILD_DIR)/extra/libnd-mob-gl.o: $(REPO_ROOT)/external/nd-mob-gl/src/libnd-mob-gl.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(DEP_FLAGS) -Wno-unused-parameter $(DEFS) $(INCS) -c -o $@ $<

-include $(ALL_OBJS:.o=.d)

clean:
	rm -rf $(BUILD_DIR) $(TARGET)
