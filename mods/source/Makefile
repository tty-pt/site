HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
SRC = source.c source-http.c
CFLAGS = -I$(shell cd ../.. && pwd)/external/libndx/include
EXTRA_CFLAGS := -I$(HYLE_DIR)/include -I$(shell cd ../.. && pwd)/external/bud/include
EXTRA_LDLIBS := -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib -ljson-c
include ../../build.mk
