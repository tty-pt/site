EXTRA_CFLAGS += -I../common -I/usr/include/json-c
EXTRA_LDLIBS += -lbud -ljson-c -lpthread

WASM_TARGETS = $(WASM_PATH)/chat.wasm $(WASM_PATH)/chat_fe.wasm
chat-src = ux/chat_fe.c ux/chat_ui.c $(REPO_ROOT)/mods/common/site_wasm_app.c
chat-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/chat/include

include ../../build.mk

$(WASM_PATH)/chat_fe.wasm: $(WASM_PATH)/chat.wasm
	ln -sf chat.wasm $@

