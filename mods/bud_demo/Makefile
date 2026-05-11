SRC = bud_demo.c ux/bud_demo_fe.c

EXTRA_CFLAGS += -I../../external/bud/include -I../../external/axil/include -I../../external/qmap/include
EXTRA_LDLIBS += -L../../external/bud/lib -lbud -Wl,-rpath,external/bud/lib -L../../external/axil/lib -L../../external/qmap/lib

# WASM
WASM_TARGETS = $(WASM_PATH)/bud_demo.wasm
bud_demo-src = ux/bud_demo_fe.c

include ../../build.mk
