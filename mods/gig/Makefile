HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common -I../song/lib/transp
EXTRA_LDLIBS += -lbud -laxil-auth -ljson-c
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/gig_detail.wasm
gig_detail-src = ux/detail.c \
	../../external/hyle/c/libhyle-bud/src/filter.c \
	../../external/hyle/c/libhyle-bud/src/table.c
gig_detail-cflags = -I$(REPO_ROOT)/mods/common \
	-I$(REPO_ROOT)/mods/song/lib/transp \
	-I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include
LIST_UX_DEPS = ../index/ux/list.c ../index/ux/list_state.h \
	../index/ux/list_query.c ../index/ux/list_filters.c \
	../index/ux/list_layout.c ../index/ux/list_json.c \
	../index/ux/list_render.c

include ../../build.mk

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h

$(WASM_PATH)/gig_detail.wasm: $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
