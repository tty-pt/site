HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib
EXTRA_LDLIBS-OpenBSD += -liconv
EXTRA_LDLIBS += -laxil-auth

include ../../build.mk
