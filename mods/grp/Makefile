HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common -I../song/lib/transp -I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include
EXTRA_LDLIBS += -lbud -laxil-auth
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib
LIST_UX_DEPS = ../index/ux/list.c ../index/ux/list_state.h \
	../index/ux/list_query.c ../index/ux/list_filters.c \
	../index/ux/list_layout.c ../index/ux/list_json.c \
	../index/ux/list_render.c

include ../../build.mk

$(TARGET): $(LIST_UX_DEPS) ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
