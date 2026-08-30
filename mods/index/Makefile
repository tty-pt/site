HYLE_DIR = $(REPO_ROOT)/external/hyle
EXTRA_CFLAGS += -I../common -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include -I$(REPO_ROOT)/external/hyle/c/libhyle-source/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib
EXTRA_LDLIBS += -Wl,--no-as-needed -L$(HYLE_DIR)/c/libhyle-source/lib -lhyle-source -Wl,--as-needed -Wl,-rpath,$(HYLE_DIR)/c/libhyle-source/lib
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -laxil-auth

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/list.wasm
list-src = ux/list_fe.c

include ../../external/hyle/c/libhyle-bud/hyle-bud-wasm.mk
list-src += $(HYLE_BUD_WASM_SRC)
LIST_UX_DEPS = ux/list.c ux/list_state.h ux/list_query.c ux/list_filters.c \
	ux/list_layout.c ux/list_json.c ux/list_render.c

include ../../build.mk

list-cflags = -I$(REPO_ROOT)/mods/common $(HYLE_BUD_WASM_CFLAGS)

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h

$(WASM_PATH)/list.wasm: $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
