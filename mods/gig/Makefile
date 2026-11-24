HYLE_DIR = $(REPO_ROOT)/external/libhyle
HYLE_BUD_DIR = $(REPO_ROOT)/external/libhyle-bud
PITCH_DIR = $(REPO_ROOT)/external/libpitch
EXTRA_CFLAGS += -I../common -I$(PITCH_DIR)/include -I$(HYLE_BUD_DIR)/include -I$(REPO_ROOT)/external/axil-hyle/include
EXTRA_LDLIBS += -lbud -ljson-c -lpitch
EXTRA_LDLIBS += -lhyle
EXTRA_LDLIBS += -lhyle-bud
EXTRA_LDLIBS += -laxil-hyle

include ../../external/libhyle-bud/hyle-bud-wasm.mk
WASM_TARGETS = $(WASM_PATH)/gig.wasm $(WASM_PATH)/gig_detail.wasm
gig-src = ux/detail.cx $(HYLE_BUD_WASM_SRC) $(REPO_ROOT)/external/libpitch/src/music.c $(REPO_ROOT)/mods/common/site_wasm_app.c
gig-cflags = $(HYLE_BUD_WASM_CFLAGS) -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/mods/gig/include -I$(REPO_ROOT)/external/libpitch/include

include ../../build.mk

$(WASM_PATH)/gig.wasm: $(LIST_UX_DEPS)

$(WASM_PATH)/gig_detail.wasm: $(WASM_PATH)/gig.wasm
	ln -sf gig.wasm $@

$(TARGET): $(LIST_UX_DEPS) ux/detail.cx.c ux/edit.cx.c
