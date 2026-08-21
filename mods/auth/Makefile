EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS := -laxil-auth -lbud

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h


test:
	./test.sh
