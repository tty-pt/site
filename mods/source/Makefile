HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
SRC = source.c source-http.c
CFLAGS = -I$(shell cd ../.. && pwd)/external/libxylem/include
EXTRA_CFLAGS := -I$(HYLE_DIR)/include -I$(HYLE_DIR)/c/libhyle-bud/include -I$(HYLE_DIR)/c/libhyle-source/include
EXTRA_LDLIBS := -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib -L$(HYLE_DIR)/c/libhyle-source/lib -lhyle-source -Wl,-rpath,$(HYLE_DIR)/c/libhyle-source/lib -ljson-c -laxil-auth
include ../../build.mk
