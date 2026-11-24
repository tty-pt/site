EXTRA_CFLAGS += -I../common -I/usr/include/json-c
EXTRA_LDLIBS += -lbud -ljson-c -lpthread
SRC = auren.c ux/auren_ui.cx

WASM_TARGETS = $(WASM_PATH)/auren.wasm $(WASM_PATH)/auren_fe.wasm
auren-src = ux/auren_fe.c ux/auren_ui.cx $(REPO_ROOT)/mods/common/site_wasm_app.c
auren-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux

include ../../build.mk

$(WASM_PATH)/auren_fe.wasm: $(WASM_PATH)/auren.wasm
	ln -sf auren.wasm $@
