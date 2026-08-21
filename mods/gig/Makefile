HYLE_DIR := $(shell cd ../.. && pwd)/external/hyle
EXTRA_CFLAGS += -I../common -I../song/lib/transp
EXTRA_LDLIBS += -lbud -laxil-auth -ljson-c
EXTRA_LDLIBS += -L$(HYLE_DIR)/lib -lhyle -Wl,-rpath,$(HYLE_DIR)/lib
EXTRA_LDLIBS += -L$(HYLE_DIR)/c/libhyle-bud/lib -lhyle-bud -Wl,-rpath,$(HYLE_DIR)/c/libhyle-bud/lib

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/gig_detail.wasm
gig_detail-src = ux/detail.c \
	../../external/hyle/c/libhyle-bud/src/filter.c \
	../../external/hyle/c/libhyle-bud/src/table.c
gig_detail-cflags = -I$(REPO_ROOT)/mods/common \
	-I$(REPO_ROOT)/mods/song/lib/transp \
	-I$(REPO_ROOT)/external/hyle/c/libhyle-bud/include

include ../../build.mk

$(TARGET) $(WASM_PATH)/gig_detail.wasm: ../common/ux/site_ui.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h
