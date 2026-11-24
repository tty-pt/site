HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
PITCH_DIR = $(REPO_ROOT)/external/libpitch
EXTRA_CFLAGS += -I../common -I$(PITCH_DIR)/include -I$(HYLE_BUD_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -lpitch -ljson-c
EXTRA_LDLIBS += -lhyle
EXTRA_LDLIBS += -lhyle-bud

include ../../external/libhyle-bud/hyle-bud-wasm.mk
WASM_TARGETS = $(WASM_PATH)/song.wasm $(WASM_PATH)/song_detail.wasm
song-src = ux/detail.cx $(HYLE_BUD_WASM_SRC) $(REPO_ROOT)/external/libpitch/src/music.c $(REPO_ROOT)/mods/common/site_wasm_app.c
song-cflags = $(HYLE_BUD_WASM_CFLAGS) -I$(REPO_ROOT)/external/libpitch/include -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/song/include

include ../../build.mk

$(TARGET): ux/detail.cx.c

$(WASM_PATH)/song_detail.wasm: $(WASM_PATH)/song.wasm
	ln -sf song.wasm $@
