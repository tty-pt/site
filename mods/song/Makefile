EXTRA_CFLAGS += -I../common
EXTRA_LDLIBS += -lbud -laxil-auth
EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a -ljson-c

# WASM targets must be defined before including build.mk so the shared
# all:/wasm: rules can see them during parse time.
WASM_TARGETS = $(WASM_PATH)/song_detail.wasm
song_detail-src = ux/detail.c
song_detail-cflags = -I$(REPO_ROOT)/mods/common -I$(REPO_ROOT)/mods/song/lib/transp

include ../../build.mk

$(TARGET) $(WASM_PATH)/song_detail.wasm: ../common/ux/site_ui.c ../common/ux/site_chrome.c ../common/ux/site_chrome.h

all: transp.so

transp.so:
	${MAKE} -C lib/transp
