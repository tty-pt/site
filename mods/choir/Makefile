HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS += -lbud -laxil-auth
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib

include ../../build.mk
