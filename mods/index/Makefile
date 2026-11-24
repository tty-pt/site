HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
HYLE_SOURCE_DIR = $(REPO_ROOT)/external/libhyle-source
EXTRA_CFLAGS += -I../common -I$(HYLE_BUD_DIR)/include -I$(HYLE_SOURCE_DIR)/include
EXTRA_LDLIBS += -lbud
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -Wl,--no-as-needed -lhyle-source -Wl,--as-needed
EXTRA_LDLIBS-OpenBSD += -liconv

include ../../external/libhyle-bud/hyle-bud-wasm.mk
WASM_TARGETS = $(WASM_PATH)/list.wasm $(WASM_PATH)/list_fe.wasm
list-src = ux/list_fe.c $(HYLE_BUD_WASM_SRC) $(REPO_ROOT)/mods/common/site_wasm_app.c
list-cflags = $(HYLE_BUD_WASM_CFLAGS) -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/index/include

SRC = index.c ux/home.cx

include ../../build.mk

$(WASM_PATH)/list.wasm: $(LIST_UX_DEPS)

$(WASM_PATH)/list_fe.wasm: $(WASM_PATH)/list.wasm
	ln -sf list.wasm $@

$(TARGET): $(LIST_UX_DEPS)
