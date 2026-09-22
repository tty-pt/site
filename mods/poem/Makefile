HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
EXTRA_CFLAGS += -I../common -I$(HYLE_BUD_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -laxil-auth
EXTRA_LDLIBS += -L$(HYLE_BUD_DIR)/lib -lhyle-bud -Wl,-rpath,$(HYLE_BUD_DIR)/lib

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h
