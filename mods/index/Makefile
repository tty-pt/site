HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
HYLE_SOURCE_DIR = $(REPO_ROOT)/external/libhyle-source
EXTRA_CFLAGS += -I../common -I$(HYLE_BUD_DIR)/include -I$(HYLE_SOURCE_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -L$(HYLE_BUD_DIR)/lib -lhyle-bud -Wl,-rpath,$(HYLE_BUD_DIR)/lib
EXTRA_LDLIBS += -Wl,--no-as-needed -L$(HYLE_SOURCE_DIR)/lib -lhyle-source -Wl,--as-needed -Wl,-rpath,$(HYLE_SOURCE_DIR)/lib
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -laxil-auth

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/list.wasm
list-src = ux/list_fe.c

include ../../external/libhyle-bud/hyle-bud-wasm.mk
list-src += $(HYLE_BUD_WASM_SRC)
LIST_UX_DEPS = ux/list.c ux/list_state.h ux/list_query.c ux/list_filters.c \
	ux/list_layout.c ux/list_json.c ux/list_render.c

include ../../build.mk

list-cflags = -I$(REPO_ROOT)/mods/common $(HYLE_BUD_WASM_CFLAGS)

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h

$(WASM_PATH)/list.wasm: $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
