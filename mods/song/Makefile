HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
TRANSP_DIR = $(REPO_ROOT)/external/libtransp
EXTRA_CFLAGS += -I../common -I$(TRANSP_DIR)/include -I$(HYLE_BUD_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -ltransp -ljson-c
EXTRA_LDLIBS += -lhyle
EXTRA_LDLIBS += -lhyle-bud

WASM_TARGETS = $(WASM_PATH)/song.wasm $(WASM_PATH)/song_detail.wasm
song-src = ux/detail.c $(REPO_ROOT)/external/libtransp/src/music.c $(REPO_ROOT)/mods/common/site_wasm_app.c
song-cflags = -I$(REPO_ROOT)/external/libtransp/include -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/song/include
song_detail-src = $(song-src)
song_detail-cflags = $(song-cflags)

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
