HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
HYLE_SOURCE_DIR = $(REPO_ROOT)/external/libhyle-source
SRC = source.c pick.c
CFLAGS = -I$(REPO_ROOT)/external/libxylem/include
EXTRA_CFLAGS += -I$(HYLE_DIR)/include -I$(HYLE_BUD_DIR)/include -I$(HYLE_SOURCE_DIR)/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -lhyle -lhyle-source -laxil-hyle -lhyle-bud -ljson-c
include ../../build.mk
