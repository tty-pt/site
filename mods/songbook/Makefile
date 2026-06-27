EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS += -lbud -laxil-auth -ljson-c -lhyle

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/songbook_detail.wasm
songbook_detail-src = ux/detail.c
songbook_detail-cflags = -I$(REPO_ROOT)/mods/common

include ../../build.mk
