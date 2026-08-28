HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
TRANSP_DIR := $(shell cd ../.. && pwd)/external/libtransp
EXTRA_CFLAGS += -I../common -I$(TRANSP_DIR)/include -I$(HYLE_DIR)/c/libhyle-bud/include
EXTRA_LDLIBS += -lbud -laxil-auth -ljson-c -L$(TRANSP_DIR)/lib -ltransp -Wl,-rpath,$(TRANSP_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/gig_detail.wasm
gig_detail-src = ux/detail.c $(REPO_ROOT)/external/libtransp/src/music.c

include ../../external/hyle/c/libhyle-bud/hyle-bud-wasm.mk
gig_detail-src += $(HYLE_BUD_WASM_SRC)
LIST_UX_DEPS = ../index/ux/list.c ../index/ux/list_state.h \
	../index/ux/list_query.c ../index/ux/list_filters.c \
	../index/ux/list_layout.c ../index/ux/list_json.c \
	../index/ux/list_render.c

include ../../build.mk

gig_detail-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/external/libtransp/include $(HYLE_BUD_WASM_CFLAGS)

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h

$(WASM_PATH)/gig_detail.wasm: $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
