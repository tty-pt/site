HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common -I../song/lib/transp
EXTRA_LDLIBS += -lbud -laxil-auth -ljson-c
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/songbook_detail.wasm
songbook_detail-src = ux/detail.c
songbook_detail-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/song/lib/transp

include ../../build.mk
