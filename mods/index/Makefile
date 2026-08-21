HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -laxil-auth

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/list.wasm
list-src = ux/list_fe.c \
	../../external/hyle/c/libhyle-bud/src/filter.c \
	../../external/hyle/c/libhyle-bud/src/table.c
list-cflags = -I$(REPO_ROOT)/mods/common \
	-I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include
LIST_UX_DEPS = ux/list.c ux/list_state.h ux/list_query.c ux/list_filters.c \
	ux/list_layout.c ux/list_json.c ux/list_render.c

include ../../build.mk

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h

$(WASM_PATH)/list.wasm: $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
