HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
HYLE_SOURCE_DIR = $(REPO_ROOT)/external/libhyle-source
SRC = source.c pick.c
CFLAGS = -I$(REPO_ROOT)/external/libxylem/include
EXTRA_CFLAGS += -I$(HYLE_DIR)/include -I$(HYLE_BUD_DIR)/include -I$(HYLE_SOURCE_DIR)/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib -L$(HYLE_SOURCE_DIR)/lib -lhyle-source -Wl,-rpath,$(HYLE_SOURCE_DIR)/lib -L$(REPO_ROOT)/external/axil-hyle/lib -laxil-hyle -Wl,-rpath,$(REPO_ROOT)/external/axil-hyle/lib -L$(HYLE_BUD_DIR)/lib -lhyle-bud -Wl,-rpath,$(HYLE_BUD_DIR)/lib -ljson-c -laxil-auth
include ../../build.mk
