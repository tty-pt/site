WASM_ONLY = 1
WASM_TARGETS = $(WASM_PATH)/site_chrome.wasm
site_chrome-src = ux/chrome.c ../common/ux/site_chrome.c
site_chrome-cflags = -I$(REPO_ROOT)/mods/common/ux

include ../../build.mk

$(WASM_PATH)/site_chrome.wasm: ../common/ux/site_chrome.h
