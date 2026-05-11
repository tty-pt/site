EXTRA_CFLAGS += -I../../external/bud/include -I../common
EXTRA_LDLIBS += -L../../external/bud/lib -lbud -laxil-auth -ljson-c

# WASM
WASM_TARGETS = $(WASM_PATH)/songbook_fe.wasm
songbook_fe-src = ux/all.c
songbook_fe-cflags = -I$(REPO_ROOT)/mods/common

all: $(TARGET)

include ../../build.mk
