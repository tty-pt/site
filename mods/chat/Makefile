EXTRA_CFLAGS += -I../common -I/usr/include/json-c
EXTRA_LDLIBS += -lbud -ljson-c -lpthread

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h
