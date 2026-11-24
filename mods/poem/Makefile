HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
EXTRA_CFLAGS += -I../common -I$(HYLE_BUD_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -lhyle-bud

SRC = poem.c ux/detail.cx

include ../../build.mk
