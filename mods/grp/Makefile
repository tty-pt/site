HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
PITCH_DIR = $(REPO_ROOT)/external/libpitch
EXTRA_CFLAGS += -I../common -I$(PITCH_DIR)/include -I$(HYLE_BUD_DIR)/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -lbud -lpitch
EXTRA_LDLIBS += -lhyle
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -laxil-hyle
include ../../build.mk

$(TARGET): $(LIST_UX_DEPS) ux/detail.cx.c
