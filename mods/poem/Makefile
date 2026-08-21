EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -laxil-auth

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h
