HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
TRANSP_DIR = $(REPO_ROOT)/external/libtransp
EXTRA_CFLAGS += -I../common -I$(TRANSP_DIR)/include -I$(HYLE_BUD_DIR)/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -lbud -ljson-c -ltransp
EXTRA_LDLIBS += -lhyle
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -laxil-hyle

include ../../external/libhyle-bud/hyle-bud-wasm.mk
WASM_TARGETS = $(WASM_PATH)/gig.wasm $(WASM_PATH)/gig_detail.wasm
gig-src = ux/detail.c $(HYLE_BUD_WASM_SRC) $(REPO_ROOT)/mods/common/site_wasm_app.c
gig-cflags = $(HYLE_BUD_WASM_CFLAGS) -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/gig/include -I$(REPO_ROOT)/external/libtransp/include
gig_detail-src = $(gig-src)
gig_detail-cflags = $(gig-cflags)

LIST_UX_DEPS = ../index/ux/list.c ../index/ux/list_state.h \
	../index/ux/list_query.c ../index/ux/list_filters.c \
	../index/ux/list_layout.c ../index/ux/list_json.c \
	../index/ux/list_render.c

include ../../build.mk

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
