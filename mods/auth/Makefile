EXTRA_CFLAGS += -I../common -I$(shell cd .. && pwd)/../external/hyle/c/libhyle-bud/include
EXTRA_LDLIBS := -laxil-auth -lbud
EXTRA_LDLIBS += -L$(shell cd .. && pwd)/../external/hyle/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(shell cd .. && pwd)/../external/hyle/c/libhyle-bud/lib

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h ../common/viewer_zoom.h


test:
	./test.sh
