EXTRA_CFLAGS += -I../../external/bud/include -I../common
EXTRA_LDLIBS += -L../../external/bud/lib -lbud -laxil-auth
EXTRA_CFLAGS += -I./lib/transp
EXTRA_LDLIBS += ./lib/transp/libtransp.a -ljson-c

# WASM
WASM_TARGETS = $(WASM_PATH)/song_fe.wasm
song_fe-src = ux/all.c
song_fe-cflags = -I$(REPO_ROOT)/mods/common

all: transp.so $(TARGET)

transp.so:
	${MAKE} -C lib/transp

include ../../build.mk
