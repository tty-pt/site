HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
HYLE_SOURCE_DIR = $(REPO_ROOT)/external/libhyle-source
EXTRA_CFLAGS += -I../common -I$(HYLE_BUD_DIR)/include -I$(HYLE_SOURCE_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -Wl,--no-as-needed -lhyle-source -Wl,--as-needed
EXTRA_LDLIBS-OpenBSD += -liconv

include ../../external/libhyle-bud/hyle-bud-wasm.mk
WASM_TARGETS = $(WASM_PATH)/list.wasm $(WASM_PATH)/list_fe.wasm
list-src = ux/list_fe.c $(HYLE_BUD_WASM_SRC) $(REPO_ROOT)/mods/common/site_wasm_app.c
list-cflags = $(HYLE_BUD_WASM_CFLAGS) -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/index/include

LIST_UX_DEPS = ux/list.c ux/list_state.h ux/list_query.c ux/list_filters.c \
	ux/list_layout.c ux/list_json.c ux/list_render.c

include ../../build.mk

$(WASM_PATH)/list_fe.wasm: $(WASM_PATH)/list.wasm
	ln -sf list.wasm $@

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
