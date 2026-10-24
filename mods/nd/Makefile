REPO_ROOT ?= $(shell cd ../.. && pwd)
WASM_PATH ?= $(REPO_ROOT)/htdocs

TARGET = nd.so

ND_UX_MODULES = \
	$(REPO_ROOT)/external/nd-attr/ux/attr_ui.c \
	$(REPO_ROOT)/external/nd-equip/ux/equip_ui.c \
	$(REPO_ROOT)/external/nd-spell/ux/spell_ui.c \
	$(REPO_ROOT)/external/nd-fight/ux/fight_ui.c \
	$(REPO_ROOT)/external/nd-mortal/ux/mortal_ui.c

SRC = nd.c ux/nd_ui.c ux/nd_scene.c ux/nd_webgl.c ux/nd_lamina.c $(ND_UX_MODULES)
EXTRA_CFLAGS += -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/include -I$(REPO_ROOT)/external/axil/include -I$(REPO_ROOT)/external/axil-nd/include -I$(REPO_ROOT)/external/axil-tty/include -I$(REPO_ROOT)/external/liblamina/include -I$(REPO_ROOT)/external/nd-attr/include -I$(REPO_ROOT)/external/nd-equip/include -I$(REPO_ROOT)/external/nd-spell/include -I$(REPO_ROOT)/external/nd-fight/include -I$(REPO_ROOT)/external/nd-mortal/include -I$(REPO_ROOT)/external/libxylem/include -I$(REPO_ROOT)/mods/nd/ux
EXTRA_LDLIBS += -lbud -laxil -lxylem -lhyle-bud -lm

WASM_TARGETS = $(WASM_PATH)/nd.wasm
nd-src = ux/nd_ui.c ux/nd_scene.c ux/nd_webgl.c ux/nd_lamina.c $(ND_UX_MODULES) $(REPO_ROOT)/external/libxylem/src/libxylem-wasm.c
nd-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/common/ux -I$(REPO_ROOT)/include -I$(REPO_ROOT)/external/axil/include -I$(REPO_ROOT)/external/axil-nd/include -I$(REPO_ROOT)/external/axil-tty/include -I$(REPO_ROOT)/external/liblamina/include -I$(REPO_ROOT)/external/nd-attr/include -I$(REPO_ROOT)/external/nd-equip/include -I$(REPO_ROOT)/external/nd-spell/include -I$(REPO_ROOT)/external/nd-fight/include -I$(REPO_ROOT)/external/nd-mortal/include -I$(REPO_ROOT)/external/libxylem/include -I$(REPO_ROOT)/mods/nd/ux
nd-wasm-allowed = $(REPO_ROOT)/external/axil/wasm-allowed-imports.lst $(REPO_ROOT)/external/axil-tty/wasm-allowed-imports.lst $(REPO_ROOT)/external/liblamina/wasm-allowed-imports.lst

include ../../external/libhyle-bud/hyle-bud-wasm.mk
nd-src += $(HYLE_BUD_WASM_SRC)
nd-cflags += $(HYLE_BUD_WASM_CFLAGS)

include ../../build.mk

$(TARGET): ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/ux/site_page.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h

$(WASM_PATH)/nd.wasm: ux/nd_ui.h ux/nd_lamina.h $(REPO_ROOT)/external/axil-nd/include/nd/bcp.h $(REPO_ROOT)/external/axil/include/ttypt/axil-ws.h $(REPO_ROOT)/external/liblamina/include/ttypt/lam.h $(REPO_ROOT)/external/liblamina/include/ttypt/webgl.h ../common/ux/site_ui.c ../common/ux/site_ui.h ../common/ux/site_paths.c ../common/ux/site_layout.c ../common/ux/site_forms.c ../common/ux/site_media.c ../common/viewer_zoom.h
