HYLE_DIR = $(REPO_ROOT)/external/hyle
SRC = source.c
CFLAGS = -I$(REPO_ROOT)/external/libxylem/include
EXTRA_CFLAGS += -I$(HYLE_DIR)/include -I$(HYLE_DIR)/c/libhyle-bud/include -I$(HYLE_DIR)/c/libhyle-source/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib -L$(HYLE_DIR)/c/libhyle-source/lib -lhyle-source -Wl,-rpath,$(HYLE_DIR)/c/libhyle-source/lib -L$(REPO_ROOT)/external/axil-hyle/lib -laxil-hyle -Wl,-rpath,$(REPO_ROOT)/external/axil-hyle/lib -ljson-c -laxil-auth
include ../../build.mk
