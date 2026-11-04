WASM_ONLY = 1
WASM_TARGETS = $(WASM_PATH)/site_chrome.wasm
site_chrome-src = ux/chrome.c ../common/ux/site_chrome.c $(REPO_ROOT)/mods/common/site_wasm_app.c
site_chrome-cflags = -I$(REPO_ROOT)/mods/common/ux

include ../../build.mk
