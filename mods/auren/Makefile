EXTRA_CFLAGS += -I../common -I/usr/include/json-c
EXTRA_LDLIBS += -lbud -ljson-c -lpthread
MODULE_EXTRA_SRC = ux/auren_ui.c

WASM_TARGETS = $(WASM_PATH)/auren.wasm $(WASM_PATH)/auren_fe.wasm
auren-src = ux/auren_fe.c ux/auren_ui.c $(REPO_ROOT)/mods/common/site_wasm_app.c
auren-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux

include ../../build.mk

$(WASM_PATH)/auren_fe.wasm: $(WASM_PATH)/auren.wasm
	ln -sf auren.wasm $@
