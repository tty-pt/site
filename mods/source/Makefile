HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
SRC = source.c source-http.c
CFLAGS = -I$(shell cd ../.. && pwd)/external/libndx/include
EXTRA_CFLAGS := -I$(HYLE_DIR)/include
EXTRA_LDLIBS := -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib
include ../../build.mk
