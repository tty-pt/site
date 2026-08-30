HYLE_DIR = $(REPO_ROOT)/external/hyle
TRANSP_DIR = $(REPO_ROOT)/external/libtransp
EXTRA_CFLAGS += -I../common -I$(TRANSP_DIR)/include -I$(HYLE_DIR)/c/libhyle-bud/include
EXTRA_LDLIBS += -lbud -laxil-auth
EXTRA_LDLIBS += -L$(TRANSP_DIR)/lib -ltransp -Wl,-rpath,$(TRANSP_DIR)/lib -ljson-c
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/song_detail.wasm
song_detail-src = ux/detail.c $(REPO_ROOT)/external/libtransp/src/music.c
song_detail-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/external/libtransp/include

include ../../external/hyle/c/libhyle-bud/hyle-bud-wasm.mk
song_detail-src += $(HYLE_BUD_WASM_SRC)
song_detail-cflags += $(HYLE_BUD_WASM_CFLAGS)

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h

$(WASM_PATH)/song_detail.wasm: ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
